// Hook called once per drawn frame, on core 1, after the sound
// (cgs/nes_osd.c, custom_blit; gb/run_gb.cpp). The place for everything that is not
// emulation: touch controls, battery (battery.cpp), charge latch, settings and LEDs.
// Everything here must be cheap, because it shares the frame with the emulator
// (emulation uses ~10 of the 16.7 ms).
//
// Touch controls:
// - Strip below the screen, with the icons printed on the CoreS3: "<" and ">" step the
//   active control down and up; "o" switches it between volume (default, bar on the
//   right) and brightness (bar on the left). After ACTIVE_TIMEOUT_MS without strip
//   touches it falls back to volume, so a later "<" never dims the screen by surprise.
// - A finger held on the screen itself for LONG_TOUCH_MS toggles mute, once per touch.
//   Short taps do nothing, so brief accidental contacts are ignored; a thumb resting
//   on the screen for longer does toggle it.

#include <M5Unified.h>
#include <Preferences.h>
#include "battery.h"
#include "hud.h"
#include "leds.h"
#include "led_effects.h"
#include "faces_pad.h"
#include "menu.h"
#include "power_mode.h"
#include "settings.h"

// Touch strip below the screen (y 240..279). Measured: touches on the "<" arrive with
// x ~88-98, on the "o" ~159-194 and on the ">" ~227-248. The boundaries sit between the groups.
static constexpr int TOUCH_STRIP_Y = 240;
static constexpr int ZONE_LEFT_MAX_X = 130;   // "<": active control down
static constexpr int ZONE_RIGHT_MIN_X = 200;  // ">": active control up; the "o" between switches control
static constexpr uint32_t ACTIVE_TIMEOUT_MS = BRIGHTNESS_ACTIVE_MS;  // hud.h
static constexpr uint32_t LONG_TOUCH_MS = 1000;

// Volume table and default step: settings.h (shared with the settings menu).
static constexpr uint32_t VOLUME_SAVE_DELAY_MS = 3000;  // write to NVS only after the user stops changing it

enum class Control : uint8_t { Volume, Brightness };

static int s_step = VOLUME_DEFAULT_STEP;
static bool s_muted = false;
static uint32_t s_saveAtMs = 0;
static Control s_control = Control::Volume;
static uint32_t s_lastStripMs = 0;
static uint32_t s_muteTouchMs = 0;  // start time of the touch that already toggled mute
static uint32_t s_nextStatsMs = 0;
static int64_t s_hookSum = 0, s_hookMax = 0;  // hook cost, for the log
static uint32_t s_hookN = 0;

static uint8_t volumeValue() { return s_muted ? 0 : VOLUME_STEPS[s_step]; }

static void showVolume() {
    hudShowVolume(volumeValue(), s_muted);
    ledEffectsShowVolume(volumeValue(), s_muted);
}

static void showBrightness() {
    const int level = settingsBrightnessLevel();
    hudShowBrightness(level, SETTINGS_BRIGHTNESS_LEVELS);
    ledEffectsShowBrightness(level, SETTINGS_BRIGHTNESS_LEVELS);
}

static void applyVolume() {
    M5.Speaker.setVolume(volumeValue());
    showVolume();
    s_saveAtMs = millis() + VOLUME_SAVE_DELAY_MS;
}

static void loadVolume() {
    Preferences p;
    p.begin("nes", true);
    s_step = p.getUChar("vol_step", VOLUME_DEFAULT_STEP);
    s_muted = p.getBool("muted", false);
    p.end();
    if (s_step >= VOLUME_STEP_COUNT) s_step = VOLUME_DEFAULT_STEP;
    M5.Speaker.setVolume(volumeValue());
}

static void saveVolume() {
    s_saveAtMs = 0;
    Preferences p;
    p.begin("nes", false);
    p.putUChar("vol_step", (uint8_t)s_step);
    p.putBool("muted", s_muted);
    p.end();
}

