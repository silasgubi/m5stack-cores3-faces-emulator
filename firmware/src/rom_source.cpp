#include <Arduino.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_layout.h>  // esp_ptr_external_ram
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "rom_source.h"

// Embedded ROMs: data/roms.bin, packed at build time by scripts/embed_data.py:
// "ROMP", u32 count, count x {char name[64], u32 offset, u32 size}, then the ROMs.
extern const uint8_t roms_bin_start[] asm("_binary_data_roms_bin_start");
extern const uint8_t roms_bin_end[] asm("_binary_data_roms_bin_end");

struct PackEntry {
    char name[64];
    uint32_t offset, size;
};
static_assert(sizeof(PackEntry) == 72, "must match scripts/embed_data.py");

static constexpr int MAX_EMBEDDED = 64;
static int s_valid[MAX_EMBEDDED];  // valid index -> pack entry
static int s_validCount = -1;      // -1 = not scanned yet
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

static const PackEntry *packEntries(uint32_t &count) {
    const size_t packSize = (size_t)(roms_bin_end - roms_bin_start);
    count = 0;
    if (packSize < 8 || memcmp(roms_bin_start, "ROMP", 4) != 0) return nullptr;
    memcpy(&count, roms_bin_start + 4, 4);
    if (8 + (size_t)count * sizeof(PackEntry) > packSize) count = 0;
    return (const PackEntry *)(roms_bin_start + 8);  // 4-byte aligned like the pack
}

int romEmbeddedCount() {
    if (s_validCount >= 0) return s_validCount;
    s_validCount = 0;
    uint32_t count;
    const PackEntry *e = packEntries(count);
    const size_t packSize = (size_t)(roms_bin_end - roms_bin_start);
    for (uint32_t i = 0; i < count && s_validCount < MAX_EMBEDDED; ++i) {
        char name[sizeof(e[i].name) + 1] = {0};
        memcpy(name, e[i].name, sizeof(e[i].name));
        const bool inPack = e[i].offset <= packSize && e[i].size <= packSize - e[i].offset;
        if (inPack && looksLikeRom(consoleFromName(name), roms_bin_start + e[i].offset, e[i].size))
            s_valid[s_validCount++] = (int)i;
        else
            log_printf("ROM: embedded %s invalid (%u bytes), not listed\n", name, (unsigned)e[i].size);
    }
    return s_validCount;
}

RomSource romEmbedded(int index) {
    RomSource rom;
    if (index < 0 || index >= romEmbeddedCount()) return rom;
    uint32_t count;
    const PackEntry &e = packEntries(count)[s_valid[index]];
    char name[sizeof(e.name) + 1] = {0};
    memcpy(name, e.name, sizeof(e.name));
    rom.data = roms_bin_start + e.offset;
    rom.size = e.size;
    setName(rom, name);
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
