// Bars and battery icon in the black strips beside the game. Both games leave the strips
// free: the NES image covers x 32..287, the Game Boy one x 40..279.
// - Volume bar on the right strip (x 296..311), brightness bar mirrored on the left
//   (x 8..23). Each shows for HUD_BAR_MS after a change; showing one hides the other at
//   once, so the bar on screen is always the control that "<" and ">" change.
// - Battery icon at the top of the left strip. It sits under the brightness bar, so it is
//   cleared while that bar shows and drawn again when it hides. Redrawn on each reading
//   (every 10 s, battery.cpp), which also repairs it if anything cleared the screen.

#include <M5Unified.h>
#include <atomic>
#include "hud.h"
#include "battery_icon.h"

static constexpr uint32_t HUD_BAR_MS = 1500;
static constexpr int BAR_W = 16, BAR_Y = 20, BAR_H = 200;
static constexpr int VOL_X = 296;
static constexpr int BRI_X = 320 - VOL_X - BAR_W;  // 8: mirror of the volume bar
static constexpr int BAT_X = (32 - BATTERY_ICON_V_W) / 2, BAT_Y = 6;  // centered in the NES strip (x 0..31)

static constexpr uint32_t REQ_NEW = 1u << 31;  // a request is never 0

// One slot for both bars: only one shows at a time, so the latest request wins.
static constexpr uint32_t BAR_BRIGHTNESS = 1u << 30;
static std::atomic<uint32_t> s_barReq{0};  // volume | muted << 8, or filled px | BAR_BRIGHTNESS
static std::atomic<uint32_t> s_batReq{0};  // percent | charging << 8 | present << 9

// Display task state.
static uint32_t s_volHideMs = 0, s_briHideMs = 0;  // 0: bar not shown
static uint32_t s_battery = 0;                     // last battery request, 0: none yet

void hudShowVolume(uint8_t volume, bool muted) {
    s_barReq.store(volume | (muted ? 1u << 8 : 0) | REQ_NEW, std::memory_order_relaxed);
}

void hudShowBrightness(int level, int levels) {
    if (levels < 1) return;
    level = constrain(level, 0, levels - 1);
    s_barReq.store((uint32_t)((level + 1) * BAR_H / levels) | BAR_BRIGHTNESS | REQ_NEW, std::memory_order_relaxed);
}

void hudSetBattery(int percent, bool charging, bool present) {
    s_batReq.store((uint32_t)constrain(percent, 0, 100) | (charging ? 1u << 8 : 0) | (present ? 1u << 9 : 0) |
                       REQ_NEW,
                   std::memory_order_relaxed);
}

static void drawBar(int x, int filled, uint16_t color) {
    M5.Display.fillRect(x, BAR_Y, BAR_W, BAR_H - filled, TFT_DARKGREY);
    M5.Display.fillRect(x, BAR_Y + BAR_H - filled, BAR_W, filled, color);
}

static void drawBattery() {
    if (!s_battery) return;
    drawBatteryIconVertical(BAT_X, BAT_Y, BATTERY_ICON_V_W, BATTERY_ICON_V_H, s_battery & 0xFF,
                            s_battery & (1u << 8), s_battery & (1u << 9), TFT_BLACK);
}

static void hideVolume() {
    s_volHideMs = 0;
    M5.Display.fillRect(VOL_X, BAR_Y, BAR_W, BAR_H, TFT_BLACK);
}

static void hideBrightness() {
    s_briHideMs = 0;
    M5.Display.fillRect(BRI_X, BAR_Y, BAR_W, BAR_H, TFT_BLACK);
    drawBattery();
}

// Hide time never 0, which means "not shown".
static uint32_t hideTime() { return (millis() + HUD_BAR_MS) | 1; }

void hudDraw() {
    const uint32_t bar = s_barReq.exchange(0, std::memory_order_relaxed);
    const uint32_t bat = s_batReq.exchange(0, std::memory_order_relaxed);

    if (bat) {
        s_battery = bat & ~REQ_NEW;
        if (!s_briHideMs) drawBattery();
    }
    if (bar & BAR_BRIGHTNESS) {
        if (s_volHideMs) hideVolume();
        if (!s_briHideMs) M5.Display.fillRect(BAT_X, BAT_Y, BATTERY_ICON_V_W, BATTERY_ICON_V_H, TFT_BLACK);
        drawBar(BRI_X, bar & 0xFFFF, TFT_GREEN);
        s_briHideMs = (millis() + BRIGHTNESS_ACTIVE_MS) | 1;
    } else if (bar) {
        if (s_briHideMs) hideBrightness();
        const bool muted = bar & (1u << 8);
        drawBar(VOL_X, muted ? BAR_H : (bar & 0xFF) * BAR_H / 255, muted ? TFT_RED : TFT_GREEN);
        s_volHideMs = hideTime();
    }

    const uint32_t now = millis();
    if (s_volHideMs && (int32_t)(now - s_volHideMs) >= 0) hideVolume();
    if (s_briHideMs && (int32_t)(now - s_briHideMs) >= 0) hideBrightness();
}
