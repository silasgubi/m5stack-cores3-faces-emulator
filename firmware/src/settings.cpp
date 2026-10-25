// User settings kept in NVS: screen brightness, charge mode, ambilight and sound.
//
// Brightness: on the CoreS3 the backlight is not PWM. M5GFX turns the 0..255 value into
// the AXP2101 DLDO1 voltage, (value + 641) >> 5 = 0.1 V steps from 2.5 V (1..30) to 3.3 V
// (255), so only 9 distinct steps exist. The levels below land on distinct steps
// (2.5 / 2.6 / 2.7 / 2.8 / 3.0 / 3.3 V) and, above the first, form a near-geometric series
// (x1.5..1.67), so each press looks like a similar change. 2.5 V, the lowest step (the
// IPS panel shows nothing with the backlight off), is close to the LED forward voltage:
// very dim, meant for dark rooms. Level 3 (96) is the value the firmware always used. Lower levels draw less backlight current: less heat near the
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
static constexpr const char *KEY_BRIGHTNESS = "bri_lvl";
static constexpr const char *KEY_BRIGHTNESS_V1 = "bright";  // 5-level scale before the 2.5 V level was added
static constexpr const char *KEY_CHARGE_MODE = "chg_mode";
static constexpr const char *KEY_AMBILIGHT = "ambi_lvl";
static constexpr const char *KEY_AMBILIGHT_V1 = "ambi";  // on/off before the levels
static constexpr const char *KEY_VOLUME = "vol_step";  // shared with frame_hook.cpp
static constexpr const char *KEY_MUTED = "muted";
static constexpr uint32_t SAVE_DELAY_MS = 3000;

static constexpr uint8_t BRIGHTNESS_VALUES[SETTINGS_BRIGHTNESS_LEVELS] = {16, 40, 64, 96, 160, 255};
static_assert(BRIGHTNESS_VALUES[SETTINGS_BRIGHTNESS_DEFAULT] == 96, "default level must keep the old brightness");

// LED brightness per ambilight level (index 0 = off). Capped at 192: 10 LEDs in full white
// draw ~370 mA at 255, a lot for the 600 mAh battery. Below 16 the colors lose too many
// steps per channel.
static constexpr uint8_t AMBILIGHT_VALUES[AMBILIGHT_LEVELS] = {0, 16, 32, 64, 128, 192};
static_assert(AMBILIGHT_VALUES[AMBILIGHT_DEFAULT] == 64, "default level must keep the old LED brightness");

static bool s_loaded = false;
static int s_brightness = SETTINGS_BRIGHTNESS_DEFAULT;
static int s_chargeMode = POWER_MODE_NORMAL;
static int s_ambilight = AMBILIGHT_DEFAULT;
static int s_volumeStep = VOLUME_DEFAULT_STEP;
static bool s_muted = false;
static bool s_savePending = false;
static uint32_t s_saveAtMs = 0;

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Brightness and ambilight share the delayed save (both change from the touch strip in a game).
static void saveBrightness() {
    s_savePending = false;
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUChar(KEY_BRIGHTNESS, (uint8_t)s_brightness);
    p.putUChar(KEY_AMBILIGHT, (uint8_t)s_ambilight);
    p.end();
}

void settingsInit() {
    if (s_loaded) return;
    s_loaded = true;
    Preferences p;
    p.begin(NVS_NAMESPACE, true);  // read-only: fails harmlessly (defaults) if the namespace is new
    // Older keys are read once, mapped to the new scales (the old brightness levels sit one up).
    const int brightness = p.isKey(KEY_BRIGHTNESS) ? p.getUChar(KEY_BRIGHTNESS)
                           : p.isKey(KEY_BRIGHTNESS_V1) ? p.getUChar(KEY_BRIGHTNESS_V1) + 1
                                                        : SETTINGS_BRIGHTNESS_DEFAULT;
    s_brightness = clampInt(brightness, 0, SETTINGS_BRIGHTNESS_LEVELS - 1);
    s_chargeMode = clampInt(p.getUChar(KEY_CHARGE_MODE, POWER_MODE_NORMAL), 0, POWER_MODE_COUNT - 1);
    const int ambilight = p.isKey(KEY_AMBILIGHT) ? p.getUChar(KEY_AMBILIGHT)
                          : p.getBool(KEY_AMBILIGHT_V1, true) ? AMBILIGHT_DEFAULT
                                                             : 0;
    s_ambilight = clampInt(ambilight, 0, AMBILIGHT_LEVELS - 1);
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

int settingsAmbilightLevel() { return s_ambilight; }
bool settingsAmbilight() { return s_ambilight > 0; }
uint8_t settingsLedBrightness() { return AMBILIGHT_VALUES[s_ambilight > 0 ? s_ambilight : AMBILIGHT_DEFAULT]; }

void settingsSetAmbilightLevel(int level) {
    level = clampInt(level, 0, AMBILIGHT_LEVELS - 1);
    if (level == s_ambilight) return;
    s_ambilight = level;  // read by led_effects on core 1 every frame
    // Saved with the brightness, after the delay: a flash write inside a game frame costs a frame.
    s_savePending = true;
    s_saveAtMs = millis() + SAVE_DELAY_MS;
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
