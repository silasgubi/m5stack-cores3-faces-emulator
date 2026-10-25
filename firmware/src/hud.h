#pragma once
#include <stdint.h>

// Indicators drawn in the black strips beside the game, like the original Game Boy:
// brightness (contrast) on the left, volume on the right, battery at the top left.
// The hudShow/hudSet calls can come from any core: they only store the request in an
// atomic, and the drawing is done by the display task (hudDraw: NES displayTask,
// Game Boy gbDisplay), the only owner of the display SPI.
void hudShowVolume(uint8_t volume, bool muted);
void hudShowBrightness(int level, int levels);  // level 0..levels-1
void hudShowAmbilight(int level, int levels);   // level 0 (off)..levels-1, same bar as brightness, other color
void hudSetBattery(int percent, bool charging, bool present);
void hudDraw();

// How long brightness stays the active control after the last strip touch; its bar and
// LEDs stay up exactly as long, so "<"/">" never change brightness with nothing showing.
constexpr uint32_t BRIGHTNESS_ACTIVE_MS = 3000;
