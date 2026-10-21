#pragma once

// Settings screen, opened with Select in the game menu: battery and charge mode, screen
// brightness, file transfer (Wi-Fi) and help. Returns on Back or B, possibly with that
// button still held. File transfer never returns: it ends in a restart into the menu.
void settingsMenuRun();
