#include <M5Unified.h>
#include <Preferences.h>
#include <string.h>
#include "menu.h"
#include "pause_menu.h"
#include "battery.h"
#include "battery_icon.h"
#include "power_mode.h"
#include "sd_roms.h"
#include "faces_pad.h"
#include "settings.h"
#include "settings_menu.h"
#include "ui_text.h"

static constexpr int ROW_H = 18, ROWS = 11, LIST_Y = MENU_HEAD_H;
static constexpr uint32_t REPEAT_FIRST_MS = 400, REPEAT_MS = 90;
static constexpr int PAUSE_HOLD_FRAMES = 60;  // ~1 s at 60 Hz

static char s_names[SD_MAX_ROMS + 4][SD_NAME_MAX];
static Console s_consoles[SD_MAX_ROMS + 4];
static int s_count = 0;

// Battery at the top right of the title bar: icon in the corner, percent (size 1) right
// before it, and the bar's own text (e.g. the card count) ending further left.
static constexpr int BAT_ICON_X = 320 - 8 - BATTERY_ICON_W, BAT_ICON_Y = (MENU_HEAD_H - BATTERY_ICON_H) / 2;
static constexpr int BAT_PCT_END = BAT_ICON_X - 4, BAT_PCT_W = 4 * 6;  // "100%"
static constexpr int HEADER_TEXT_END = BAT_PCT_END - BAT_PCT_W - 12;
static constexpr int HEADER_TEXT_Y = 14;

static uint32_t s_batteryReadMs = 0;
static bool s_batteryRead = false;

static bool readBatteryIfDue(uint32_t periodMs) {
    const uint32_t now = millis();
    if (s_batteryRead && now - s_batteryReadMs < periodMs) return false;
    s_batteryRead = true;
    s_batteryReadMs = now;
    batteryRead();
    powerModeTick(batteryMv());  // the Smart latch also works while the console sits in the menu
    return true;
}

// Percent from the voltage (the AXP2101 gauge is still learning the cell); blank and a
// crossed icon with the rear switch on "O".
static void drawBatteryStatus() {
    const int mv = batteryMv();
    const bool present = batteryPresent(mv);
    const int pct = batteryPercentFromMv(mv);
    char text[8] = "";
    if (present) snprintf(text, sizeof(text), "%d%%", pct);
    M5.Display.fillRect(BAT_PCT_END - BAT_PCT_W, HEADER_TEXT_Y, BAT_PCT_W, 8, TFT_NAVY);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setCursor(BAT_PCT_END - 6 * (int)strlen(text), HEADER_TEXT_Y);
    M5.Display.print(text);
    drawBatteryIcon(BAT_ICON_X, BAT_ICON_Y, pct, batteryAnyCharging(), present, TFT_NAVY);
}

bool menuBatteryPoll(uint32_t periodMs) {
    if (!readBatteryIfDue(periodMs)) return false;
    drawBatteryStatus();
    return true;
}

void menuDrawTitleBar(const char *title, const char *right) {
    readBatteryIfDue(MENU_BATTERY_MS);  // the first bar of the boot already shows a real reading
    M5.Display.fillRect(0, 0, 320, MENU_HEAD_H, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 10);
    M5.Display.print(title);
    if (right && *right) {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
        M5.Display.setCursor(HEADER_TEXT_END - 6 * (int)strlen(right), HEADER_TEXT_Y);
        M5.Display.print(right);
    }
    drawBatteryStatus();
}

static void drawList(int cursor, int top) {
    M5.Display.setTextSize(2);
    for (int r = 0; r < ROWS; ++r) {
        const int i = top + r, y = LIST_Y + r * ROW_H;
        const bool sel = i == cursor;
        M5.Display.fillRect(0, y, 320, ROW_H, sel ? TFT_DARKGREEN : TFT_BLACK);
        if (i >= s_count) continue;
        M5.Display.setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, sel ? TFT_DARKGREEN : TFT_BLACK);
        M5.Display.setCursor(8, y + 1);
        char line[27];
        snprintf(line, sizeof(line), "%-25.25s", s_names[i]);
        char *dot = strrchr(line, '.');
        if (dot && strlen(s_names[i]) <= 25) *dot = '\0';  // drop the extension when the whole name fits
        M5.Display.print(line);
        M5.Display.setTextSize(1);
        M5.Display.setCursor(320 - 8 - 3 * 6, y + 5);
        M5.Display.print(s_consoles[i] == Console::GB ? "GB " : "NES");
        M5.Display.setTextSize(2);
    }
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.fillRect(0, 240 - 10, 320, 10, TFT_BLACK);
    M5.Display.setCursor(8, 240 - 9);
    M5.Display.print(TXT_MENU_FOOTER);  // 6 px font: at most 51 chars from x=8 (ui_text.h)
}

