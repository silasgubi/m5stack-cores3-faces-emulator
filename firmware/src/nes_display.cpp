// Display: 256x240 NES frame widened to 272x240, centered on the CoreS3 320x240 screen.
// Runs in displayTask (core 0, see cgs/nes_osd.c).
//
// Width: on a TV the NES pixel is wider than tall (8:7, the picture would be ~292 wide at
// this height). 272 (17:16, every 16th column repeated) goes halfway there and keeps 24 px
// strips at the sides for the HUD bars and the battery icon (hud.cpp).
//
// Interlaced: the even lines of one frame, the odd lines of the next. A full frame takes
// ~25 ms on the 40 MHz SPI bus (the panel's limit), so only ~39 of the 60 frames reached
// the screen, unevenly, and animations that alternate frames (the SMB3 tail spin, sprite
// flicker) stuttered. Half a frame takes ~13 ms, so every frame goes out. The cost is a
// comb effect on fast motion, since half the lines are 1/60 s older; a steady 30 Hz with
// whole frames and a 30 Hz blend of frame pairs were both tried and judged worse.
// Each line is converted through the palette into one of two DMA buffers while the other
// one goes out over SPI.

extern "C" {
#include <nes/nes.h>
}

#include <M5Unified.h>
#include "hud.h"

extern uint16_t *myPalette;  // already RGB565 with swapped bytes (cgs/nes_osd.c)

static constexpr int SRC_W = NES_SCREEN_WIDTH;   // 256
static constexpr int H = NES_SCREEN_HEIGHT;      // 240
static constexpr int DST_W = 272;                // 17:16
static_assert((DST_W - 1) * SRC_W / DST_W < SRC_W, "the column table must stay inside the source line");
static int s_x0 = 0;
static uint16_t *s_line[2] = {nullptr, nullptr};
static uint8_t s_srcX[DST_W];  // source column of each destination column

extern "C" {

void display_init() {
    s_x0 = (M5.Display.width() - DST_W) / 2;  // 24
    for (int x = 0; x < DST_W; ++x) s_srcX[x] = (uint8_t)(x * SRC_W / DST_W);
    for (auto &b : s_line) {
        if (!b) b = (uint16_t *)heap_caps_malloc(DST_W * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    M5.Display.fillScreen(TFT_BLACK);
}

// frame: line 0 of the 8-bit frame (palette indices), lines pitch bytes apart.
void display_write_frame(const uint8_t *frame, int pitch) {
    if (!myPalette || !s_line[0] || !s_line[1]) return;
    const uint32_t frameStart = micros();
    static int parity = 0;
    M5.Display.startWrite();
    int flip = 0;
    for (int y = parity; y < H; y += 2) {
        uint16_t *dst = s_line[flip];
        const uint8_t *src = frame + y * pitch;
        for (int x = 0; x < DST_W; ++x) *dst++ = myPalette[src[s_srcX[x]]];
        // setWindow waits for the DMA of the previous line, so the other buffer is free.
        M5.Display.setWindow(s_x0, y, s_x0 + DST_W - 1, y);
        M5.Display.pushPixelsDMA(s_line[flip], DST_W);
        flip ^= 1;
    }
    parity ^= 1;
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
