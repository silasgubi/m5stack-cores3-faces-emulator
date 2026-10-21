# NES and Game Boy on the M5Stack CoreS3 + Faces

A pocket retro console built from an **M5Stack CoreS3 SE**, the **Faces Bottom3** base and the **Faces Gamepad3**
panel. It plays NES and Game Boy / Game Boy Color games from a microSD card, with the base's battery, its ten RGB LEDs
as a screen-edge "ambilight", and settings you can change without a computer.

> **Status:** works on one CoreS3 SE (the plain CoreS3 is untested). Everything that needs a microSD card (game list,
> saves, Wi-Fi file transfer) is written but **not yet tested with a card**; the embedded-game path is tested.

## What it does

- **NES** (Nofrendo): 60 Hz emulation, picture shown 1:1 at about 40 frames per second, sound at 22050 Hz.
- **Game Boy / Color** (gnuboy): 59.73 Hz, picture scaled 3:2 to 240x216, about 30 frames per second.
- **Game menu** from `/roms` on a FAT32 microSD (up to 64 games), remembers the last one, saves in `/nes_saves` and
  `/gb_saves`.
- **Faces Bottom3 LEDs:** the ten LEDs follow the colors at the left and right edges of the picture; they also show
  the volume (right side), the brightness (left side) and pulse red when the battery is low.
- **Battery:** the base's 600 mAh cell powers the console without a cable; percent and charge state on screen; four
  charge modes (Normal, Fast, Smart 40-80%, Off) when charging through the console's USB-C.
- **Touch controls** on the icons printed below the screen; in game, the battery icon sits at the top of the right
  strip and the brightness bar on the left (like the original Game Boy: contrast left, volume right).
- **Wi-Fi file transfer** (experimental): the console creates its own network, shows a QR code, and you manage games
  and saves from a web page.
- English or Brazilian Portuguese on-screen text (build option).

## What you need

| Part | Notes |
|---|---|
| M5Stack **CoreS3 SE** | The only tested board. |
| M5Stack **Faces Bottom3** (SKU A168) | With its battery. |
| **Faces Gamepad3** panel | Reads over I2C. |
| FAT32 **microSD card** | Optional for a first test; needed to play your own games. |
| USB-C data cable | For flashing. |

**Set the SELECTION switch on the base to "Core2/CoreS3".** In the "Core" position the LED data line lands on the
speaker's data pin and the LEDs will not work (see [docs/LESSONS.md](docs/LESSONS.md)). The slide switch on the back
of the base connects the battery: "I" is on, "O" is off.

## Build and flash

Needs [PlatformIO](https://platformio.org/) (Python 3.10 to 3.13).

```sh
cd firmware
pio run -e cores3 -t upload --upload-port <PORT>      # English on-screen text
pio run -e cores3-ptbr -t upload --upload-port <PORT> # Brazilian Portuguese
```

Keep the PlatformIO build folder out of cloud-synced directories (set `PLATFORMIO_WORKSPACE_DIR`).

The build works with no games at all: optional files in `firmware/data/` (`game.nes`, `game.gb`, `logo.png`) are
embedded if present, and a pre-build script creates empty placeholders if they are missing.

## Games

**No games are included.** Use games you own or free homebrew.

1. Format a microSD card as FAT32 and create a folder `roms` in its root.
2. Copy `.nes`, `.gb` or `.gbc` files, **not zipped**.
3. Insert it, power on, pick a game.

The NES core includes about 40 cartridge mappers (among them MMC1, MMC3 and MMC5), but not every game runs
correctly; only a few games were tried so far. Game Boy / Color cartridges with battery RAM keep their save on the
card.

## Controls

| Where | Control | Action |
|---|---|---|
| Game | D-pad, A, B, Select, Start | The console's buttons. |
| Game | **Select + Start for 2 s** | Back to the menu (the save is written first). |
| Menu | Up / Down, **A** or **Start** | Choose and play. |
| Menu | **Select** | Settings. |
| Menu | **B** | Help screen. |
| Game, icons below the screen | `<` / `>` | Lower / raise the active control (volume by default). |
| Game | `o` (middle circle) | Switch between volume and brightness (back to volume after 3 s). |
| Game | Finger held on the screen about 1 s | Mute on / off. |
| Console | Side button: tap | Power on. |
| Console | Side button: hold about 4 s | Power off. |

Settings (Select in the menu): battery and charge mode, brightness (5 levels), volume, mute, ambilight LEDs on/off,
Wi-Fi file transfer, help. B goes back.

### Charging

- With the rear switch on "I", the base's cell is the console's battery and charges from the console's USB-C. Charge
  modes only control this path. The magnetic Faces Charging3 dock charges through the base's own charger and cannot be
  controlled.
- The charge settings are kept by the power chip: after "Off" (or a paused "Smart") the device keeps not charging,
  even with new firmware, until you switch the mode back.
- The battery percent is estimated from voltage; the chip's own gauge has not learned the cell.

### Wi-Fi file transfer (experimental)

Settings, then File transfer. The console starts a network named `M5-Emulator-XXXX` with a new 8-digit password each
time and shows a QR code. Join it, open `http://10.10.0.1`, and list, upload and delete games, or download and restore
saves. B on the console exits and restarts into the menu. Wi-Fi never runs together with a game.

## What we learned

The interesting parts are written up in [docs/LESSONS.md](docs/LESSONS.md): the right-edge picture column bug in the
Nofrendo frame buffer, clock-driven audio feeding for `M5.Speaker`, the Faces Bottom3 LED pin, order and power, the
base battery working as the CoreS3 SE battery, touch below the screen, and more, each with how sure we are.

## Language / Idioma

On-screen text is in English by default. The `cores3-ptbr` build environment (`-DUI_LANG_PT_BR=1`) shows Brazilian
Portuguese. All strings are in `firmware/src/ui_text.h`.

## Credits and license

Distributed under the **GNU General Public License, version 2 only** ([LICENSE](LICENSE)), because the Game Boy core is
GPL-2.0 without an "or later" clause. Files derived from MIT projects keep their MIT notices. Components, origins
and changes: [THIRD_PARTY.md](THIRD_PARTY.md). Built on the work of Matthew Conte and the Nofrendo authors,
moononournation, geo-tp (Cardputer Game Station), the gnuboy authors and Alex Duchesne (retro-go), GOROman
(kexi/cluade-famicom-emu-stackchan) and M5Stack.

M5Stack, Faces, CoreS3, Nintendo, NES and Game Boy are trademarks of their respective owners. This project is not
affiliated with or endorsed by any of them.

## Em português

Emulador de NES e Game Boy para o M5Stack CoreS3 SE com a base Faces Bottom3 e o painel Faces Gamepad3. Compile com
`pio run -e cores3-ptbr -t upload` para a tela em português. Ponha a chave SELECTION da base em "Core2/CoreS3". Os
jogos não vêm junto: coloque arquivos `.nes`, `.gb` ou `.gbc` (sem compactar) na pasta `roms` de um cartão microSD
FAT32. Select + Start por 2 s volta ao menu. O que depende do cartão (lista de jogos, saves e transferência por
Wi-Fi) ainda não foi testado.
