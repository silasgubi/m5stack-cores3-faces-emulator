// Faces Gamepad3 reader (I2C 0x08 on the CoreS3 internal bus, pins 12/11).
// Logic ported from kexi/cluade-famicom-emu-stackchan (m5stack/src/faces_input.cpp, MIT),
// which in turn ports it from the official M5Faces library (MIT).
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GOROman (kexi/cluade-famicom-emu-stackchan)
// Copyright (c) M5Stack Technology CO LTD (M5Faces register map and polarity)

#include <M5Unified.h>
#include "faces_pad.h"

static constexpr uint8_t ADDR = 0x08;
static constexpr uint8_t REG_KEY = 0x00;       // active-low bits
static constexpr uint8_t REG_MODEL_ID = 0xD0;
static constexpr uint8_t MODEL_GAMEPAD3 = 0x03;
static constexpr uint32_t FREQ = 100000;

static bool s_present = false;
static uint8_t s_lastByte = 0xFF;
static uint8_t s_pressed = 0;  // bit 0..7 = UP DOWN LEFT RIGHT A B SELECT START

bool facesPadInit() {
    uint8_t model = 0;
    if (!M5.In_I2C.readRegister(ADDR, REG_MODEL_ID, &model, 1, FREQ) || model != MODEL_GAMEPAD3) {
        log_printf("FACES: gamepad not found (model=0x%02X)\n", model);
        return false;
    }
    // KEY returns the last byte sent until the next button change; right after
    // reading MODEL_ID it returns 0x03, which would become "6 buttons pressed".
    // Storing that leftover as the last byte makes the next read ignore it.
    uint8_t stale = 0;
    if (M5.In_I2C.readRegister(ADDR, REG_KEY, &stale, 1, FREQ)) s_lastByte = stale;
    s_present = true;
    log_printf("FACES: gamepad ok\n");
    return true;
}

uint8_t facesPadRead() {
    if (!s_present) return 0;
    uint8_t raw = 0;
    if (!M5.In_I2C.readRegister(ADDR, REG_KEY, &raw, 1, FREQ)) return s_pressed;
    // Same byte = nothing new. 0x00 is a leftover in the panel's buffer, not "all pressed".
    if (raw == s_lastByte || raw == 0x00) return s_pressed;
    s_lastByte = raw;
    s_pressed = (uint8_t)~raw;
    return s_pressed;
}
