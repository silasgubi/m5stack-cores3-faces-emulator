// File transfer mode: own Wi-Fi access point, a DNS server that answers every name with
// the console (so phones open the page as a captive portal) and a small web API for the
// SD card. It runs alone, from the menu, and leaves through esp_restart(): Wi-Fi takes RAM
// and CPU time the emulators need. The card shares the SPI bus with the display, but
// everything here runs in this one loop task, so card writes and drawing never overlap.

#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>
#include <Preferences.h>
#include <lwip/sockets.h>
#include <string.h>
#include <strings.h>
#include "wifi_transfer.h"
#include "faces_pad.h"
#include "rom_source.h"
#include "sd_roms.h"
#include "ui_text.h"
#include "web_page.h"

// On-screen text: TXT_FT_* in ui_text.h.

// ---- Network ----
// 10.10.0.1 instead of the ESP32 default AP address: short to type, and the page text uses it too.
constexpr uint8_t AP_IP[4] = {10, 10, 0, 1};
constexpr char AP_URL[] = "http://10.10.0.1";
constexpr char AP_HOME[] = "http://10.10.0.1/";
constexpr char SSID_PREFIX[] = "M5-Emulator-";

// ---- Folders, as paths from the card root (the SD library adds the /sd mount) ----
// Same folders as sd_roms.h (SD_ROMS_DIR), cgs/nes_save.c and gb/gb_save.cpp.
struct Folder {
    const char *key;   // name used by the web page and the API
    const char *path;
    bool roms;  // true: .nes .gb .gbc; false: .sav
};
static constexpr Folder FOLDERS[] = {
    {"roms", "/roms", true},
    {"nes_saves", "/nes_saves", false},
    {"gb_saves", "/gb_saves", false},
};

static constexpr size_t NAME_LEN_MAX = SD_NAME_MAX - 1;  // the menu keeps names in SD_NAME_MAX bytes
static constexpr size_t PATH_LEN = 96;                   // "/nes_saves/" + 63 + ".part" + NUL fits
static constexpr size_t WRITE_BUF = 16 * 1024;           // card writes in 32-sector blocks
static constexpr uint64_t SPACE_MARGIN = 64 * 1024;      // cluster slack and FAT updates
static constexpr int LIST_MAX = 256;                     // per folder, bounds the JSON size
static constexpr uint32_t PAD_MS = 30, PROGRESS_MS = 500, STATIONS_MS = 1000;

// ---- Layout (320x240) ----
static constexpr int HEAD_H = 36;
static constexpr int QR_X = 204, QR_Y = 42, QR_BOX = 112, QR_PAD = 8;  // white box = quiet zone
static constexpr int SCAN_Y = 164, STATUS_Y = 186, BAR_Y = 200, FOOT_Y = 231;

// ---- State ----
static WebServer *s_server = nullptr;
static DNSServer *s_dns = nullptr;
static uint8_t *s_buf = nullptr;  // upload write buffer (heap: only exists in this mode)
static uint8_t s_padPrev = 0;

enum class Tone : uint8_t { Info, Busy, Ok, Error };
static char s_status[ui::LINE_MAX_1 + 1];
static Tone s_tone = Tone::Info;
static int s_progress = -1;  // 0..100 draws a bar under the status line, -1 hides it
static bool s_statusDirty = true;

// One file of a multipart upload. The request may carry several (the page sends one).
struct Upload {
    File file;
    char name[SD_NAME_MAX];
    char path[PATH_LEN];
    char tmp[PATH_LEN];  // "<path>.part" until the whole file is on the card
    bool active;         // false: no file open, the rest of this file part is ignored
    size_t buffered, written;
    uint32_t startMs, drawMs;
    const char *err;  // first error code of the request, sent to the page
    int saved;        // files saved in this request
};
static Upload s_up;

