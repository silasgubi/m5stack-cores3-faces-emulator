#pragma once

// "File transfer" mode, started from the settings menu. The console creates its own Wi-Fi
// network (WPA2, a new random password every time; no home Wi-Fi credentials anywhere)
// and serves a web page at http://10.10.0.1 to manage the SD card: add or delete games in
// /roms, download saves from /nes_saves and /gb_saves or restore them.
// Blocking and never returns: B on the gamepad (or no SD card) restarts into the menu,
// because Wi-Fi and the web server must never run next to the emulators.
[[noreturn]] void fileTransferRun();
