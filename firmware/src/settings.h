#pragma once
#include <stdint.h>

// Persistent user settings in NVS (namespace "nes", next to vol_step, muted, last_rom
// and go_menu, which belong to other modules): screen brightness and charge mode.
// Everything here is cheap and meant for core 1 (setup/menu and frame_hook). The values
// are plain ints written only from core 1, so reading them from core 0 is safe too.

constexpr int SETTINGS_BRIGHTNESS_LEVELS = 6;
constexpr int SETTINGS_BRIGHTNESS_DEFAULT = 3;  // 96, the fixed value used before this setting

// Loads both settings (only the first call does work) and applies the brightness.
// Call right after M5.begin(), in place of a fixed M5.Display.setBrightness().
void settingsInit();

// Brightness level 0..SETTINGS_BRIGHTNESS_LEVELS-1. The setter clamps, applies it to the
// screen right away and saves it to NVS once it has not changed for 3 s (settingsTick).
int settingsBrightnessLevel();
void settingsSetBrightnessLevel(int level);
// Value passed to M5.Display.setBrightness for a level (clamped).
uint8_t settingsBrightnessValue(int level);

// Charge mode 0..POWER_MODE_COUNT-1 (power_mode.h). The setter clamps and saves at once
// (it changes rarely). It does not touch the charger: call powerModeApply() afterwards.
int settingsChargeMode();
void settingsSetChargeMode(int mode);

// Sound: volume step 0..VOLUME_STEP_COUNT-1 (table below, 6 = 96 like the first version) and mute.
// Saved at once (changed from the menu, where changes are rare); the in-game touch controls
// (frame_hook.cpp) keep their own copy, loaded when the game starts, so a change here applies
// to the next game. The same NVS keys ("vol_step", "muted") are used by both.
constexpr uint8_t VOLUME_STEPS[] = {0, 8, 16, 32, 48, 64, 96, 128, 160, 200, 255};
constexpr int VOLUME_STEP_COUNT = sizeof(VOLUME_STEPS);
constexpr int VOLUME_DEFAULT_STEP = 6;
int settingsVolumeStep();
void settingsSetVolumeStep(int step);
bool settingsMuted();
void settingsSetMuted(bool muted);

// Ambilight (screen-edge colors on the Faces LEDs): level 0 = off, 1..AMBILIGHT_LEVELS-1 set
// the LED brightness. When off, the LEDs still show the volume/brightness levels and the
// low-battery pulse, at the default level brightness. Saved at once.
constexpr int AMBILIGHT_LEVELS = 6;
constexpr int AMBILIGHT_DEFAULT = 3;  // 64, the fixed LED brightness used before this setting
int settingsAmbilightLevel();
void settingsSetAmbilightLevel(int level);
bool settingsAmbilight();       // level > 0
uint8_t settingsLedBrightness();  // 0..255 scale for leds.cpp

// Once per frame (frame_hook, core 1): writes a pending brightness change when it is due.
void settingsTick();
// Writes a pending change now, e.g. right before a restart (Select+Start back to the menu).
void settingsFlush();