// ---- Helpers ----
static const char *baseName(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static const Folder *folderByKey(const String &key) {
    for (const Folder &f : FOLDERS)
        if (key == f.key) return &f;
    return nullptr;
}

static bool hasExt(const char *name, const char *ext) {
    const size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

static bool extFits(const Folder &dir, const char *name) {
    return dir.roms ? consoleFromName(name) != Console::None : hasExt(name, ".sav");
}

// Names of files already on the card (delete, download): only refuse what could leave the
// folder or break the Content-Disposition header. UTF-8 names from a PC are fine here.
static bool nameSafe(const String &s) {
    const char *name = s.c_str();
    const size_t n = strlen(name);
    if (n != s.length() || n == 0 || n > NAME_LEN_MAX || name[0] == '.' || strstr(name, "..")) return false;
    for (const char *p = name; *p; ++p) {
        const unsigned char c = *p;
        if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == '"') return false;
    }
    return true;
}

// Names of new files: also printable ASCII only (the console font has nothing else; the
// page already replaces accents) and none of the characters FAT forbids.
static bool nameValidForNewFile(const String &s) {
    if (!nameSafe(s)) return false;
    const char *name = s.c_str();
    const size_t n = strlen(name);
    if (name[0] == ' ' || name[n - 1] == ' ' || name[n - 1] == '.') return false;
    for (const char *p = name; *p; ++p)
        if ((unsigned char)*p > 0x7E || strchr(":*?<>|", *p)) return false;
    return true;
}

// Builds the card path of an existing file from the request. Returns an error code for
// the page, or nullptr.
static const char *existingPath(const Folder *dir, const String &name, char *path) {
    if (!dir) return "dir";
    if (!nameSafe(name)) return "name";
    if (!extFits(*dir, name.c_str())) return "ext";
    snprintf(path, PATH_LEN, "%s/%s", dir->path, name.c_str());
    return nullptr;
}

static uint64_t cardFree() { return SD.totalBytes() - SD.usedBytes(); }

// ---- Screen ----
// Status line: the file name is shortened so the number after it stays visible.
static void setStatus(Tone tone, const char *fmt, const char *name = "", unsigned num = 0) {
    const int room = (int)ui::LINE_MAX_1 - snprintf(nullptr, 0, fmt, "", num);
    char shortName[ui::LINE_MAX_1 + 1] = "";
    if ((int)strlen(name) <= room)
        snprintf(shortName, sizeof(shortName), "%s", name);
    else if (room > 2)
        snprintf(shortName, sizeof(shortName), "%.*s..", room - 2, name);
    snprintf(s_status, sizeof(s_status), fmt, shortName, num);
    s_tone = tone;
    s_statusDirty = true;
}

static void drawStatus() {
    static const uint16_t COLORS[] = {TFT_WHITE, TFT_YELLOW, TFT_GREEN, TFT_RED};
    M5.Display.fillRect(0, STATUS_Y, 320, BAR_Y + 8 - STATUS_Y, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLORS[(int)s_tone], TFT_BLACK);
    M5.Display.setCursor(8, STATUS_Y);
    M5.Display.print(s_status);
    if (s_progress >= 0) {
        M5.Display.drawRect(8, BAR_Y, 304, 8, TFT_DARKGREY);
        M5.Display.fillRect(9, BAR_Y + 1, 302 * s_progress / 100, 6, TFT_YELLOW);
    }
    M5.Display.waitDisplay();  // the bus is free again before the next card access
    s_statusDirty = false;
}

static void drawStations(int n) {
    char s[24];
    snprintf(s, sizeof(s), TXT_FT_CLIENTS_FMT, n);
    M5.Display.fillRect(160, FOOT_Y - 1, 160, 10, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(n ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(320 - 8 - 6 * (int)strlen(s), FOOT_Y);
    M5.Display.print(s);
    M5.Display.waitDisplay();
}

static void drawLabelValue(int y, const char *label, const char *value, uint16_t color) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.print(label);
    M5.Display.setTextSize(2);  // 16 chars = 192 px, ends before the QR box
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setCursor(8, y + 12);
    M5.Display.print(value);
}

static void drawScreen(const char *ssid, const char *pass, const char *qrText) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(0, 0, 320, HEAD_H, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 10);
    M5.Display.print(TXT_FT_TITLE);
    drawLabelValue(44, TXT_FT_NETWORK, ssid, TFT_WHITE);
    drawLabelValue(80, TXT_FT_PASSWORD, pass, TFT_YELLOW);
    drawLabelValue(116, TXT_FT_OPEN, AP_URL, TFT_CYAN);
    // Cameras need a light margin of about 4 modules around the code: the white box gives
    // 8 px plus the centering slack (version 3, 29 modules of 3 px in 96 px).
    M5.Display.fillRect(QR_X, QR_Y, QR_BOX, QR_BOX, TFT_WHITE);
    M5.Display.qrcode(qrText, QR_X + QR_PAD, QR_Y + QR_PAD, QR_BOX - 2 * QR_PAD);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(8, SCAN_Y);
    M5.Display.print(TXT_FT_SCAN);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(8, FOOT_Y);
    M5.Display.print(TXT_FT_FOOTER);
}

// ---- Leaving ----
// The flag makes the next boot show the menu, like Select+Start in a game (menu.cpp).
[[noreturn]] static void restartToMenu() {
    Preferences p;
    p.begin("nes", false);
    p.putBool("go_menu", true);
    p.end();
    log_printf("FT: restarting into the menu\n");
    delay(100);
    esp_restart();
}

static void waitAnyButton() {
    while (facesPadRead()) delay(16);   // release the button that opened this screen
    while (!facesPadRead()) delay(16);  // wait for a new press
    while (facesPadRead()) delay(16);   // and its release
}

[[noreturn]] static void showMessageAndRestart(const char *title, const char *const *lines, size_t count) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 70);
    M5.Display.print(title);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    for (size_t i = 0; i < count; ++i) {
        M5.Display.setCursor(8, 100 + 12 * (int)i);
        M5.Display.print(lines[i]);
    }
    waitAnyButton();
    WiFi.mode(WIFI_OFF);
    restartToMenu();
}

