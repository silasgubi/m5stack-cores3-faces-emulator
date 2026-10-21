#pragma once
#include <stddef.h>
#include <stdint.h>

// ROM in use: either one embedded in the firmware (data/roms/, packed in data/roms.bin) or one
// read from the SD card into PSRAM. The emulators read it through the pointer: Nofrendo
// via /xip (cgs/vfs_xip.c, which calls get_rom_ptr/get_rom_size), gnuboy directly
// through gnuboy_load_rom(ptr, len).

enum class Console : uint8_t { None, NES, GB };

struct RomSource {
    const uint8_t *data = nullptr;
    size_t size = 0;
    char name[64] = {0};  // file name without folder (also names the save file)
    Console console = Console::None;
};

// Optional ROMs embedded in the firmware, to play without an SD card: every .nes/.gb/.gbc
// in data/roms/ at build time (scripts/embed_data.py), in name order. Only valid ROMs are
// counted and index 0..count-1 walks them. The file name is the menu and save name.
int romEmbeddedCount();
RomSource romEmbedded(int index);
// Reads the file from the SD card into PSRAM. Returns data == nullptr on failure or when
// the file does not look like a ROM.
RomSource romLoadFromSd(const char *path);
// Sets the active ROM (the one get_rom_ptr returns).
void romSetActive(const RomSource &rom);
const RomSource &romActive();
// Console from the file extension (.nes, .gb, .gbc).
Console consoleFromName(const char *name);

extern "C" {
const uint8_t *get_rom_ptr(void);
size_t get_rom_size(void);
}
