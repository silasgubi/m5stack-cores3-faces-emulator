# Lessons learned

Things that cost time while building this on the M5Stack CoreS3 SE with the Faces Bottom3 base and the Faces
Gamepad3 panel. Each one has the symptom, the cause, the fix and how sure we are. "Online" says whether we found it
documented elsewhere; where we did not, that only means we did not find it.

Numbers are from one CoreS3 SE; the plain CoreS3 was not tested.

## Emulation and display

### 1. The right-most picture column shows the picture from the left edge, delayed

- **Symptom:** while the screen scrolls, the first column on the right looks like a late copy of the left side.
- **Cause:** the NES picture processor draws 8 pixels past each edge for fine scrolling. Nofrendo's
  `bmp_createhw()` makes a bitmap with zero overdraw, so the left overdraw lands at the end of the previous line.
- **Fix:** give every line of the frame buffer 8 pixels of margin on each side (pitch = 256 + 16) and point the
  bitmap 8 bytes in (`FB_OVERDRAW` in `firmware/src/cgs/nes_osd.c`). The Cardputer hides it by cropping the edges.
- **Online:** not found.

### 2. The game runs 4% fast with a FreeRTOS timer

- **Cause:** with a 1 kHz tick, `configTICK_RATE_HZ / 60` is 16 ms, which is 62.5 Hz.
- **Fix:** drive the 60 Hz tick from `esp_timer` (period 1,000,000 / 60 µs).
- **Online:** partially (the tick rounding is known).

### 3. The display SPI, not the CPU, limits the picture to about 40 frames per second

- **Measured:** a 256x240 frame takes about 25 ms to send at 40 MHz, so the screen refreshes about 40 times per second
  while emulation runs at 60 Hz (about 10 of the 16.7 ms per frame). Drawing alternates two DMA band buffers so the
  next band is converted while the previous one goes out. Do not raise the SPI clock to 80 MHz: another project
  measured corrupted address windows on this panel.
- **Online:** a lower figure (about 33 fps, 13 Hz screen) is documented for another CoreS3 NES port.

## Sound

### 4. Fixed-size audio blocks make the speaker click

- **Symptom:** music crackles a little; the queue runs dry several times per second.
- **Cause:** the emulator delivers 55 to 59 frames per second, not 60, so one fixed block per frame is about 5%
  short. `M5.Speaker` keeps at most two blocks per channel and mixes 512 samples at a time.
- **Fix:** generate the number of samples the clock asks for (time since the last call times the sample rate), in
  blocks up to three frames long (`firmware/src/nes_sound.cpp`).
- **Online:** not found for `M5.Speaker`.

### 5. Dropping the division remainder still clicks, every ~18 seconds

- **Cause:** `elapsed_us * rate / 1000000` rounded down every call loses about half a sample (about 28 per second);
  the one-to-two block reserve then drains in about 18 s and the speaker inserts 23 ms of silence.
- **Fix:** keep the remainder and add it to the next call. When the queue does run dry, add a whole block back.
- **Online:** not found.

### 6. Game Boy pace follows the I2S clock

- **Cause:** gnuboy produces a fixed number of samples per emulated frame (about 369.6). At exactly 60 Hz the ring
  overflows every ~40 s; the real frame rate is 59.73 Hz.
- **Fix:** frame period 16,743 µs plus a small correction (limited to ±250 µs) from the average ring level, so
  the speaker is the master clock (`firmware/src/gb/gb_sound.cpp`).
- **Online:** partially (similar to dynamic rate control in other emulator front ends).

## Faces Bottom3 base

### 7. The LED data pin is GPIO5, and only with the SELECTION switch on "Core2/CoreS3"

- **Cause:** the switch picks one physical M-Bus pin. In the "Core" position the LED line lands on GPIO13, which on
  the CoreS3 is the speaker's I2S data output. In "Core2/CoreS3" it is pin 8 of the bus, GPIO5.
- **Online:** not found for the CoreS3 (a wrong pin circulates for other bases).

### 8. The ten LEDs are one chain with a U-shaped order

- **Measured:** ten SK6812 (GRB order, 800 kHz) in one chain. Index 0 is on the right side next to the screen,
  0 to 4 run down the right side, 5 to 9 run up the left side. The LEDs get their 5 V from the console's M-Bus
  output, which M5Unified switches on by default.
- **Online:** not found for the Bottom3.

### 9. Two RMT writes back to back lock up arduino-esp32 2.0.17 on core 1

- **Symptom:** the emulator loop stops in the first frame that sends two LED frames close together.
- **Cause:** the IDF 4.4 RMT driver waits on a semaphore that the first transfer does not give back in time.
- **Fix:** at least 5 ms between LED writes (`firmware/src/leds.cpp`), always from the same core that did the init.
- **Online:** known in general.

### 10. The base battery becomes the CoreS3 SE battery

- **Measured:** the 600 mAh cell reaches the M-Bus BAT pin through a load switch controlled by the rear slide
  switch. With the switch on "I", the console reads the cell as its own (4.11 V), runs without a cable and charges
  from the console's USB-C. With the switch on "O" the power chip reads about 0.4 V.
- **Not controllable:** charging through the magnetic Faces Charging3 dock uses the base's own charger.
- **Online:** partially (the schematics show it; we found no write-up for the SE).

## Touch

### 11. The CoreS3 SE reports touches below the screen

- **Measured:** touches on the printed icons below the screen arrive with y from 258 to 279, x about 88-98 for "<",
  159-194 for the middle circle and 227-248 for ">". One forum thread says nothing past y = 239 ever arrives; that
  does not match this device.
- **Online:** contradicts a community post (checked on the SE only).

## Menu, card and saves

### 12. A stale byte from the gamepad panel looks like six buttons pressed

- **Cause:** right after reading the model register, the panel's key register returns the last byte it sent (`0x03`),
  which read as active-low means several buttons down. A `0x00` leftover also appears.
- **Fix:** read the key register once after the model check and treat that byte as already seen; ignore `0x00`
  (`firmware/src/faces_pad.cpp`, logic from the kexi project).
- **Online:** known.

### 13. Game Boy quick saves can wipe the other RAM banks

- **Cause:** `gnuboy_save_sram(file, true)` opens the file with `"wb"`, which truncates it, so writing only the dirty
  banks leaves the others zeroed on cartridges with more than 8 KB.
- **Fix:** always save the whole RAM, to a `.tmp` file, then rename; if the rename is interrupted the `.tmp` is the
  valid save and is recovered on the next load (`firmware/src/gb/gb_save.cpp`).
- **Online:** not found.

### 14. The ROM list must not depend on a card or an embedded game

- **Symptom:** an empty list divided by zero in the menu.
- **Fix:** optional embedded files (1-byte placeholders created by a pre-build script, because the linker's
  object copy step rejects empty files) and a "No games found" screen that still reaches the settings menu.

## Power

### 15. AXP2101 charge settings outlive a reset and a new firmware

- **Why it matters:** the charge mode (Normal, Fast, Smart, Off) writes AXP2101 registers. After "Off" or a paused
  "Smart", the device keeps not charging, even after flashing another firmware, until the mode is changed back (or
  USB is removed with the rear switch on "O").
- **Not available:** this unit's power chip does not report battery current (M5Unified returns 0).
