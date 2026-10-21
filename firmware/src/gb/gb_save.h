#pragma once

// Battery-backed cartridge SRAM in /sd/gb_saves/<name without extension>.sav.
// With no card mounted, everything here is a silent no-op. Runs in the emulator loop
// (core 1): writes once the SRAM has been dirty for 500 ms, at most every 15 s.
bool gbSaveInit(const char *romName);
void gbSaveLoad();
void gbSaveTick();
// Writes now, if anything is dirty: called before restarting into the menu.
void gbSaveFlush();
