# Changelog

All notable changes to this project are documented in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 1.4.0 - 2026-10-23

### Changed

- NES picture: every frame reaches the screen. A whole 256x240 frame takes ~25 ms on the panel's SPI bus, so only
  ~39 of the 60 frames were drawn, unevenly, and animations that alternate frames (the SMB3 tail spin, sprite
  flicker) stuttered. The picture is now interlaced: the even lines of one frame and the odd lines of the next,
  ~14 ms each. Fast motion shows a slight comb effect; a steady 30 Hz and a 30 Hz blend of frame pairs were
  tried and looked worse.
- NES picture widened from 256 to 272 pixels (17:16). On a TV the NES pixel was wider than tall (8:7); 272 goes
  halfway there and keeps 24 px strips at the sides for the volume and brightness bars and the battery icon.
- Ambilight: each LED now shows the dominant color of its own rectangle, a 16 px strip at the edge of the picture
  (10 px on the Game Boy) cut into five. The old version looked 64 px in and favored colors that departed from
  the background, so it lit colors that were not at the edge.

### Fixed

- NES sound: pops and "groggy" music, worst in games with fast music such as SMB3. Two causes. The APU was asked
  for samples only when the speaker queue had room, and a skipped frame merged two frames of register writes, so
  one-frame notes (drum hits) were lost; it is now asked every frame, into a ring buffer. And the M5.Speaker task
  inserts a buffer of silence whenever it finds no queued block, which happened regularly with one-frame blocks;
  blocks are now at least two frames long, the ring starts with a stock of silence so the DMA stays full, and
  `playRaw` is only called when `isPlaying(channel) < 2` (with a fixed channel it blocks for up to ~20 ms
  otherwise, long enough to lose a frame). See lessons 18 and 19.

## 1.3.3 - 2026-10-23

### Fixed

- The ambilight stayed lit with the last game's colors after going back to the game list. The LEDs keep their last
  color through a restart, so they are now turned off before the restart and again at boot.

## 1.3.2 - 2026-10-22

### Fixed

- Game Boy: audio pops and an uneven picture, worst while scrolling. The Game Boy display task ran above the speaker
  task on core 0, so during each ~26 ms screen transfer the mixer starved and the emulator blocked for up to ~29 ms
  handing it samples (51 to 60 frames/s, the sound queue running dry up to 13 times a second). It now runs below the
  speaker, as on the NES: a steady 60 frames/s and no gaps.
- Game Boy: the picture is drawn on every other frame, a steady 30 Hz. It used to draw "when the display is free",
  checked at the start of the frame while the previous transfer still had 1 to 2 ms to go, so frames were skipped
  now and then (20 to 27 draws/s).

## 1.3.1 - 2026-10-21

### Changed

- On-screen help rewritten for the newer features: pause menu and save states, what the settings hold, card or
  built-in games, where states are saved, and charging through the console's USB-C versus the base.

## 1.3.0 - 2026-10-21

### Added

- In-game pause menu: hold Select + Start for about 1 s for Continue, Save state, Load state and Quit to menu. A save
  state freezes the whole emulated machine, so any NES or Game Boy game can be resumed, including games with no
  battery save. One state per game: games from the card keep it on the card (`/states`), embedded games in the
  console's own flash (LittleFS on the `spiffs` partition, formatted on first use).
- Charging through the Faces base (its USB-C or the magnetic dock) is shown as "Charging (base)" and with the bolt on
  the battery icons. The base charger is invisible to the CoreS3 power chip, so it is inferred from the battery
  voltage rising; it takes about 1 to 2 minutes to show or clear and may not show near a full charge. No low-battery
  alert while it is on.

### Changed

- Select + Start opens the pause menu (after about 1 s) instead of going straight back to the game list (after 2 s).

## 1.2.0 - 2026-10-21

### Added

- Any number of embedded games: every `.nes`, `.gb` and `.gbc` file in `firmware/data/roms/` is packed into the
  firmware at build time (`firmware/scripts/embed_data.py`); the file name is the name in the menu. About 4.5 MB of
  flash is free for them.
