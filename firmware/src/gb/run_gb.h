#pragma once
#include <stddef.h>
#include <stdint.h>

// Game Boy / Game Boy Color with gnuboy (the "gnuboy" lib, taken from Cardputer Game Station).
// Blocking: only returns if the emulator fails to start. 160x144 screen at 1.5x (240x216),
// centered; sound on M5.Speaker; input from the Faces (faces_pad.h); saves in
// /sd/gb_saves/<name>.sav. Calls osd_frame_hook() once per frame, like the NES.
void run_gb(const uint8_t *rom, size_t romLen, const char *romName);