static void saveVolumeIfDue() {
    if (s_saveAtMs != 0 && (int32_t)(millis() - s_saveAtMs) >= 0) saveVolume();
}

static void stripTouch(int x) {
    if (x >= ZONE_LEFT_MAX_X && x < ZONE_RIGHT_MIN_X) {  // "o"
        s_control = s_control == Control::Volume ? Control::Brightness : Control::Volume;
        if (s_control == Control::Brightness)
            showBrightness();
        else
            showVolume();
        return;
    }
    const int dir = x < ZONE_LEFT_MAX_X ? -1 : 1;
    if (s_control == Control::Brightness) {
        settingsSetBrightnessLevel(settingsBrightnessLevel() + dir);  // applied now, saved by settingsTick
        showBrightness();
    } else {
        s_step = constrain(s_step + dir, 0, VOLUME_STEP_COUNT - 1);
        s_muted = false;
        applyVolume();
    }
}

static void handleTouch() {
    M5.update();
    const uint32_t now = millis();
    if (s_control == Control::Brightness && now - s_lastStripMs >= ACTIVE_TIMEOUT_MS) s_control = Control::Volume;
    if (M5.Touch.getCount() == 0) return;
    const auto &t = M5.Touch.getDetail(0);
    if (t.wasPressed() && t.y >= TOUCH_STRIP_Y) {
        s_lastStripMs = now;
        stripTouch(t.x);
    } else if (t.isPressed() && t.base_y < TOUCH_STRIP_Y && t.base_msec != s_muteTouchMs &&
               now - t.base_msec >= LONG_TOUCH_MS) {
        // base_msec is when this touch began: it marks the touch, so mute toggles once per hold.
        s_muteTouchMs = t.base_msec;
        s_muted = !s_muted;
        s_control = Control::Volume;  // the red bar shows on the volume side
        applyVolume();
        log_printf("touch: mute %s\n", s_muted ? "on" : "off");
    }
}

extern "C" void osd_frame_hook(void) {
    const int64_t t0 = esp_timer_get_time();
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        loadVolume();  // here, not in setup: osd_init_sound has already configured the speaker
        ledsInit();
    }
    handleTouch();
    saveVolumeIfDue();
    settingsTick();
    // Before the LEDs: the alert applies already in this frame. The same 10 s reading
    // feeds the Smart charge latch and the battery icon, with no extra I2C.
    if (batteryTick()) {
        const int mv = batteryMv();
        powerModeTick(mv);
        hudSetBattery(batteryPercentFromMv(mv), batteryAnyCharging(), batteryPresent(mv));
    }
    const uint8_t pad = facesPadRead();
    // Select+Start opens the pause menu, which can restart into the game list: a volume
    // change still waiting for its delayed save is written as soon as the combo is held.
    if ((pad & (FACES_SELECT | FACES_START)) == (FACES_SELECT | FACES_START) && s_saveAtMs) saveVolume();
    menuCheckQuitCombo(pad);
#ifdef LED_ORDER_TEST
    // Order test: only index 0 in pure red and index 9 in pure blue.
    ledsClear();
    ledsSet(0, 255, 0, 0);
    ledsSet(9, 0, 0, 255);
#else
    ledEffectsFrame();
#endif
    ledsShow();  // after NVS: a flash write never catches a LED transfer halfway
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextStatsMs) >= 0) {
        s_nextStatsMs = now + 30000;
        log_printf("hook: avg=%.2f ms max=%.2f ms\n", s_hookSum / 1000.0f / (s_hookN ? s_hookN : 1), s_hookMax / 1000.0f);
        s_hookSum = s_hookMax = 0;
        s_hookN = 0;
    }
    const int64_t dt = esp_timer_get_time() - t0;
    s_hookSum += dt;
    s_hookN++;
    if (dt > s_hookMax) s_hookMax = dt;
}