[[noreturn]] static void exitTransfer() {
    log_printf("FT: exit\n");
    if (s_up.active) {  // B in the middle of an upload: drop the partial file
        s_up.file.close();
        SD.remove(s_up.tmp);
    }
    s_server->stop();
    s_dns->stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    restartToMenu();
}

// New press of B only: a button still held from the menu does not count.
static bool bPressed() {
    const uint8_t pad = facesPadRead();
    const bool b = (pad & ~s_padPrev) & FACES_B;
    s_padPrev = pad;
    return b;
}

// ---- Upload ----
// A phone that leaves the network mid-upload never closes the connection, and WebServer
// would wait for the rest of the body forever (and B too, since the loop is stuck inside
// handleClient). Keepalive probes drop such a connection after about 11 s.
static void enableKeepAlive(const WiFiClient &client) {
    const int fd = client.fd();
    if (fd < 0) return;
    int on = 1, idle = 5, interval = 2, count = 3;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

static bool flushBuffer() {
    if (s_up.buffered == 0) return true;
    const size_t w = s_up.file.write(s_buf, s_up.buffered);
    s_up.written += w;
    const bool ok = w == s_up.buffered;
    s_up.buffered = 0;
    return ok;
}

// WebServer hands over 1436-byte pieces; the card is much faster with big blocks.
static bool writeChunk(const uint8_t *data, size_t len) {
    if (!s_buf) {
        const size_t w = s_up.file.write(data, len);
        s_up.written += w;
        return w == len;
    }
    while (len) {
        const size_t n = min(len, WRITE_BUF - s_up.buffered);
        memcpy(s_buf + s_up.buffered, data, n);
        s_up.buffered += n;
        data += n;
        len -= n;
        if (s_up.buffered == WRITE_BUF && !flushBuffer()) return false;
    }
    return true;
}

// Ends the current file with an error: the partial file goes away, the rest of this file
// part is ignored, and the code is sent to the page when the request ends.
static void rejectUpload(const char *code, const char *fmt) {
    if (s_up.active) s_up.file.close();
    if (s_up.tmp[0]) SD.remove(s_up.tmp);
    s_up.active = false;
    s_up.buffered = 0;
    if (!s_up.err) s_up.err = code;
    s_progress = -1;
    log_printf("FT: upload of \"%s\" failed: %s\n", s_up.name, code);
    setStatus(Tone::Error, fmt, s_up.name);
}

// Progress on the screen and a way out with B during long uploads. Runs between card
// writes (this chunk is already on the card or in the buffer), so drawing here is safe.
static void uploadProgress(size_t received) {
    const uint32_t now = millis();
    if (now - s_up.drawMs < PROGRESS_MS) return;
    s_up.drawMs = now;
    const int total = s_server->clientContentLength();
    s_progress = total > 0 ? (int)min<uint64_t>(99, (uint64_t)received * 100 / total) : 0;
    setStatus(Tone::Busy, TXT_FT_RECEIVING_FMT, s_up.name, (unsigned)s_progress);
    drawStatus();
    if (bPressed()) exitTransfer();
}

static void uploadStart(const HTTPUpload &up) {
    s_up.active = false;
    s_up.buffered = s_up.written = 0;
    s_up.tmp[0] = '\0';
    snprintf(s_up.name, sizeof(s_up.name), "%s", up.filename.c_str());
    const Folder *dir = folderByKey(s_server->arg("dir"));
    if (!dir) return rejectUpload("dir", TXT_FT_REJECTED_FMT);
    if (!nameValidForNewFile(up.filename)) return rejectUpload("name", TXT_FT_REJECTED_FMT);
    if (!extFits(*dir, s_up.name)) return rejectUpload("ext", TXT_FT_REJECTED_FMT);
    // Content-Length covers the whole form, a little more than the file: a safe estimate.
    if ((uint64_t)s_server->clientContentLength() + SPACE_MARGIN > cardFree())
        return rejectUpload("space", TXT_FT_REJECTED_FMT);
    if (!SD.exists(dir->path)) SD.mkdir(dir->path);  // the save folders appear on first use
    snprintf(s_up.path, sizeof(s_up.path), "%s/%s", dir->path, s_up.name);
    snprintf(s_up.tmp, sizeof(s_up.tmp), "%s.part", s_up.path);
    s_up.file = SD.open(s_up.tmp, FILE_WRITE);
    if (!s_up.file) return rejectUpload("write", TXT_FT_SAVE_FAILED_FMT);
    s_up.active = true;
    enableKeepAlive(s_server->client());
    s_up.startMs = s_up.drawMs = millis();
    s_progress = 0;
    setStatus(Tone::Busy, TXT_FT_RECEIVING_FMT, s_up.name, 0);
    drawStatus();
    log_printf("FT: receiving %s (%d bytes of form data)\n", s_up.path, s_server->clientContentLength());
}

static void uploadEnd(const HTTPUpload &up) {
    const bool flushed = flushBuffer();
    s_up.file.close();
    s_up.active = false;
    if (!flushed || s_up.written != up.totalSize) return rejectUpload("write", TXT_FT_SAVE_FAILED_FMT);
    // FAT rename does not replace an existing file. The old copy (e.g. the save being
    // restored over) only goes away once the new one is complete.
    if (SD.exists(s_up.path)) SD.remove(s_up.path);
    if (!SD.rename(s_up.tmp, s_up.path)) return rejectUpload("write", TXT_FT_SAVE_FAILED_FMT);
    s_up.tmp[0] = '\0';
    s_up.saved++;
    s_progress = -1;
    const uint32_t ms = max<uint32_t>(1, millis() - s_up.startMs);
    log_printf("FT: saved %s, %u bytes in %u ms (%u KB/s)\n", s_up.path, (unsigned)up.totalSize, (unsigned)ms,
               (unsigned)((uint64_t)up.totalSize * 1000 / 1024 / ms));
    setStatus(Tone::Ok, TXT_FT_UPLOADED_FMT, s_up.name, (unsigned)((up.totalSize + 1023) / 1024));
}

// Called by WebServer for each part of the upload, inside handleClient.
static void handleUploadChunk() {
    const HTTPUpload &up = s_server->upload();
    switch (up.status) {
    case UPLOAD_FILE_START:
        uploadStart(up);
        break;
    case UPLOAD_FILE_WRITE:
        if (!s_up.active) return;
        if (!writeChunk(up.buf, up.currentSize)) return rejectUpload("write", TXT_FT_SAVE_FAILED_FMT);
        uploadProgress(up.totalSize + up.currentSize);
        break;
    case UPLOAD_FILE_END:
        if (s_up.active) uploadEnd(up);
        break;
    case UPLOAD_FILE_ABORTED:
        // Connection lost: WebServer will not call handleUploadDone for this request.
        if (s_up.active) {
            s_up.file.close();
            SD.remove(s_up.tmp);
            s_up.active = false;
            s_progress = -1;
            log_printf("FT: upload of %s interrupted\n", s_up.path);
            setStatus(Tone::Error, TXT_FT_INTERRUPTED_FMT, s_up.name);
        }
        s_up.err = nullptr;
        s_up.saved = 0;
        break;
    }
}

// Answer once the whole upload request was read.
static void handleUploadDone() {
    const char *err = s_up.err ? s_up.err : (s_up.saved ? nullptr : "name");  // "name": no file in the form
    s_up.err = nullptr;
    s_up.saved = 0;
    if (err)
        s_server->send(400, "text/plain", err);
    else
        s_server->send(200, "text/plain", "ok");
}

// ---- Other requests ----
static void appendJsonString(String &out, const char *s) {
    out += '"';
    for (; *s; ++s) {
        const unsigned char c = *s;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            out += esc;
        } else {
            out += (char)c;  // UTF-8 bytes pass through unchanged
        }
    }
    out += '"';
}

