// User settings kept in NVS: screen brightness level and charge mode.
//
// Brightness: on the CoreS3 the backlight is not PWM. M5GFX turns the 0..255 value into
// the AXP2101 DLDO1 voltage, (value + 641) >> 5 = 0.1 V steps from 2.5 V (1..30) to 3.3 V
// (255), so only 9 distinct steps exist. The levels below land on distinct steps
// (2.6 / 2.7 / 2.8 / 3.0 / 3.3 V) and also form a near-geometric series (x1.5..1.67),
// so each press looks like a similar change. 2.5 V is skipped: it is close to the LED
// forward voltage and may be too dim to read indoors. Level 2 (96) is the value the
// firmware always used. Lower levels draw less backlight current: less heat near the
// board and longer battery life; full brightness warms the board measurably.
//
// Saving: brightness changes come in bursts (one press per step), so it is written to
// flash only after SAVE_DELAY_MS without changes, like the volume in frame_hook.cpp.
// The charge mode changes rarely (settings menu) and is written at once.

#include <M5Unified.h>
#include <Preferences.h>
#include "settings.h"
#include "power_mode.h"

static constexpr const char *NVS_NAMESPACE = "nes";
static constexpr const char *KEY_BRIGHTNESS = "bright";
static constexpr const char *KEY_CHARGE_MODE = "chg_mode";
static constexpr const char *KEY_AMBILIGHT = "ambi";
static constexpr const char *KEY_VOLUME = "vol_step";  // shared with frame_hook.cpp
static constexpr const char *KEY_MUTED = "muted";
static constexpr uint32_t SAVE_DELAY_MS = 3000;

static constexpr uint8_t BRIGHTNESS_VALUES[SETTINGS_BRIGHTNESS_LEVELS] = {40, 64, 96, 160, 255};
static_assert(BRIGHTNESS_VALUES[SETTINGS_BRIGHTNESS_DEFAULT] == 96, "default level must keep the old brightness");

static bool s_loaded = false;
static int s_brightness = SETTINGS_BRIGHTNESS_DEFAULT;
static int s_chargeMode = POWER_MODE_NORMAL;
static bool s_ambilight = true;
static int s_volumeStep = VOLUME_DEFAULT_STEP;
static bool s_muted = false;
static bool s_savePending = false;
static uint32_t s_saveAtMs = 0;

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void saveBrightness() {
    s_savePending = false;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar(KEY_BRIGHTNESS, (uint8_t)s_brightness);
    p.end();
}

void settingsInit() {
    if (s_loaded) return;
    s_loaded = true;
    Preferences p;
    p.begin(NVS_NAMESPACE, true);  // read-only: fails harmlessly (defaults) if the namespace is new
    s_brightness = clampInt(p.getUChar(KEY_BRIGHTNESS, SETTINGS_BRIGHTNESS_DEFAULT), 0, SETTINGS_BRIGHTNESS_LEVELS - 1);
    s_chargeMode = clampInt(p.getUChar(KEY_CHARGE_MODE, POWER_MODE_NORMAL), 0, POWER_MODE_COUNT - 1);
    s_ambilight = p.getBool(KEY_AMBILIGHT, true);
    s_volumeStep = clampInt(p.getUChar(KEY_VOLUME, VOLUME_DEFAULT_STEP), 0, VOLUME_STEP_COUNT - 1);
    s_muted = p.getBool(KEY_MUTED, false);
    p.end();
    M5.Display.setBrightness(BRIGHTNESS_VALUES[s_brightness]);
    log_printf("settings: brightness=%d charge_mode=%d\n", s_brightness, s_chargeMode);
}

int settingsBrightnessLevel() { return s_brightness; }

uint8_t settingsBrightnessValue(int level) {
    return BRIGHTNESS_VALUES[clampInt(level, 0, SETTINGS_BRIGHTNESS_LEVELS - 1)];
}

void settingsSetBrightnessLevel(int level) {
    level = clampInt(level, 0, SETTINGS_BRIGHTNESS_LEVELS - 1);
    // Applied even when unchanged: one I2C write (~0.2 ms), and it restores the screen
    // if something else changed the backlight.
    M5.Display.setBrightness(BRIGHTNESS_VALUES[level]);
    if (level == s_brightness) return;
    s_brightness = level;
    s_savePending = true;
    s_saveAtMs = millis() + SAVE_DELAY_MS;
}

int settingsVolumeStep() { return s_volumeStep; }
bool settingsMuted() { return s_muted; }

void settingsSetVolumeStep(int step) {
    s_volumeStep = clampInt(step, 0, VOLUME_STEP_COUNT - 1);
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar(KEY_VOLUME, (uint8_t)s_volumeStep);
    p.end();
}

void settingsSetMuted(bool muted) {
    s_muted = muted;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putBool(KEY_MUTED, muted);
    p.end();
}

bool settingsAmbilight() { return s_ambilight; }

void settingsSetAmbilight(bool on) {
    if (on == s_ambilight) return;
    s_ambilight = on;  // read by led_effects on core 1 every frame
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putBool(KEY_AMBILIGHT, on);
    p.end();
}

int settingsChargeMode() { return s_chargeMode; }

void settingsSetChargeMode(int mode) {
    mode = clampInt(mode, 0, POWER_MODE_COUNT - 1);
    if (mode == s_chargeMode) return;
    s_chargeMode = mode;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar(KEY_CHARGE_MODE, (uint8_t)mode);
    p.end();
}

void settingsTick() {
    if (!s_savePending || (int32_t)(millis() - s_saveAtMs) < 0) return;
    saveBrightness();
}

void settingsFlush() {
    if (s_savePending) saveBrightness();
}
