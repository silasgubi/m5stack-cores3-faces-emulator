// Game Boy display (see gb_display.h). Nearest-neighbor 5:3 scaling to the full screen
// height: 160x144 becomes 266x240 (the aspect ratio kept), centered with 27 px strips on
// each side for the HUD (hud.cpp: battery and level bars fit in them). Every 3 source lines
// become 5 destination lines; columns use a table, src = dst * 3 / 5. 266 and not 267: the
// last column would be a 1-pixel sliver of the same source column.

#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include "gb_display.h"
#include "../hud.h"

static constexpr int DST_W = GB_FB_W * 5 / 3;  // 266
static constexpr int DST_H = GB_FB_H * 5 / 3;  // 240
static constexpr int X0 = (320 - DST_W) / 2;   // 27
static constexpr int Y0 = (240 - DST_H) / 2;   // 0
// 20 destination lines = 12 source lines: bands fall on multiples of the 5-line pattern.
static constexpr int BAND_ROWS = 20;           // 240 / 20 = 12 bands of 10.4 KB
static_assert(DST_H % BAND_ROWS == 0, "band must divide the height");
static_assert(BAND_ROWS % 5 == 0, "band must follow the 5-line pattern");
static_assert((DST_W - 1) * 3 / 5 < GB_FB_W, "the column table must stay inside the source line");

static QueueHandle_t s_queue = nullptr;
static SemaphoreHandle_t s_spi = nullptr;
static TaskHandle_t s_task = nullptr;
static uint16_t *s_band[2] = {nullptr, nullptr};
static volatile bool s_busy = false;
static uint8_t s_srcX[DST_W];  // source column of each destination column

static inline void scaleRow(const uint16_t *src, uint16_t *dst) {
    for (int x = 0; x < DST_W; ++x) dst[x] = src[s_srcX[x]];
}

static void drawFrame(const uint16_t *fb) {
    const uint32_t frameStart = micros();
    M5.Display.startWrite();
    M5.Display.setWindow(X0, Y0, X0 + DST_W - 1, Y0 + DST_H - 1);
    int flip = 0;
    int prevSrc = -1;
    for (int y0 = 0; y0 < DST_H; y0 += BAND_ROWS) {
        uint16_t *dst = s_band[flip];
        for (int y = y0; y < y0 + BAND_ROWS; ++y, dst += DST_W) {
            const int sy = y * 3 / 5;
            if (sy == prevSrc && y != y0) {
                memcpy(dst, dst - DST_W, DST_W * sizeof(uint16_t));  // repeated line: just copy
            } else {
                scaleRow(fb + sy * GB_FB_W, dst);
                prevSrc = sy;
            }
        }
        // pushPixelsDMA waits for the previous band to finish before starting this one,
        // so rewriting the other buffer on the next pass is safe.
        M5.Display.pushPixelsDMA(s_band[flip], DST_W * BAND_ROWS);
        flip ^= 1;
    }
    M5.Display.waitDMA();
    M5.Display.endWrite();
    hudDraw();

    // Measurement: frames drawn per second and the cost of each.
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

static void displayTask(void *) {
    const uint16_t *fb = nullptr;
    for (;;) {
        if (xQueueReceive(s_queue, &fb, portMAX_DELAY) != pdTRUE || !fb) continue;
        xSemaphoreTake(s_spi, portMAX_DELAY);
        drawFrame(fb);
        xSemaphoreGive(s_spi);
        s_busy = false;
    }
}

bool gbDisplayInit() {
    for (int x = 0; x < DST_W; ++x) s_srcX[x] = (uint8_t)(x * 3 / 5);
    for (auto &b : s_band) {
        if (!b) b = (uint16_t *)heap_caps_malloc(DST_W * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!b) {
            log_printf("GB display: out of DMA memory\n");
            return false;
        }
    }
    if (!s_queue) s_queue = xQueueCreate(1, sizeof(const uint16_t *));
    if (!s_spi) s_spi = xSemaphoreCreateMutex();
    if (!s_queue || !s_spi) return false;
    M5.Display.fillScreen(TFT_BLACK);
    if (!s_task && xTaskCreatePinnedToCore(displayTask, "gbDisplay", 4096, nullptr, 5, &s_task, 0) != pdPASS) {
        log_printf("GB display: task creation failed\n");
        s_task = nullptr;
        return false;
    }
    return true;
}

bool gbDisplaySubmit(const uint16_t *fb) {
    if (!s_queue || !fb || s_busy) return false;
    s_busy = true;
    if (xQueueSend(s_queue, &fb, 0) != pdTRUE) {
        s_busy = false;
        return false;
    }
    return true;
}

bool gbDisplayBusy() { return s_busy; }

void gbDisplayLock() {
    if (s_spi) xSemaphoreTake(s_spi, portMAX_DELAY);
}

void gbDisplayUnlock() {
    if (s_spi) xSemaphoreGive(s_spi);
}