// Help screen: B in the menu, or Help in the settings. Any button goes back.
static void drawHelp() {
    M5.Display.fillScreen(TFT_BLACK);
    menuDrawTitleBar(TXT_HELP, nullptr);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    int y = LIST_Y + 6;
    for (const char *l : TXT_HELP_LINES) {
        M5.Display.setCursor(8, y);
        M5.Display.print(l);
        y += 12;
    }
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(8, 240 - 9);
    M5.Display.print(TXT_HELP_FOOTER);
}

static void waitAnyButton() {
    while (facesPadRead()) delay(16);   // release the button that opened this screen
    while (!facesPadRead()) delay(16);  // wait for a new press
    while (facesPadRead()) delay(16);   // and its release, so the menu does not react to it
}

void menuShowHelp() {
    drawHelp();
    waitAnyButton();
}

// No game on the card and no valid embedded ROM: say how to add games. Select opens
// the settings, which is how a first game arrives over Wi-Fi; any other button
// restarts, and the boot mounts the card again.
static void drawNoGames() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 70);
    M5.Display.print(TXT_NO_GAMES);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    int y = 100;
    for (const char *l : TXT_NO_GAMES_LINES) {
        M5.Display.setCursor(8, y);
        M5.Display.print(l);
        y += 12;
    }
}

[[noreturn]] static void showNoGames() {
    log_printf("menu: no games (card %s)\n", sdMounted() ? "mounted, /roms empty" : "missing");
    drawNoGames();
    for (;;) {
        while (facesPadRead()) delay(16);  // release the button that led here
        uint8_t pad;
        while (!(pad = facesPadRead())) delay(16);
        while (facesPadRead()) delay(16);
        if (!(pad & FACES_SELECT)) esp_restart();
        settingsMenuRun();  // File transfer inside it restarts on exit; the rest returns here
        drawNoGames();
    }
}

extern const uint8_t logo_png_start[] asm("_binary_data_logo_png_start");
extern const uint8_t logo_png_end[] asm("_binary_data_logo_png_end");

// Optional boot image. Without data/logo.png the build embeds a 1-byte placeholder
// (scripts/embed_data.py), so only a real PNG is drawn.
static bool drawLogo() {
    static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    const uint8_t *png = logo_png_start;
    const size_t size = (size_t)(logo_png_end - logo_png_start);
    if (size < 24 || memcmp(png, PNG_SIG, sizeof(PNG_SIG)) != 0) return false;
    // Width and height from the IHDR chunk (big endian, bytes 16..23) to center the image.
    const int w = (png[18] << 8) | png[19], h = (png[22] << 8) | png[23];
    M5.Display.fillScreen(TFT_WHITE);  // logos are usually drawn for a light background
    return M5.Display.drawPng(png, size, max(0, (320 - w) / 2), max(0, (240 - h) / 2));
}

void menuSplash() {
    if (drawLogo()) return;
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.setCursor((320 - M5.Display.textWidth(TXT_SPLASH_TITLE)) / 2, 92);
    M5.Display.print(TXT_SPLASH_TITLE);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor((320 - M5.Display.textWidth(TXT_SPLASH_SUB)) / 2, 132);
    M5.Display.print(TXT_SPLASH_SUB);
}

void menuMessage(const char *title, const char *detail, uint32_t waitMs) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 90);
    M5.Display.print(title);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(8, 120);
    M5.Display.print(detail);
    if (waitMs == 0) for (;;) delay(1000);
    delay(waitMs);
}

