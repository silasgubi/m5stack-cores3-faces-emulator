// Nofrendo sound on the CoreS3 speaker (M5.Speaker, mono 22050 Hz).
// Adapted from Cardputer Game Station (src/nes/nes_sound.cpp, MIT): M5.Speaker instead
// of M5Cardputer.Speaker, and a clock-driven sample count.
//
// SPDX-License-Identifier: MIT
// Copyright (c) geo-tp and contributors

#include <M5Unified.h>

extern "C" {
#include <osd.h>
}

static constexpr int kSampleRate = 22050;
static constexpr int kNesHz = 60;
static constexpr int kChunk = (kSampleRate + kNesHz / 2) / kNesHz;  // 368 samples per frame
static constexpr int kChannel = 0;
static constexpr uint8_t kVolume = 96;  // 0..255

static void (*s_audio_cb)(void *buffer, int length) = nullptr;

// Three buffers in rotation: M5.Speaker keeps at most 2 blocks in the channel queue,
// so the third one is always free to be filled.
static constexpr int kMaxChunk = kChunk * 3;
static int16_t *s_buf[3] = {nullptr, nullptr, nullptr};
static uint8_t s_next = 0;
static int64_t s_lastUs = 0;
static int32_t s_owed = 0;  // samples the clock already asked for that were not generated yet

static void queueSamples(int n) {
    int16_t *b = s_buf[s_next];
    s_audio_cb((void *)b, n);
    M5.Speaker.playRaw(b, (size_t)n, kSampleRate, false, 1, kChannel, false);
    s_next = (s_next + 1) % 3;
}

extern "C" {

int osd_init_sound() {
    for (auto &b : s_buf) b = (int16_t *)heap_caps_calloc(kMaxChunk, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // A beep in the settings menu (tone) may have started the speaker with the default
    // config, whose unpinned task can land on core 1 and starve: restart it with ours.
    if (M5.Speaker.isRunning()) M5.Speaker.end();
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = kSampleRate;
    cfg.stereo = false;
    cfg.dma_buf_len = 512;
    cfg.dma_buf_count = 8;
    cfg.task_priority = 4;
    cfg.task_pinned_core = 0;  // the emulator has core 1 to itself
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(kVolume);
    M5.Speaker.stop(kChannel);
    return 0;
}

void osd_stopsound() {
    s_audio_cb = nullptr;
    M5.Speaker.stop(kChannel);
}

void osd_setsound(void (*playfunc)(void *buffer, int length)) { s_audio_cb = playfunc; }

void osd_getsoundinfo(sndinfo_t *info) {
    info->sample_rate = kSampleRate;
    info->bps = 16;
}

// Called once per drawn frame (cgs/nes_osd.c, custom_blit). The emulator delivers
// 55 to 59 frames per second; with a fixed block per frame ~5% of the samples were
// missing and the queue ran dry (crackling sound). So the amount generated follows
// the clock: time since the last call times the sample rate.
void do_audio_frame() {
    if (!s_audio_cb) return;
    const int64_t now = esp_timer_get_time();
    if (s_lastUs == 0) {
        s_lastUs = now;
        queueSamples(kChunk);  // initial headroom of one frame
        queueSamples(kChunk);
        return;
    }
    // The division remainder is kept: dropping it lost ~0.5 sample per call
    // (~28 per second), and the 1 to 2 block headroom ran dry every ~18 s (a click).
    static int64_t s_remUs = 0;
    const int64_t acc = (now - s_lastUs) * kSampleRate + s_remUs;
    s_owed += (int32_t)(acc / 1000000);
    s_remUs = acc % 1000000;
    s_lastUs = now;
    if (s_owed > kMaxChunk * 2) {  // long pause: do not build up a delay
        s_owed = kMaxChunk * 2;
        s_remUs = 0;
    }

    const size_t queued = M5.Speaker.isPlaying(kChannel);
    if (queued == 0) s_owed += kChunk;  // queue ran dry: add back a full block of headroom
    if (queued < 2 && s_owed > 0) {
        const int n = s_owed > kMaxChunk ? kMaxChunk : s_owed;
        queueSamples(n);
        s_owed -= n;
    }

    // Measurement: calls per second and how many found the queue empty.
    static uint32_t s_calls = 0, s_empty = 0, s_t0 = 0;
    s_calls++;
    if (queued == 0) s_empty++;
    const uint32_t ms = millis();
    if (ms - s_t0 >= 1000) {
        log_printf("audio=%lu/s empty=%lu\n",(unsigned long)s_calls, (unsigned long)s_empty);
        s_calls = s_empty = 0;
        s_t0 = ms;
    }
}

}  // extern "C"
