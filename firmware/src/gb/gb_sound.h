#pragma once
#include <stddef.h>
#include <stdint.h>

// Game Boy sound on M5.Speaker (mono 22050 Hz). gnuboy delivers ~370 samples per
// emulated frame (audio callback); they go into a ring and, once per frame,
// gbSoundFrame() sends blocks of 368 to the speaker (at most 2 in the queue).
// Everything runs on core 1, in the emulator loop: no task and no lock.
constexpr int GB_SAMPLE_RATE = 22050;

bool gbSoundInit();
void gbSoundSubmit(const int16_t *samples, size_t count);  // gnuboy callback
void gbSoundFrame();
// Frame period correction, in us, to keep the ring at a constant level:
// the I2S clock sets the speed, and the emulator follows it.
int gbSoundPaceAdjustUs();