static void appendFolder(String &out, const Folder &dir) {
    out += ",\"";
    out += dir.key;
    out += "\":[";
    File d = SD.open(dir.path);
    int n = 0;
    if (d && d.isDirectory()) {
        while (n < LIST_MAX) {
            File f = d.openNextFile();
            if (!f) break;
            const char *name = baseName(f.name());
            // Same filter as the game menu; also hides .part leftovers and macOS "._" files.
            if (!f.isDirectory() && name[0] != '.' && extFits(dir, name)) {
                if (n++) out += ',';
                out += "{\"n\":";
                appendJsonString(out, name);
                out += ",\"s\":";
                out += (unsigned long)f.size();
                out += '}';
            }
            f.close();
        }
    }
    if (d) d.close();
    out += ']';
}

// GET /api/list: {"free":B,"total":B,"max":64,"roms":[{"n":name,"s":bytes}],"nes_saves":[],"gb_saves":[]}
static void handleList() {
    char head[96];
    snprintf(head, sizeof(head), "{\"free\":%llu,\"total\":%llu,\"max\":%d", (unsigned long long)cardFree(),
             (unsigned long long)SD.totalBytes(), SD_MAX_ROMS);
    String out;
    out.reserve(2048);
    out += head;
    for (const Folder &f : FOLDERS) appendFolder(out, f);
    out += '}';
    s_server->sendHeader("Cache-Control", "no-store");
    s_server->send(200, "application/json", out);
}

