// Game Boy saves (see gb_save.h). Adapted from Cardputer Game Station
// (src/gbc/gbc_save.cpp, MIT), without the separate task: the emulator loop calls
// gbSaveTick() once per frame, and the write (8 to 32 KB) happens right there.
// During the write the display task is held (gbDisplayLock): card and display
// share the same SPI.
//
// SPDX-License-Identifier: MIT
// Copyright (c) geo-tp and contributors

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gb_save.h"
#include "gb_display.h"
#include "../sd_roms.h"

extern "C" {
#include <gnuboy.h>
}

static constexpr char SAVE_DIR[] = "/sd/gb_saves";
static constexpr uint32_t DEBOUNCE_MS = 500;
static constexpr uint32_t MIN_GAP_MS = 15000;

static char s_path[96] = {0};
static char s_tmp[100] = {0};
static bool s_enabled = false;
static uint32_t s_dirtySinceMs = 0;
static uint32_t s_nextAllowedMs = 0;

bool gbSaveInit(const char *romName) {
    s_enabled = false;
    if (!sdMounted()) {
        log_printf("GB save: no card, saves disabled\n");
        return false;
    }
    mkdir(SAVE_DIR, 0777);  // error ignored: either it already exists or the write will fail and say so
    char base[64];
    snprintf(base, sizeof(base), "%s", romName ? romName : "gb");
    char *dot = strrchr(base, '.');
    if (dot && dot != base) *dot = '\0';
    snprintf(s_path, sizeof(s_path), "%s/%s.sav", SAVE_DIR, base);
    snprintf(s_tmp, sizeof(s_tmp), "%s.tmp", s_path);
    s_enabled = true;
    log_printf("GB save: %s\n", s_path);
    return true;
}

void gbSaveLoad() {
    if (!s_enabled) return;
    struct stat st;
    // Previous write interrupted between unlink and rename: the .tmp is the valid save.
    if (stat(s_path, &st) != 0 && stat(s_tmp, &st) == 0) rename(s_tmp, s_path);
    const bool exists = stat(s_path, &st) == 0;
    // Called even without a file: gnuboy returns -1 before touching the disk when the
    // cartridge has neither battery nor RAM (that is how we find out), and -2 when only the file is missing.
    gbDisplayLock();
    const int rc = gnuboy_load_sram(s_path);
    gbDisplayUnlock();
    if (rc == -1 && !exists) {
        s_enabled = false;
        log_printf("GB save: cartridge has no battery\n");
    } else if (!exists) {
        log_printf("GB save: no previous save\n");
    } else if (rc != 0) {
        log_printf("GB save: could not read %s (rc=%d)\n", s_path, rc);
    } else {
        log_printf("GB save: loaded %ld bytes\n", (long)st.st_size);
    }
}

static bool saveNow() {
    // Write the whole thing (quick_save = false): gnuboy opens the file with "wb", which
    // truncates it, so writing only the dirty banks would leave the others zeroed.
    // Temporary file + rename: a power loss halfway does not ruin the old save.
    const int rc = gnuboy_save_sram(s_tmp, false);
    if (rc == -1) {
        // RAM without battery (MBC1+RAM etc.): gnuboy refuses before opening the file.
        s_enabled = false;
        log_printf("GB save: cartridge has no battery, saves disabled\n");
        return false;
    }
    if (rc != 0) {
        unlink(s_tmp);
        log_printf("GB save: failed rc=%d\n", rc);
        return false;
    }
    unlink(s_path);
    if (rename(s_tmp, s_path) != 0) {
        // The .tmp stays: it is the only valid copy, and gbSaveLoad recovers it on the next boot.
        log_printf("GB save: rename failed, save left in %s\n", s_tmp);
        return false;
    }
    log_printf("GB save: wrote %s\n", s_path);
    return true;
}

void gbSaveFlush() {
    if (!s_enabled || !gnuboy_sram_dirty()) return;
    gbDisplayLock();
    saveNow();
    gbDisplayUnlock();
}

void gbSaveTick() {
    if (!s_enabled) return;
    if (!gnuboy_sram_dirty()) {
        s_dirtySinceMs = 0;
        return;
    }
    const uint32_t now = millis();
    if (s_dirtySinceMs == 0) s_dirtySinceMs = now ? now : 1;
    if (now - s_dirtySinceMs < DEBOUNCE_MS) return;
    if ((int32_t)(now - s_nextAllowedMs) < 0) return;
    s_nextAllowedMs = now + MIN_GAP_MS;  // applies even after a failure: do not hammer the card
    s_dirtySinceMs = 0;
    gbDisplayLock();
    saveNow();
    gbDisplayUnlock();
}
