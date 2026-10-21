# Modifications to Nofrendo

This directory contains Nofrendo, (c) 1998-2000 Matthew Conte and the authors
listed in `AUTHORS`, licensed under version 2 of the GNU Library General Public
License (`COPYING.LGPL`), in the Arduino packaging by moononournation
(<https://github.com/moononournation/arduino-nofrendo>).

**Modified by geo-tp (Cardputer Game Station, commit 74b4776); used unchanged by this project.**

- Source of this copy: Cardputer Game Station,
  <https://github.com/geo-tp/Cardputer-Game-Station-Emulators>, directory
  `lib/arduino-nofrendo`, commit 74b47763c3f2ccbaed6505b83ef0ef37c2deac82
  (2026-05-25).
- The changes were made there between 2025-10-06 and 2026-05-17 by geo-tp, with
  MMC5 and frame IRQ fixes by mariovirgili (commit cae8d59, 2026-05-17).
- Baseline for the comparison below: moononournation/arduino-nofrendo, commit
  7506ab5 (2024-07-02; last change under `src/` in 428d3e8, 2024-04-30).
- The only change made by this project is the license header of
  `src/mappers/map005.c`, restored on 2026-10-21 (comment only, see below).

This notice is given as required by section 2(b) of the GNU Library General
Public License v2 and section 2(a) of the GNU General Public License v2.

## Changed files

27 files under `src/` differ from the baseline. No file was added or removed.
Dates are those of the Cardputer Game Station commits that touched each file;
2025-10-06 is its first commit, which already contained the change.

| File | Date, author | Change |
|---|---|---|
| `src/bitmap.c` | 2025-10-06 geo-tp | `bmp_create()` stores the real pitch (width + 2 x overdraw) instead of the width, and allocates at least 256 rows (height + 16 when taller), 8-byte aligned. |
| `src/bitmap.h` | 2025-10-06 geo-tp | Adds an `owns_pixels` field to `bitmap_t` (not used by the core). |
| `src/cpu/nes6502.c` | 2025-11-04 geo-tp; 2026-05-17 mariovirgili | `null_page` and the opcode jump table made `const`; adds `nes6502_clear_irq_pending()`. |
| `src/cpu/nes6502.h` | 2026-05-17 mariovirgili | Declares `nes6502_clear_irq_pending()`. |
| `src/event.c` | 2026-03-31 geo-tp | `nes_events[]` made `const`. |
| `src/gui.c` | 2025-12-13, 2026-03-31 geo-tp | `gui_pal` made `const`; the on-screen message is allocated in `gui_init()` and freed in `gui_shutdown()` instead of being static. |
| `src/gui.h` | 2025-12-13 geo-tp | `gui_pal` declared `const`. |
| `src/gui_elem.c` | 2026-03-31 geo-tp | Small font table made `const`. |
| `src/intro.c` | 2025-11-04 geo-tp | Intro code, vector and tile tables made `const`. |
| `src/log.c` | 2025-12-13 geo-tp | The 1 KB buffer of `nofrendo_log_printf()` is allocated on first use instead of being static. |
| `src/mappers/map005.c` | 2025-10-14, 2025-11-04 geo-tp; 2026-05-17 mariovirgili; 2026-10-21 this project | MMC5 mapper rewritten after puNES: PRG/CHR banking modes, separate sprite and background CHR sets for 8x16 sprites, fill and EXRAM nametables, extended-attribute background mode, scanline IRQ, multiplier, timer and 64 KB PRG-RAM. The rewrite dropped the Nofrendo license legend; this project restored it and added a notice naming the puNES origin (GPL-2.0-or-later). Code unchanged by this project. |
| `src/memguard.c` | 2026-04-13 geo-tp | Includes `osd.h`; the non-debug `_my_malloc()` no longer asserts when out of memory, and `_my_free()` ignores a NULL pointer instead of asserting. |
| `src/nes/nes.c` | 2025-12-13 geo-tp; 2026-05-17 mariovirgili | Memory read/write handler tables allocated in `nes_create()`; writing $4017 with bit 6 set now clears a pending APU frame IRQ. |
| `src/nes/nes.h` | 2025-12-13 geo-tp | `readhandler` and `writehandler` become pointers. |
| `src/nes/nes_mmc.c` | 2026-05-17 mariovirgili | `mmc_reset()` clears the PPU background hook before the mapper init. |
| `src/nes/nes_mmc.h` | 2025-10-14 geo-tp | Final newline removed only. |
| `src/nes/nes_pal.c` | 2026-03-31 geo-tp | `shady_palette` made `const`; `nes_palette` allocated on the first `pal_generate()`; a few comments removed. |
| `src/nes/nes_pal.h` | 2026-03-31 geo-tp | Declarations updated to match `nes_pal.c`. |
| `src/nes/nes_ppu.c` | 2025-11-25 geo-tp; 2026-05-17 mariovirgili | Nametables, OAM and palette copy allocated in `ppu_create()`; adds an optional background fetch hook (`ppu_setbgfunc()`) and `ppu_obj_8x16()`. |
| `src/nes/nes_ppu.h` | 2025-11-25 geo-tp; 2026-05-17 mariovirgili | `nametab`, `oam` and `curpal` become pointers; declares the background hook and `ppu_obj_8x16()`, plus `ppu_init()` (declared, not defined). |
| `src/nes/nes_rom.c` | 2025-10-06, 2025-10-14, 2025-11-04, 2026-04-13 geo-tp | PRG/CHR ROM used in place from a memory-mapped image (`get_rom_ptr()`/`get_rom_size()`, provided by the platform layer), with a RAM fallback; the SRAM buffer is passed to the platform layer with `osd_set_sram_ptr()`; a missing ROM file is now an error instead of loading the built-in intro. |
| `src/nes/nes_rom.h` | 2025-10-14 geo-tp | Final newline removed only. |
| `src/noftypes.h` | 2026-04-13 geo-tp | `NOFRENDO_DEBUG` and `NOFRENDO_MEM_DEBUG` disabled (smaller flash). |
| `src/osd.h` | 2025-10-06 geo-tp | Includes `<stdbool.h>`. |
| `src/sndhrdw/mmc5_snd.c` | 2026-05-17 mariovirgili | $5114-$5115 removed from the MMC5 sound write range, so they reach the mapper as PRG bank registers. |
| `src/sndhrdw/nes_apu.c` | 2025-12-13 geo-tp | Lookup tables and the two square channels allocated in `apu_create()`. |
| `src/sndhrdw/nes_apu.h` | 2025-12-13 geo-tp | `rectangle` becomes a pointer. |

## Unchanged and omitted files

- `AUTHORS`, `COPYING.LGPL`, `README`, `README.md` and `library.properties` are
  identical to the baseline.
- `examples/` is not included: it comes from espressif/esp32-nesemu
  (Apache-2.0) and is not used by this firmware.
- Carried over from upstream as is: `src/config.c` has no license header,
  `src/libsnss/` refers to a `README.TXT` that upstream does not ship, and
  `src/nofconfig.h` is in the public domain.
