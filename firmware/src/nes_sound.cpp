// Nofrendo sound on the CoreS3 speaker (M5.Speaker, mono 22050 Hz).
// Adapted from Cardputer Game Station (src/nes/nes_sound.cpp, MIT): M5.Speaker instead
// of M5Cardputer.Speaker, and a ring buffer between the APU and the speaker.
//
// The APU applies register writes at once and only renders them into samples when it is
// asked for a block, so it must be asked every frame: skipping a frame (because the
// speaker queue was full) merged two frames of writes and one-frame notes, the drum hits
// of fast music, were lost ("groggy" music). Now every frame renders one frame of samples
// into a ring, and the ring feeds the speaker in variable blocks, as gb_sound.cpp does.
// The ring level corrects the drift between esp_timer (the 60 Hz frame clock) and the
// I2S crystal by +-1 sample per frame.
//
// Blocks are at least two frames long. The M5.Speaker task mixes one DMA buffer per pass
// and, when it finds no published block, it inserts a buffer of silence (a pop). With
// one-frame blocks and the two-slot queue, a pass of 512 samples sometimes needed a
// second block that had not been published yet. The ring also starts with a stock of
// silence, so the DMA fills up and the speaker task is paced by it, not by the emulator.
//
// SPDX-License-Identifier: MIT
// Copyright (c) geo-tp and contributors

#include <M5Unified.h>
#include <string.h>

extern "C" {
#include <osd.h>
}

static constexpr int kSampleRate = 22050;
static constexpr int kNesHz = 60;
static constexpr int kChunk = (kSampleRate + kNesHz / 2) / kNesHz;  // 368 samples per frame
static constexpr int kBlock = kChunk * 2;     // minimum block: more than one speaker pass (see above)
static constexpr int kMaxChunk = kChunk * 3;  // largest block (buffer size)
static constexpr int kPrime = kChunk * 10;    // initial stock of silence: DMA + two slots + ring
static constexpr int kRing = 4096;            // power of 2: masked index
static constexpr int kRingMask = kRing - 1;
static constexpr int kTargetLevel = kChunk * 2;  // ring level after draining: one spare block
static constexpr int kChannel = 0;
static constexpr uint8_t kVolume = 96;  // 0..255, frame_hook overrides it with the saved volume

static void (*s_audio_cb)(void *buffer, int length) = nullptr;

static int16_t *s_ring = nullptr;
static uint32_t s_wr = 0, s_rd = 0;  // grow without bound; the difference is what is in the ring
static int16_t *s_gen = nullptr;     // one frame of samples from the APU, before the ring
// Three buffers in rotation: M5.Speaker keeps at most 2 blocks in the channel queue,
// so the third one is always free to be filled.
static int16_t *s_buf[3] = {nullptr, nullptr, nullptr};
static uint8_t s_next = 0;
static int s_adjust = 0;  // samples added to (or taken from) each frame to hold the ring level
static uint32_t s_calls = 0, s_empty = 0, s_overrun = 0, s_levelSum = 0, s_t0 = 0;

static inline int ringCount() { return (int)(s_wr - s_rd); }

static void playChunk(int n) {
    int16_t *b = s_buf[s_next];
    const int start = s_rd & kRingMask;
    const int first = kRing - start < n ? kRing - start : n;
    memcpy(b, s_ring + start, first * sizeof(int16_t));
    if (first < n) memcpy(b + first, s_ring, (n - first) * sizeof(int16_t));
    s_rd += n;
    M5.Speaker.playRaw(b, (size_t)n, kSampleRate, false, 1, kChannel, false);
    s_next = (s_next + 1) % 3;
}

extern "C" {

int osd_init_sound() {
    if (!s_ring) s_ring = (int16_t *)heap_caps_calloc(kRing, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_gen) s_gen = (int16_t *)heap_caps_calloc(kChunk + 8, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (auto &b : s_buf) {
        if (!b) b = (int16_t *)heap_caps_calloc(kMaxChunk, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!b) return -1;
    }
    if (!s_ring || !s_gen) return -1;
    s_wr = s_rd = 0;

    // A beep in the settings menu (tone) may have started the speaker with the default
    // config, whose unpinned task can land on core 1 and starve: restart it with ours.
    if (M5.Speaker.isRunning()) M5.Speaker.end();
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = kSampleRate;
    cfg.stereo = false;
    cfg.dma_buf_len = 256;   // smaller DMA: the stock that fills it adds less delay
    cfg.dma_buf_count = 4;
    cfg.task_priority = 4;
    cfg.task_pinned_core = 0;  // the emulator has core 1 to itself
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(kVolume);
    M5.Speaker.stop(kChannel);
    s_wr = kPrime;  // the ring is zeroed: kPrime samples of silence go out first
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

// Called once per frame (cgs/nes_osd.c, custom_blit), on the 60 Hz frame clock.
void do_audio_frame() {
    if (!s_audio_cb || !s_ring) return;

    // One frame of samples: 367 and 368 alternating (22050 / 60 = 367.5), plus the drift
    // correction. The APU's length counters assume ~367 per frame (apu_build_luts).
    static uint32_t s_frame = 0;
    const int n = kChunk - 1 + (int)(s_frame++ & 1) + s_adjust;
    s_audio_cb((void *)s_gen, n);
    for (int i = 0; i < n; ++i) {
        if (ringCount() >= kRing) {  // overflow: drop the oldest sample
            s_rd++;
            s_overrun++;
        }
        s_ring[s_wr++ & kRingMask] = s_gen[i];
    }

    size_t queued = M5.Speaker.isPlaying(kChannel);
    s_calls++;
    if (queued == 0) s_empty++;
    // Keep two blocks queued whenever there is anything to send. The NES produces exactly
    // what the speaker consumes, so waiting for a full frame before the second block (the
    // Game Boy rule) left a single block in flight and the queue ran dry now and then.
    while (queued < 2 && ringCount() > 0) {
        int m = ringCount() > kMaxChunk ? kMaxChunk : ringCount();
        if (queued == 1 && m < kBlock) break;  // blocks of two frames or more, unless the speaker is idle
        playChunk(m);
        queued++;
    }
    s_levelSum += ringCount();

    // Once per second: log and proportional drift correction from the average ring level.
    const uint32_t ms = millis();
    if (ms - s_t0 >= 1000) {
        const int level = (int)(s_levelSum / (s_calls ? s_calls : 1));
        int adj = (kTargetLevel - level) / 64;  // samples per frame; ring too full: fewer
        if (adj > 1) adj = 1;
        if (adj < -1) adj = -1;
        s_adjust = adj;
        log_printf("audio=%lu/s empty=%lu ring=%d adjust=%d dropped=%lu\n", (unsigned long)s_calls,
                   (unsigned long)s_empty, level, s_adjust, (unsigned long)s_overrun);
        s_calls = s_empty = s_overrun = s_levelSum = 0;
        s_t0 = ms;
    }
}

}  // extern "C"
