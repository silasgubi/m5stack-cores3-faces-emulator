#pragma once
#include <stddef.h>

// CoreS3 microSD card (SPI bus shared with the display), mounted at /sd.
// Games in /sd/roms (created if missing); saves in /sd/nes_saves and /sd/gb_saves.
constexpr char SD_ROMS_DIR[] = "/sd/roms";
constexpr int SD_MAX_ROMS = 64;
constexpr int SD_NAME_MAX = 64;

bool sdMount();
bool sdMounted();
// Lists the .nes/.gb/.gbc files in /sd/roms in alphabetical order. Returns the count.
int sdListRoms(char names[][SD_NAME_MAX], int max);
