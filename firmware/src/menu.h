#pragma once
#include <stdint.h>
#include "rom_source.h"

// Game menu on the screen, driven by the Gamepad (up/down, A or Start picks, Select opens
// the settings, B the help). Lists the games in /sd/roms plus the valid embedded ROMs.
// Remembers the last pick in NVS and starts with the cursor on it. Returns only when a
// game is picked, already loaded. With no game at all it shows how to add some and
// restarts on any button.
RomSource menuChooseRom();

// Boot screen: the optional image in data/logo.png (centered on white) when it is a real
// PNG, otherwise a text splash.
void menuSplash();

// Called by frame_hook during a game: Select + Start for about 1 s opens the pause menu
// (pause_menu.h), whose "Quit to menu" calls menuQuitToGameList.
void menuCheckQuitCombo(uint8_t padBits);
// Back to the game list: sets a flag in NVS and restarts (the emulators have no clean exit).
[[noreturn]] void menuQuitToGameList();
// Called right before that restart (e.g. to write the game's save).
void menuOnQuit(void (*fn)());
// Full-screen message (error or notice), shown for waitMs ms (0 = forever).
void menuMessage(const char *title, const char *detail, uint32_t waitMs);

// Shared by the menu screens (settings_menu.cpp). All of them run on core 1 before any
// emulator, so they draw and use I2C freely.
constexpr int MENU_HEAD_H = 36;     // navy title bar
constexpr uint32_t MENU_BATTERY_MS = 10000;
// Title bar: title on the left, optional text (right) and the battery at the top right.
void menuDrawTitleBar(const char *title, const char *right);
// Reads the battery when periodMs has passed since the last reading, runs the Smart
// charge latch (powerModeTick) and redraws the battery of the title bar. True if it read.
bool menuBatteryPoll(uint32_t periodMs);
// Help screen; returns after any button, with every button released.
void menuShowHelp();
