#pragma once
#include <stdint.h>

// Charge modes of the CoreS3's AXP2101, plus battery helpers for the UI.
// Only the CoreS3 USB-C path goes through the AXP2101 and can be controlled. The Faces
// Charging3 dock charges the cell through the Bottom3 base's own TP4057 (~330 mA, 4.2 V,
// fixed): on the dock every mode charges to full.

constexpr int POWER_MODE_NORMAL = 0;  // 200 mA, 4.2 V (factory)
constexpr int POWER_MODE_FAST = 1;    // 400 mA, 4.2 V
constexpr int POWER_MODE_SMART = 2;   // 200 mA, kept between ~40% and ~80% (stops at 4.0 V)
constexpr int POWER_MODE_OFF = 3;     // no charging
constexpr int POWER_MODE_COUNT = 4;

// Sets the charger for a mode (clamped). Call at boot, right after settingsInit(), and
// after each change (settingsSetChargeMode). A few I2C transfers: not for every frame.
// The AXP2101 keeps these registers while it has battery or USB power, so Off (and a
// paused Smart) also hold while the console is switched off and plugged in.
void powerModeApply(int mode);

// Every ~10 s with the latest battery voltage (mV). Only Smart does work, and it only
// talks to the chip when it pauses or resumes charging, so it is cheap on core 1.
void powerModeTick(int16_t mv);

// False when the mode is Off or Smart has paused charging (for the UI: "paused" rather
// than "not charging" while on USB).
bool powerModeChargeEnabled();

// Rough charge level 0..100 from the battery voltage, for a 1S LiPo under the console's
// load (see the table in power_mode.cpp). Check batteryPresent() first.
int batteryPercentFromMv(int mv);

// False below 2500 mV: the base's rear switch is on "O" and the AXP2101 reads ~400 mV.
bool batteryPresent(int mv);
