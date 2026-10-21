// Driver for the 10 SK6812 through the arduino-esp32 2.0.17 RMT (no external library).
// Why not the usual libraries:
// - M5.Led / LED_Strip_Class from M5Unified: empty on IDF 4.4.
// - neopixelWrite: only 1 LED, and it blocks.
// - Adafruit NeoPixel: installs and uninstalls the driver on every show(), and blocks.
// SK6812: GRB order, most significant bit first, 800 kHz.
// 100 ns tick: bit 0 = 0.3 us high + 0.9 us low; bit 1 = 0.6 + 0.6.

#include <Arduino.h>
#include <string.h>
#include "esp32-hal-rmt.h"
#include "leds.h"

static constexpr int LED_PIN = 5;
static constexpr uint8_t BRIGHTNESS = 64;  // 0..255; the datasheet gives 370 mA with all white at full
static constexpr int BITS = LED_COUNT * 24;

static rmt_obj_t *s_rmt = nullptr;
static rmt_data_t s_items[BITS];
static uint8_t s_grb[LED_COUNT * 3];
static bool s_dirty = false;

bool ledsInit() {
    // 4 memory blocks (192 items): the 240-bit frame needs 1 refill by interrupt.
    s_rmt = rmtInit(LED_PIN, RMT_TX_MODE, RMT_MEM_256);
    if (!s_rmt) {
        log_printf("LED: rmtInit failed\n");
        return false;
    }
    rmtSetTick(s_rmt, 100);
    // GPIO5 floats until here, and the LEDs may light up with garbage: the next
    // ledsShow sends everything off (or whatever color is set before it).
    ledsClear();
    s_dirty = true;
    log_printf("LED: rmt ok on GPIO%d\n", LED_PIN);
    return true;
}

void ledsSet(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= LED_COUNT) return;
    uint8_t *p = &s_grb[index * 3];
    const uint8_t gs = g * BRIGHTNESS / 255, rs = r * BRIGHTNESS / 255, bs = b * BRIGHTNESS / 255;
    if (p[0] == gs && p[1] == rs && p[2] == bs) return;
    p[0] = gs;
    p[1] = rs;
    p[2] = bs;
    s_dirty = true;
}

void ledsClear() {
    for (int i = 0; i < LED_COUNT; ++i) ledsSet(i, 0, 0, 0);
}

void ledsShow() {
    if (!s_rmt || !s_dirty) return;
    // Two back-to-back writes lock up the IDF 4.4 RMT driver (the second waits on a
    // semaphore the first never gives back). A frame takes ~0.3 ms; 5 ms apart is plenty.
    static uint32_t s_lastUs = 0;
    const uint32_t now = micros();
    if (s_lastUs && now - s_lastUs < 5000) return;  // stays dirty, goes out next frame
    s_lastUs = now;
    s_dirty = false;
    int n = 0;
    for (int i = 0; i < LED_COUNT * 3; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool one = s_grb[i] & (1 << bit);
            rmt_data_t &it = s_items[n++];
            it.level0 = 1;
            it.duration0 = one ? 6 : 3;
            it.level1 = 0;
            it.duration1 = one ? 6 : 9;
        }
    }
    rmtWrite(s_rmt, s_items, BITS);
}
