// Game Boy sound (see gb_sound.h). Speaker configuration identical to the NES one
// (nes_sound.cpp). Difference: the NES generates samples by the clock; gnuboy generates a
// fixed amount per emulated frame (35112 cycles / 95 cycles per sample = 369.6).
// So the frame pace is what gets adjusted: if the ring fills up, the emulator slows down
// a little; if it drains, it speeds up (gbSoundPaceAdjustUs). Without this, the drift between
// the I2S crystal and esp_timer would make the ring overflow or run dry every few minutes.

#include <M5Unified.h>
#include <string.h>
#include "gb_sound.h"

static constexpr int kChannel = 0;
static constexpr uint8_t kVolume = 96;  // frame_hook overrides it with the volume saved in NVS
static constexpr int kChunk = (GB_SAMPLE_RATE + 30) / 60;  // 368 samples, like the NES
// Variable-size block, up to 3 frames: the M5.Speaker task mixes 512 samples per pass,
// and with fixed 368-sample blocks the 2-deep queue ran dry and silence crept in (same
// defect fixed on the NES). Each send drains the whole ring, up to kMaxChunk.
static constexpr int kMaxChunk = kChunk * 3;
static constexpr int kRing = 4096;                         // power of 2: masked index
static constexpr int kRingMask = kRing - 1;
static constexpr int kTargetLevel = kChunk;  // ring level after draining (~17 ms of headroom)
static constexpr int kAdjustMaxUs = 250;     // +-1.5% on the frame period, imperceptible

static int16_t *s_ring = nullptr;
static uint32_t s_wr = 0, s_rd = 0;  // grow without bound; the difference is what is in the ring
static int16_t *s_buf[3] = {nullptr, nullptr, nullptr};  // M5.Speaker holds up to 2; the 3rd is free
static uint8_t s_next = 0;
static int s_adjustUs = 0;
static uint32_t s_calls = 0, s_empty = 0, s_overrun = 0, s_levelSum = 0, s_t0 = 0;

static inline int ringCount() { return (int)(s_wr - s_rd); }

bool gbSoundInit() {
    if (!s_ring) s_ring = (int16_t *)heap_caps_calloc(kRing, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (auto &b : s_buf) {
        if (!b) b = (int16_t *)heap_caps_calloc(kMaxChunk, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!b) return false;
    }
    if (!s_ring) return false;
    s_wr = s_rd = 0;

    auto cfg = M5.Speaker.config();
    cfg.sample_rate = GB_SAMPLE_RATE;
    cfg.stereo = false;
    cfg.dma_buf_len = 512;
    cfg.dma_buf_count = 8;
    cfg.task_priority = 4;
    cfg.task_pinned_core = 0;  // the emulator has core 1 to itself
    M5.Speaker.config(cfg);
    if (!M5.Speaker.isRunning()) M5.Speaker.begin();
    M5.Speaker.setVolume(kVolume);
    M5.Speaker.stop(kChannel);
    return true;
}

void gbSoundSubmit(const int16_t *samples, size_t count) {
    if (!s_ring || !samples) return;
    for (size_t i = 0; i < count; ++i) {
        if (ringCount() >= kRing) {  // overflow: drop the oldest sample
            s_rd++;
            s_overrun++;
        }
        s_ring[s_wr++ & kRingMask] = samples[i];
    }
}

static void playChunk(int n) {
    int16_t *b = s_buf[s_next];
    const int start = s_rd & kRingMask;
    const int first = kRing - start < n ? kRing - start : n;
    memcpy(b, s_ring + start, first * sizeof(int16_t));
    if (first < n) memcpy(b + first, s_ring, (n - first) * sizeof(int16_t));
    s_rd += n;
    M5.Speaker.playRaw(b, (size_t)n, GB_SAMPLE_RATE, false, 1, kChannel, false);
    s_next = (s_next + 1) % 3;
}

void gbSoundFrame() {
    if (!s_ring) return;
    size_t queued = M5.Speaker.isPlaying(kChannel);
    s_calls++;
    if (queued == 0) s_empty++;
    while (queued < 2 && ringCount() > 0) {
        // With the queue half full, only send if there is at least one block; when empty,
        // send whatever there is, so the speaker does not sit in silence waiting.
        if (queued == 1 && ringCount() < kChunk) break;
        const int n = ringCount() > kMaxChunk ? kMaxChunk : ringCount();
        playChunk(n);
        queued++;
    }
    s_levelSum += ringCount();

    // Once per second: log and proportional pace correction from the average ring level.
    const uint32_t ms = millis();
    if (ms - s_t0 >= 1000) {
        const int level = (int)(s_levelSum / (s_calls ? s_calls : 1));
        int adj = (level - kTargetLevel) / 2;  // us per frame; half of the error each second
        if (adj > kAdjustMaxUs) adj = kAdjustMaxUs;
        if (adj < -kAdjustMaxUs) adj = -kAdjustMaxUs;
        s_adjustUs = adj;  // ring above target: producing too much, so longer period (slower emulator)
        log_printf("audio=%lu/s empty=%lu ring=%d adjust=%dus dropped=%lu\n", (unsigned long)s_calls,
                   (unsigned long)s_empty, level, s_adjustUs, (unsigned long)s_overrun);
        s_calls = s_empty = s_overrun = s_levelSum = 0;
        s_t0 = ms;
    }
}

int gbSoundPaceAdjustUs() { return s_adjustUs; }
