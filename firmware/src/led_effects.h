#pragma once
#include <stdint.h>

// LED strip effects, called from frame_hook (core 1).
// During the game: each side copies the color of the image edge at that height.
// After a change, the level shows for LED_BAR_MS on the side of its bar on the screen
// (hud.cpp): volume on the right LEDs, brightness on the left. Showing one ends the other.
void ledEffectsShowVolume(uint8_t volume, bool muted);
void ledEffectsShowBrightness(int level, int levels);  // level 0..levels-1
void ledEffectsShowAmbilight();  // ends both overlays: the edge effect itself shows the new level
void ledEffectsFrame();

// Frame source for the edge effect. Without a call, it uses the Nofrendo paletted
// frame (osd_frame_lines). The Game Boy registers its RGB565 framebuffer here
// (160x144; pitch in pixels), read by the same hue-bucket logic.
// fb = nullptr goes back to the Nofrendo source.
void ledEffectsSetSource565(const uint16_t *fb, int width, int height, int pitchPixels);

// Low battery alert (battery.cpp): 0 none; 1 the two bottom LEDs (indices 4 and 5)
// pulse red; 2 all 10 pulse. Drawn over the edge effect; the level overlays still win.
void ledEffectsSetBatteryAlert(int level);
