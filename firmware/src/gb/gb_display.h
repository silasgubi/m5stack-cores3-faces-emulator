#pragma once
#include <stdint.h>

// Game Boy display: 160x144 at 1.5x (240x216), centered on the 320x240 screen.
// A task on core 0 gets the framebuffer pointer through a queue, converts it in bands
// and sends them by DMA (same scheme as nes_display.cpp), so the emulation (core 1)
// does not wait for the SPI. The framebuffer comes from gnuboy as RGB565 with swapped
// bytes (GB_PIXEL_565_BE), which is the order the panel expects without setSwapBytes.
constexpr int GB_FB_W = 160, GB_FB_H = 144;

bool gbDisplayInit();
// Does not block. Returns false if the task is still drawing the previous frame
// (the frame is skipped; there is a single framebuffer and it would be rewritten mid-transfer).
bool gbDisplaySubmit(const uint16_t *fb);
bool gbDisplayBusy();
// The SD card shares the SPI with the display: whoever writes to the card from another
// core holds this, and the task waits for the frame in progress to finish.
void gbDisplayLock();
void gbDisplayUnlock();