- Ambilight brightness: the Ambilight setting has levels 0 (off) to 5 (level 3 is the old fixed brightness), and the
  LEDs show a rainbow preview while that row is selected.
- A sixth, dimmest screen brightness level (2.5 V backlight), for dark rooms.

### Changed

- Game Boy picture scaled 5:3 to 266x240: full screen height, aspect ratio kept (was 3:2, 240x216).
- Settings order: Battery, Brightness, Ambilight, Volume, File transfer, Help. Mute is no longer a separate row: it is
  the first (red) box of the Volume bar. In game, the finger-on-screen mute still returns to the previous volume.
- The boot screen stays 3 s at power-on and 1 s when coming back from a game.
- The microSD game list, saves and Wi-Fi file transfer are now tested with a card (no longer "experimental").

### Fixed

- No sound in games after changing the volume in the settings menu (see lesson 16 in `docs/LESSONS.md`).

### Removed

- `firmware/platformio_local.ini` and the `EMBED_NES_NAME` / `EMBED_GB_NAME` flags, replaced by `firmware/data/roms/`.

## 1.1.0 - 2026-10-21

### Added

- Settings: ambilight LEDs on/off (when off, the LEDs still show the volume and brightness levels and the
  low-battery pulse), volume in 10 steps with a short preview beep, and mute.

### Changed

- In game, the battery icon moved to the top of the right strip, where battery icons usually are; it hides while
  the volume bar shows.
- The settings list has no "Back" row any more: B goes back (needed to fit seven rows).

## 1.0.0 - 2026-10-21

First public release. Tested on one M5Stack CoreS3 SE with the Faces Bottom3 base and the Faces Gamepad3 panel.

### Added

- NES emulation with Nofrendo: 60 Hz emulation clocked by `esp_timer`, 256x240 picture at 1:1, about 40 frames per
  second on screen (the display SPI is the limit), clock-driven sound at 22050 Hz.
- Game Boy and Game Boy Color emulation with gnuboy: 59.73 Hz frame, picture scaled 3:2 to 240x216, about 30 frames
  per second on screen, frame pace adjusted from the audio ring level.
- Game menu: `.nes`, `.gb` and `.gbc` files from `/roms` on a FAT32 microSD card (up to 64), loaded into PSRAM; the
  cursor starts on the last game played. Optional games embedded in the firmware are listed last.
- Battery-backed cartridge saves in `/nes_saves` and `/gb_saves`; Select + Start for 2 s writes the save and goes
  back to the menu.
- "No games found" screen (instead of a crash) when there is no card and no embedded game.
- Settings menu (Select in the game menu): battery screen, screen brightness in 5 levels, Wi-Fi file transfer and
  help.
- Charge modes for the CoreS3 USB-C (AXP2101): Normal 200 mA, Fast 400 mA, Smart (keeps the cell at about 40 to
  80%) and Off.
- Battery icon with percent on every menu screen and a small battery icon in the left black strip during games.
- Touch controls on the icons printed below the screen: `<` and `>` change the active control, `o` switches between
  volume and brightness, a finger held on the screen for about 1 s toggles mute. Volume and brightness are saved.
- Faces Bottom3 LEDs: screen-edge "ambilight" effect, volume level on the right LEDs, brightness level on the left
  LEDs, low-battery pulse when running on the battery.
- Wi-Fi file transfer (experimental, not yet tested with a card): the console starts its own access point with a
  new 8-digit WPA2 password per session, shows a QR code and serves a page at `http://10.10.0.1` to list, upload and
  delete games and to download and restore saves.
- On-screen help (B in the game menu) and a text boot screen.
- On-screen text in English by default and in Brazilian Portuguese with the `cores3-ptbr` build environment.
- Build without any game: a pre-build script creates 1-byte placeholders for the optional embedded files and the
  firmware ignores them.
- `docs/LESSONS.md`: 15 lessons with symptom, cause, fix and whether we found them documented online.
