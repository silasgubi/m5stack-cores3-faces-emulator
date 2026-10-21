#include <Arduino.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_layout.h>  // esp_ptr_external_ram
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "rom_source.h"

// Names shown in the menu for the embedded ROMs; they also name the save files, so keep
// the extension. Set them with build flags (see platformio.ini).
#ifndef EMBED_NES_NAME
#define EMBED_NES_NAME "Demo.nes"
#endif
#ifndef EMBED_GB_NAME
#define EMBED_GB_NAME "Demo.gb"
#endif

extern const uint8_t game_nes_start[] asm("_binary_data_game_nes_start");
extern const uint8_t game_nes_end[] asm("_binary_data_game_nes_end");
extern const uint8_t game_gb_start[] asm("_binary_data_game_gb_start");
extern const uint8_t game_gb_end[] asm("_binary_data_game_gb_end");

struct Embedded { const uint8_t *start, *end; const char *name, *file; Console console; };
static const Embedded EMBEDDED[] = {
    {game_nes_start, game_nes_end, EMBED_NES_NAME, "data/game.nes", Console::NES},
    {game_gb_start, game_gb_end, EMBED_GB_NAME, "data/game.gb", Console::GB},
};
static constexpr int EMBEDDED_SLOTS = sizeof(EMBEDDED) / sizeof(EMBEDDED[0]);

static int s_valid[EMBEDDED_SLOTS];  // valid index -> slot
static int s_validCount = -1;        // -1 = not scanned yet
static RomSource s_active;

Console consoleFromName(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return Console::None;
    if (!strcasecmp(dot, ".nes")) return Console::NES;
    if (!strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc")) return Console::GB;
    return Console::None;
}

// Smallest real ROMs: iNES is a 16-byte header plus at least one 16 KiB PRG bank (CHR can
// be RAM); a Game Boy cartridge header ends at 0x150. Anything else would only end in an
// emulator error screen, so it is treated as missing.
static bool looksLikeRom(Console console, const uint8_t *data, size_t size) {
    if (console == Console::NES) return size >= 16 + 16384 && memcmp(data, "NES\x1A", 4) == 0;
    if (console == Console::GB) return size >= 0x150;
    return false;
}

static void setName(RomSource &rom, const char *path) {
    const char *slash = strrchr(path, '/');
    snprintf(rom.name, sizeof(rom.name), "%s", slash ? slash + 1 : path);
    rom.console = consoleFromName(rom.name);
}

int romEmbeddedCount() {
    if (s_validCount >= 0) return s_validCount;
    s_validCount = 0;
    for (int i = 0; i < EMBEDDED_SLOTS; ++i) {
        const Embedded &e = EMBEDDED[i];
        const size_t size = (size_t)(e.end - e.start);
        if (looksLikeRom(e.console, e.start, size))
            s_valid[s_validCount++] = i;
        else
            log_printf("ROM: %s missing or invalid (%u bytes), not listed\n", e.file, (unsigned)size);
    }
    return s_validCount;
}

RomSource romEmbedded(int index) {
    RomSource rom;
    if (index < 0 || index >= romEmbeddedCount()) return rom;
    const Embedded &e = EMBEDDED[s_valid[index]];
    rom.data = e.start;
    rom.size = (size_t)(e.end - e.start);
    setName(rom, e.name);
    rom.console = e.console;  // the slot decides, whatever extension the name has
    return rom;
}

RomSource romLoadFromSd(const char *path) {
    RomSource rom;
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_printf("ROM: cannot open %s\n", path);
        return rom;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return rom;
    }
    // PSRAM (8 MB) holds the ROM; a small ROM still fits in internal RAM if PSRAM is missing.
    uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf && size <= 128 * 1024) buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        log_printf("ROM: no memory for %ld bytes\n", size);
        fclose(f);
        return rom;
    }
    size_t got = 0;
    while (got < (size_t)size) {
        const size_t n = fread(buf + got, 1, (size_t)size - got, f);
        if (n == 0) break;
        got += n;
    }
    fclose(f);
    if (got != (size_t)size) {
        log_printf("ROM: short read %u/%ld\n", (unsigned)got, size);
        free(buf);
        return rom;
    }
    setName(rom, path);
    if (!looksLikeRom(rom.console, buf, (size_t)size)) {
        log_printf("ROM: %s does not look like a ROM (%ld bytes)\n", rom.name, size);
        free(buf);
        return RomSource();
    }
    rom.data = buf;
    rom.size = (size_t)size;
    log_printf("ROM: %s, %u bytes in %s\n", rom.name, (unsigned)rom.size, esp_ptr_external_ram(buf) ? "PSRAM" : "RAM");
    return rom;
}

void romSetActive(const RomSource &rom) { s_active = rom; }
const RomSource &romActive() { return s_active; }

extern "C" const uint8_t *get_rom_ptr(void) { return s_active.data; }
extern "C" size_t get_rom_size(void) { return s_active.size; }
