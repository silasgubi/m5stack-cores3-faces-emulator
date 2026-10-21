#pragma once
#include <stdbool.h>
#include <stddef.h>

// Where save states go (pause_menu.h). They follow the game: a game read from the card keeps
// its state on the card (/sd/states), an embedded game in the console's own flash
// (/flash/states, LittleFS on the "spiffs" partition of the 16 MB flash, mounted on first use).
// One state per game, named after the ROM file: "Game.gb" -> "Game.gb.state".

#ifdef __cplusplus
extern "C" {
#endif

// Path of the active game's state file. False when its storage is not available (a card
// game with the card gone, or the flash file system failed to mount).
bool statePath(char *dst, size_t len);
// True when the active game's state goes to the card, false for the console's flash.
bool stateOnCard(void);

#ifdef __cplusplus
}
#endif
