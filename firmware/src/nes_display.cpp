// Display: 256x240 NES frame at 1:1, centered on the CoreS3 320x240 screen.
// Runs in displayTask (core 0, see cgs/nes_osd.c). Converts the 8-bit frame through
// the palette in bands of BAND_ROWS lines and sends them by DMA, alternating two
// buffers: while one band goes out over SPI, the next one is being converted.

extern "C" {
#include <nes/nes.h>
}

#include <M5Unified.h>
#include "hud.h"

extern uint16_t *myPalette;  // already RGB565 with swapped bytes (cgs/nes_osd.c)

static constexpr int W = NES_SCREEN_WIDTH;   // 256
static constexpr int H = NES_SCREEN_HEIGHT;  // 240
static constexpr int BAND_ROWS = 16;         // 240 / 16 = 15 bands of 8 KB
static int s_x0 = 0;
static uint16_t *s_band[2] = {nullptr, nullptr};

extern "C" {

void display_init() {
    s_x0 = (M5.Display.width() - W) / 2;
    for (auto &b : s_band) {
        if (!b) b = (uint16_t *)heap_caps_malloc(W * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    M5.Display.fillScreen(TFT_BLACK);
}

void display_write_frame(const uint8_t *data[]) {
    if (!myPalette || !s_band[0] || !s_band[1]) return;
    const uint32_t frameStart = micros();
    M5.Display.startWrite();
    M5.Display.setWindow(s_x0, 0, s_x0 + W - 1, H - 1);
    int flip = 0;
    for (int y0 = 0; y0 < H; y0 += BAND_ROWS) {
        uint16_t *dst = s_band[flip];
        for (int y = y0; y < y0 + BAND_ROWS; ++y) {
            const uint8_t *src = data[y];
            for (int x = 0; x < W; ++x) *dst++ = myPalette[src[x]];
        }
        // pushPixelsDMA waits for the previous band to finish before starting this one,
        // so rewriting the other buffer on the next pass is safe.
        M5.Display.pushPixelsDMA(s_band[flip], W * BAND_ROWS);
        flip ^= 1;
    }
    M5.Display.waitDMA();
    M5.Display.endWrite();
    hudDraw();

    // Measurement: frames drawn per second and average cost of each.
    static uint32_t s_frames = 0, s_busyUs = 0, s_t0 = 0;
    const uint32_t now = micros();
    s_frames++;
    s_busyUs += now - frameStart;
    if (now - s_t0 >= 1000000) {
        log_printf("draw=%.1f/s draw_ms=%.1f\n", s_frames * 1e6f / (now - s_t0), s_busyUs / 1000.0f / s_frames);
        s_frames = s_busyUs = 0;
        s_t0 = now;
    }
}

void display_clear() { M5.Display.fillScreen(TFT_BLACK); }

}  // extern "C"
