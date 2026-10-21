# Modifications to gnuboy

This directory contains gnuboy as maintained in retro-go by Alex Duchesne,
licensed under the GNU General Public License v2 (`COPYING`). Authors and
origin: `CREDITS` and `NOTICE`.

**Modified by geo-tp and bomberman30 (Cardputer Game Station, commit 74b4776); used unchanged by this project.**

- Source of this copy: Cardputer Game Station,
  <https://github.com/geo-tp/Cardputer-Game-Station-Emulators>, directory
  `src/gbc/gnuboy`, commit 74b47763c3f2ccbaed6505b83ef0ef37c2deac82
  (2026-05-25). Imported there on 2025-11-25.
- Baseline for the comparison below: <https://github.com/ducalex/retro-go>,
  directory `retro-core/components/gnuboy`, identical from commit 8ea557e
  (2024-07-16) to commit 4ced120 (2026-01-19). The Cardputer Game Station
  import matches it except for the `hw.c`/`hw.h` change dated 2025-11-25 below.
- This project changed no source file. It moved the sources to `src/` and
  added `library.json`, `NOTICE`, this file and `CREDITS` (restored, identical
  to retro-go's).

This notice is given as required by section 2(a) of the GNU General Public
License v2.

## Changed files

7 of the 11 source files differ from the baseline. No source file was added or
removed. Dates are those of the Cardputer Game Station commits that touched
each file.

| File | Date, author | Change |
|---|---|---|
| `src/cpu.c` | 2025-12-11 geo-tp | `gb_cpu_emulate()` no longer placed in IRAM (`IRAM_ATTR` removed). |
| `src/gnuboy.c` | 2026-04-16 geo-tp; 2026-05-06 bomberman30 | `gnuboy_free_bios()` also calls `gb_hw_cleanup()`. `do_save_load()` (save states) rewritten: refuses a NULL CPU or cartridge context, works without a sound context, and reports progress and errors with `printf()` instead of `MESSAGE_ERROR`; same file layout. |
| `src/hw.c` | 2025-11-25, 2025-12-13, 2026-03-31, 2026-04-16 geo-tp | I/O registers, OAM, palette RAM and the video palette allocated in `gb_hw_init()` instead of being arrays; adds `gb_hw_cleanup()`. |
| `src/hw.h` | 2025-11-25, 2025-12-13, 2026-04-16 geo-tp | `ioregs`, `oam`, `pal` and `video.palette` become pointers; declares `gb_hw_cleanup()`. |
| `src/lcd.c` | 2026-03-31, 2026-04-16 geo-tp | The 256-byte line buffer is allocated in `gb_lcd_init()`; `gb_lcd_reset()` adjusted for the pointer members. Side effect: its `memset(BUF, 0, sizeof(BUF))` now clears only pointer-size bytes of that buffer. |
| `src/sound.c` | 2026-04-16 geo-tp | Sound state allocated in `gb_sound_init()` instead of being static; adds `gb_sound_cleanup()` and NULL checks; some lines re-indented. |
| `src/sound.h` | 2026-04-16 geo-tp | Declares `gb_sound_cleanup()`. |

Unchanged from the baseline: `cpu.h`, `gnuboy.h`, `lcd.h`, `tables.h`,
`COPYING` and `CREDITS`.