// POST /delete, form fields dir and name.
static void handleDelete() {
    const String name = s_server->arg("name");
    char path[PATH_LEN];
    const char *err = existingPath(folderByKey(s_server->arg("dir")), name, path);
    if (err) return s_server->send(400, "text/plain", err);
    if (!SD.remove(path)) return s_server->send(404, "text/plain", "missing");
    log_printf("FT: deleted %s\n", path);
    setStatus(Tone::Ok, TXT_FT_DELETED_FMT, name.c_str());
    s_server->send(200, "text/plain", "ok");
}

// GET /download?dir=...&name=...
static void handleDownload() {
    const String name = s_server->arg("name");
    char path[PATH_LEN];
    const char *err = existingPath(folderByKey(s_server->arg("dir")), name, path);
    if (err) return s_server->send(400, "text/plain", err);
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) return s_server->send(404, "text/plain", "missing");
    char disposition[PATH_LEN];  // nameSafe() already refused quotes and control characters
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name.c_str());
    s_server->sendHeader("Content-Disposition", disposition);
    const size_t size = f.size();
    const size_t sent = s_server->streamFile(f, "application/octet-stream");
    f.close();
    log_printf("FT: sent %s, %u of %u bytes\n", path, (unsigned)sent, (unsigned)size);
    if (sent == size)
        setStatus(Tone::Ok, TXT_FT_DOWNLOADED_FMT, name.c_str());
    else
        setStatus(Tone::Error, TXT_FT_INTERRUPTED_FMT, name.c_str());
}

static void handleRoot() {
    s_server->sendHeader("Cache-Control", "no-store");
    s_server->send_P(200, "text/html", WEB_PAGE);
}

