#pragma once
#include <stdint.h>

// Faces Bottom3 base strip: 10 x SK6812 in a single chain (5 per side), data on the
// CoreS3 GPIO5 with the SELECTION switch on "Core2/CoreS3". Init, color and send always
// from the same task (core 1, frame_hook): the RMT interrupt is tied to the init core.
constexpr int LED_COUNT = 10;

bool ledsInit();
void ledsSet(int index, uint8_t r, uint8_t g, uint8_t b);
void ledsClear();
// Scale applied by ledsSet from its next call on (colors already set keep the old one).
void ledsSetBrightness(uint8_t value);
// Sends only if some color changed. Does not block: the RMT transmits on its own (~0.3 ms).
void ledsShow();
