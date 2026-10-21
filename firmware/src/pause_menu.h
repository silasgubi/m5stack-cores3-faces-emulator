#pragma once

// In-game pause menu, opened by holding Select + Start (menu.cpp, menuCheckQuitCombo):
// Continue, Save state, Load state, Quit to menu. It runs on core 1 inside the frame hook,
// so the emulator is stopped while it is open. A save state freezes the whole machine
// (memory, picture, sound chip), so it also works in games with no battery save.
// Where it is stored: state_store.h.

// What each emulator provides. begin() stops its clock and waits until its display task
// has finished the frame in flight (the menu then owns the screen); end() undoes that.
struct PauseHandlers {
    bool (*save)(const char *path);
    bool (*load)(const char *path);
    void (*begin)();
    void (*end)();
};

void pauseSetHandlers(const PauseHandlers &h);
// Returns to the game after Continue, B, a save or a load. Quit restarts into the game list.
void pauseMenuRun();