// Captive portal: after joining, phones fetch a known URL (Android /generate_204, Apple
// /hotspot-detect.html, Windows /connecttest.txt...). Any other answer than the expected
// one, here a redirect to the page, makes them offer to open it.
static void handleNotFound() {
    s_server->sendHeader("Location", AP_HOME, true);
    s_server->send(302, "text/plain", "");
}

// ---- Entry point ----
void fileTransferRun() {
    s_padPrev = facesPadRead();
    if (!sdMounted()) {
        log_printf("FT: no SD card\n");
        showMessageAndRestart(TXT_FT_NO_CARD, TXT_FT_NO_CARD_LINES,
                              TXT_FT_NO_CARD_COUNT);
    }

    // Leftovers of an upload cut by a power loss: never listed, they only take space.
    for (const Folder &dir : FOLDERS) {
        char stale[4][SD_NAME_MAX];
        int n = 0;
        File d = SD.open(dir.path);
        if (d && d.isDirectory()) {
            for (File f = d.openNextFile(); f && n < 4; f = d.openNextFile()) {
                const char *name = baseName(f.name());
                if (!f.isDirectory() && hasExt(name, ".part")) snprintf(stale[n++], SD_NAME_MAX, "%s", name);
                f.close();
            }
        }
        if (d) d.close();
        for (int i = 0; i < n; ++i) {
            char path[PATH_LEN];
            snprintf(path, sizeof(path), "%s/%s", dir.path, stale[i]);
            SD.remove(path);
            log_printf("FT: removed stale %s\n", path);
        }
    }

    // Not saved to NVS: the SSID and password of each session must not outlive it.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", SSID_PREFIX, mac[4], mac[5]);
    // Drawn only now: esp_random() is a true random source once the radio is on. The
    // limit is a multiple of 10^8, so every 8-digit password is equally likely.
    uint32_t r;
    do r = esp_random();
    while (r >= 4200000000u);
    char pass[12];
    snprintf(pass, sizeof(pass), "%08lu", (unsigned long)(r % 100000000u));

    const IPAddress ip(AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
    const bool apOk = WiFi.softAP(ssid, pass) && WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    if (!apOk) {
        log_printf("FT: access point failed\n");
        showMessageAndRestart(TXT_FT_WIFI_FAILED, TXT_FT_WIFI_FAILED_LINES,
                              TXT_FT_WIFI_FAILED_COUNT);
    }
    log_printf("FT: access point %s up at %s\n", ssid, AP_URL);

    s_buf = (uint8_t *)malloc(WRITE_BUF);  // without it, chunks go straight to the card
    s_dns = new DNSServer();
    s_dns->setErrorReplyCode(DNSReplyCode::NoError);
    s_dns->start(53, "*", ip);
    s_server = new WebServer(80);
    s_server->on("/", HTTP_GET, handleRoot);
    s_server->on("/api/list", HTTP_GET, handleList);
    s_server->on("/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
    s_server->on("/delete", HTTP_POST, handleDelete);
    s_server->on("/download", HTTP_GET, handleDownload);
    s_server->on("/favicon.ico", HTTP_GET, [] { s_server->send(404, "text/plain", ""); });
    s_server->onNotFound(handleNotFound);
    s_server->begin();

    char qr[80];  // Wi-Fi QR format understood by the Android and iOS cameras
    snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
    drawScreen(ssid, pass, qr);
    setStatus(Tone::Info, TXT_FT_WAITING);
    drawStatus();
    int stations = -1;
    uint32_t nextPad = 0, nextStations = 0;
    for (;;) {
        s_server->handleClient();
        s_dns->processNextRequest();
        const uint32_t now = millis();
        if ((int32_t)(now - nextPad) >= 0) {
            nextPad = now + PAD_MS;
            if (bPressed()) exitTransfer();
        }
        if ((int32_t)(now - nextStations) >= 0) {
            nextStations = now + STATIONS_MS;
            const int n = WiFi.softAPgetStationNum();
            if (n != stations) {
                stations = n;
                drawStations(n);
            }
        }
        if (s_statusDirty) drawStatus();
        delay(2);  // lets the idle task run: the watchdogs are still on in the menu
    }
}
