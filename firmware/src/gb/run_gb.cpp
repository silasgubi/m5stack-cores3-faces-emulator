// Game Boy / Game Boy Color with gnuboy (see run_gb.h). Loop structure adapted
// from Cardputer Game Station (src/gbc/run_gbc.cpp, MIT): display, sound and saves in
// the gb_*.cpp files next to it. Runs on core 1 (setup); only the display has a task (core 0).
//
// SPDX-License-Identifier: MIT
// Copyright (c) geo-tp and contributors

#include <M5Unified.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>
#include <string.h>
#include <strings.h>
#include "run_gb.h"
#include "gb_display.h"
#include "gb_sound.h"
#include "gb_save.h"
#include "../faces_pad.h"
#include "../led_effects.h"
#include "../menu.h"
#include "../pause_menu.h"

extern "C" {
#include <gnuboy.h>
void osd_frame_hook(void);
}

// Real Game Boy frame: 70224 cycles at 4.194304 MHz = 16742.7 us (59.73 Hz). With
// this period gnuboy produces ~22050 samples/s, which is what the speaker consumes; the
// fine adjustment is left to gbSoundPaceAdjustUs.
static constexpr int64_t FRAME_US = 16743;
static constexpr int AUDIO_BUF_SAMPLES = 2048;

static uint16_t *s_fb = nullptr;
static int16_t *s_audioBuf = nullptr;

// Called by gnuboy at vblank of the drawn frames: the image is complete here.
static void videoCallback(void *) { gbDisplaySubmit(s_fb); }

static void audioCallback(void *buffer, size_t length) { gbSoundSubmit((const int16_t *)buffer, length); }

// Pause menu glue (pause_menu.h). The emulator only runs from the loop below, which is
// waiting inside the frame hook while the menu is open, so begin() only has to take the
// screen from the display task; the loop realigns its clock after a long frame.
static bool pauseSave(const char *path) { return gnuboy_save_state(path) == 0; }
static bool pauseLoad(const char *path) { return gnuboy_load_state(path) == 0; }
static void pauseBegin() {
    while (gbDisplayBusy()) delay(1);
    gbDisplayLock();
}
static void pauseEnd() { gbDisplayUnlock(); }

// Waits until instant t (esp_timer): sleeps in FreeRTOS while there is a lot left and
// finishes with a busy wait, so the frame starts on time (the 1 ms tick is coarse).
static void sleepUntilUs(int64_t t) {
    for (;;) {
        const int64_t remain = t - esp_timer_get_time();
        if (remain <= 0) return;
        if (remain > 2000) {
            vTaskDelay(pdMS_TO_TICKS((remain - 1000) / 1000));
        } else {
            ets_delay_us((uint32_t)remain);
            return;
        }
    }
}

static int padBits() {
    const uint8_t p = facesPadRead();
    int pad = 0;
    if (p & FACES_UP) pad |= GB_PAD_UP;
    if (p & FACES_DOWN) pad |= GB_PAD_DOWN;
    if (p & FACES_LEFT) pad |= GB_PAD_LEFT;
    if (p & FACES_RIGHT) pad |= GB_PAD_RIGHT;
    if (p & FACES_A) pad |= GB_PAD_A;
    if (p & FACES_B) pad |= GB_PAD_B;
    if (p & FACES_SELECT) pad |= GB_PAD_SELECT;
    if (p & FACES_START) pad |= GB_PAD_START;
    return pad;
}

static bool isGbc(const char *name) {
    const char *dot = name ? strrchr(name, '.') : nullptr;
    return dot && strcasecmp(dot, ".gbc") == 0;
}

void run_gb(const uint8_t *rom, size_t romLen, const char *romName) {
    if (!rom || romLen < 0x150) {
        log_printf("GB: invalid ROM (%u bytes)\n", (unsigned)romLen);
        return;
    }
    // Sound first: the first osd_frame_hook reads the volume from NVS and applies it to the speaker.
    if (!gbSoundInit() || !gbDisplayInit()) {
        log_printf("GB: sound or display init failed\n");
        return;
    }
    if (!s_fb) s_fb = (uint16_t *)heap_caps_calloc(GB_FB_W * GB_FB_H, sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_audioBuf) s_audioBuf = (int16_t *)heap_caps_calloc(AUDIO_BUF_SAMPLES, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_fb || !s_audioBuf) {
        log_printf("GB: out of memory for framebuffer/sound\n");
        return;
    }

    // 565_BE: palette with bytes already swapped, the same order nes_osd.c writes and that
    // pushPixelsDMA sends to the panel without conversion.
    int rc = gnuboy_init(GB_SAMPLE_RATE, GB_AUDIO_MONO_S16, GB_PIXEL_565_BE, videoCallback, audioCallback);
    if (rc < 0) {
        log_printf("GB: gnuboy_init failed (%d)\n", rc);
        return;
    }
    gnuboy_set_framebuffer(s_fb);
    gnuboy_set_soundbuffer(s_audioBuf, AUDIO_BUF_SAMPLES);
    rc = gnuboy_load_rom(rom, romLen);
    if (rc < 0) {
        log_printf("GB: gnuboy_load_rom failed (%d)\n", rc);
        return;
    }
    gnuboy_reset(true);
    gnuboy_set_pad(0);
    // Classic Game Boy game: GB_PALETTE_CGB uses the colors the Game Boy Color would give
    // that game (BIOS table, when the game is known) and, otherwise, a soft green.
    // A .gbc game has its own colors and gnuboy ignores the palette.
    if (!isGbc(romName)) gnuboy_set_palette(GB_PALETTE_CGB);
    log_printf("GB: %s, hw=%d, %u bytes\n", romName, gnuboy_get_hwtype(), (unsigned)romLen);

    gbSaveInit(romName);
    gbSaveLoad();
    menuOnQuit(gbSaveFlush);  // "Quit to menu" restarts: write the SRAM first
    pauseSetHandlers({pauseSave, pauseLoad, pauseBegin, pauseEnd});
    ledEffectsSetSource565(s_fb, GB_FB_W, GB_FB_H, GB_FB_W);

    uint32_t frame = 0;
    bool late = false;
    int64_t next = esp_timer_get_time();
    int64_t emuSum = 0, emuMax = 0;
    uint32_t emuN = 0, logAt = millis() + 10000;

    for (;;) {
        const int64_t t0 = esp_timer_get_time();
        // Draw when the display is free and the previous frame was not late. Sending
        // 266x240 takes ~26 ms, so two consecutive frames never go out (~30 Hz); without
        // requiring an even frame, a busy display at frame N does not push the draw to N+4.
        const bool draw = !late && !gbDisplayBusy();
        gnuboy_run(draw);
        gnuboy_set_pad(padBits());
        gbSoundFrame();
        osd_frame_hook();
        gbSaveTick();
        frame++;

        const int64_t dt = esp_timer_get_time() - t0;
        emuSum += dt;
        emuN++;
        if (dt > emuMax) emuMax = dt;
        if ((int32_t)(millis() - logAt) >= 0) {
            logAt = millis() + 10000;
            log_printf("emu_ms avg=%.2f max=%.2f\n", emuSum / 1000.0f / emuN, emuMax / 1000.0f);
            emuSum = emuMax = 0;
            emuN = 0;
        }

        next += FRAME_US + gbSoundPaceAdjustUs();
        const int64_t lateness = esp_timer_get_time() - next;
        if (lateness > 0) {
            late = true;
            if (lateness > FRAME_US) next = esp_timer_get_time();  // large delay: realign, do not accumulate
        } else {
            late = false;
            sleepUntilUs(next);
        }
    }
}
