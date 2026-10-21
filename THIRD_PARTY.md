# Third-party components

This project as a whole is distributed under the **GNU General Public License, version 2 only**
(see [LICENSE](LICENSE)). The Game Boy core (gnuboy) is GPL-2.0 without an "or later" clause, so the combined
firmware cannot be offered under any other license. Files derived from MIT-licensed projects keep their MIT
notice and original copyright, as listed below.

## Included in this repository

| Component | Where | License | Copyright holders | Origin |
|---|---|---|---|---|
| **Nofrendo** (NES core), Arduino packaging with changes | `firmware/lib/nofrendo/` | LGPL-2.0 (`COPYING.LGPL`); used here as part of a GPL-2.0 work (LGPL section 3) | Matthew Conte and the authors in `AUTHORS`; Arduino packaging by moononournation; changes by geo-tp and mariovirgili | Copy from Cardputer Game Station `lib/arduino-nofrendo`, commit `74b4776`, itself from [moononournation/arduino-nofrendo](https://github.com/moononournation/arduino-nofrendo). Every changed file is listed in `firmware/lib/nofrendo/MODIFICATIONS.md`. The MMC5 mapper (`map005.c`) was rewritten after puNES (GPL-2.0-or-later); its notice was restored by this project. |
| **gnuboy** (Game Boy / Game Boy Color core) | `firmware/lib/gnuboy/` | GPL-2.0 (`COPYING`), no "or later" | Laguna, Gilgamesh and the contributors in `CREDITS`; retro-go changes by Alex Duchesne; Cardputer Game Station changes by geo-tp and bomberman30 | Copy from Cardputer Game Station `src/gbc/gnuboy`, commit `74b4776`, derived from the gnuboy in [retro-go](https://github.com/ducalex/retro-go). Origin and changes: `firmware/lib/gnuboy/NOTICE` and `MODIFICATIONS.md`. |
| **NES platform layer** (video, sound, save and ROM file access for Nofrendo) | `firmware/src/cgs/`, `firmware/src/nes_sound.cpp` | MIT | Copyright (c) geo-tp and contributors | Adapted from [Cardputer Game Station](https://github.com/geo-tp/Cardputer-Game-Station-Emulators), commit `74b4776`. Each file says what this project changed. |
| **Game Boy platform layer** (run loop, sound, saves) | `firmware/src/gb/run_gb.cpp`, `gb_save.cpp` | MIT | Copyright (c) geo-tp and contributors | Same origin; rewritten for the CoreS3 (see the file headers). |
| **Faces Gamepad3 reader** | `firmware/src/faces_pad.cpp` | MIT | Copyright (c) 2026 GOROman; Copyright (c) M5Stack Technology CO LTD | Ported from [kexi/cluade-famicom-emu-stackchan](https://github.com/kexi/cluade-famicom-emu-stackchan), which ports it from the official [M5Faces](https://github.com/m5stack/M5Faces) library (register map and polarity). |

The MIT license text for the three MIT sources above is the standard one:

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
> documentation files (the "Software"), to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
> to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
> copyright notice and this permission notice shall be included in all copies or substantial portions of the
> Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
> LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
> SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
> OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
> DEALINGS IN THE SOFTWARE.

## Not included, downloaded at build time

| Component | License | Note |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) and [M5GFX](https://github.com/m5stack/M5GFX) | MIT | PlatformIO dependencies (`lib_deps`). |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) and ESP-IDF | LGPL-2.1 and Apache-2.0 | Provided by the PlatformIO `espressif32` platform. |

## Not distributed

- **Games.** No game ROM is included or required. Bring your own (see the README).
- **Logos.** The M5Stack logo is a trademark of M5Stack and is not part of this repository. The optional boot
  image slot (`firmware/data/logo.png`) is empty in the public build, which shows a text boot screen.
