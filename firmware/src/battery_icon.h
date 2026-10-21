#pragma once
#include <stdint.h>

// Battery icons, pure drawing on M5.Display (no I2C, no state). The caller reads the
// battery (battery.cpp) and passes the values in. Both functions repaint their whole
// area every call, so they can be redrawn over themselves without leaving traces.
// Draw only from the task that owns the display: the menu loop, or displayTask in game
// (through hud.cpp), because the screen and the SD card share the SPI bus.
//
// Fill color: green >= 40 %, amber 20..39 %, red < 20 %. charging adds a bolt;
// !present (rear switch on "O", no battery) draws a grey outline with a red cross.
// percent is clamped to 0..100.

// Horizontal icon: 22x11 body plus a 2x5 tip on the right, BATTERY_ICON_W x BATTERY_ICON_H.
constexpr int BATTERY_ICON_W = 24;
constexpr int BATTERY_ICON_H = 11;
void drawBatteryIcon(int x, int y, int percent, bool charging, bool present, uint16_t bg);

// Vertical icon for the 32 px black strips beside the NES image (x 0..31), tip on top,
// filled from the bottom. (x, y, w, h) is the whole area including the tip. Below
// BATTERY_ICON_V_MIN_W x BATTERY_ICON_V_MIN_H it only clears the area.
constexpr int BATTERY_ICON_V_W = 14;  // suggested size
constexpr int BATTERY_ICON_V_H = 26;
constexpr int BATTERY_ICON_V_MIN_W = 8;
constexpr int BATTERY_ICON_V_MIN_H = 12;
void drawBatteryIconVertical(int x, int y, int w, int h, int percent, bool charging, bool present,
                             uint16_t bg);

// Call sites:
//   menu.cpp, title bar of every menu screen: drawBatteryIcon at the top right on the navy
//     bar, with the percent (text size 1) right before it.
//   hud.cpp, hudDraw() (display task): drawBatteryIconVertical at the top of the right strip,
//     cleared while the brightness bar (same strip) shows.