RomSource menuChooseRom() {
    s_count = sdMounted() ? sdListRoms(s_names, SD_MAX_ROMS) : 0;
    const int sdCount = s_count;
    for (int i = 0; i < sdCount; ++i) s_consoles[i] = consoleFromName(s_names[i]);
    for (int i = 0; i < romEmbeddedCount(); ++i) {  // embedded ROMs come last
        const RomSource e = romEmbedded(i);
        snprintf(s_names[s_count], SD_NAME_MAX, "%s", e.name);
        s_consoles[s_count++] = e.console;
    }
    if (s_count == 0) showNoGames();  // never returns, so the "% s_count" below is safe

    Preferences p;
    p.begin("nes", true);
    String last = p.getString("last_rom", "");
    p.end();
    int cursor = sdCount < s_count ? sdCount : 0;  // first embedded ROM, else first card game
    for (int i = 0; i < s_count; ++i) if (last == s_names[i]) cursor = i;

    M5.Display.fillScreen(TFT_BLACK);
    char right[40];
    if (sdMounted())
        snprintf(right, sizeof(right), TXT_ON_CARD_FMT, sdCount);
    else
        snprintf(right, sizeof(right), "%s", TXT_NO_CARD);
    menuDrawTitleBar(TXT_GAMES, right);
    int top = max(0, min(cursor - ROWS / 2, s_count - ROWS));
    drawList(cursor, top);

    uint8_t prev = facesPadRead();  // a button still held from the boot or a restart is not a press
    uint32_t nextRepeat = 0;
    for (;;) {
        const uint8_t pad = facesPadRead();
        const uint8_t pressed = pad & ~prev;
        const uint32_t now = millis();
        int move = 0;
        if (pressed & (FACES_UP | FACES_DOWN)) {
            move = (pad & FACES_UP) ? -1 : 1;
            nextRepeat = now + REPEAT_FIRST_MS;
        } else if ((pad & (FACES_UP | FACES_DOWN)) && (int32_t)(now - nextRepeat) >= 0) {
            move = (pad & FACES_UP) ? -1 : 1;
            nextRepeat = now + REPEAT_MS;
        }
        if (move) {
            cursor = (cursor + move + s_count) % s_count;
            if (cursor < top) top = cursor;
            if (cursor >= top + ROWS) top = cursor - ROWS + 1;
            drawList(cursor, top);
        }
        if (pressed & (FACES_A | FACES_START)) break;
        if (pressed & (FACES_B | FACES_SELECT)) {
            if (pressed & FACES_B)
                menuShowHelp();
            else
                settingsMenuRun();  // returns on Back or B, possibly with that button still held
            M5.Display.fillScreen(TFT_BLACK);
            menuDrawTitleBar(TXT_GAMES, right);
            drawList(cursor, top);
            prev = facesPadRead();  // a held A or B must not start a game or open the help
            continue;
        }
        menuBatteryPoll(MENU_BATTERY_MS);
        settingsTick();
        prev = pad;
        delay(16);
    }

    p.begin("nes", false);
    p.putString("last_rom", s_names[cursor]);
    p.end();

    if (cursor >= sdCount) return romEmbedded(cursor - sdCount);
    char path[sizeof(SD_ROMS_DIR) + 1 + SD_NAME_MAX];
    snprintf(path, sizeof(path), "%s/%s", SD_ROMS_DIR, s_names[cursor]);
    menuMessage(TXT_LOADING, s_names[cursor], 1);
    RomSource rom = romLoadFromSd(path);
    if (!rom.data) {
        menuMessage(TXT_READ_FAILED, path, 3000);
        return menuChooseRom();
    }
    return rom;
}

static void (*s_onQuit)() = nullptr;
void menuOnQuit(void (*fn)()) { s_onQuit = fn; }

void menuCheckQuitCombo(uint8_t padBits) {
    static int held = 0;
    const bool combo = (padBits & FACES_SELECT) && (padBits & FACES_START);
    held = combo ? held + 1 : 0;
    if (held < PAUSE_HOLD_FRAMES) return;
    held = 0;
    pauseMenuRun();
}

void menuQuitToGameList() {
    if (s_onQuit) s_onQuit();
    settingsFlush();  // a brightness change from the last 3 s is not lost
    Preferences p;
    p.begin("nes", false);
    p.putBool("go_menu", true);
    p.end();
    log_printf("menu: quit to the game list, restarting\n");
    delay(100);
    esp_restart();
}
