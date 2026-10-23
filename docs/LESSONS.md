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

### 16. One beep before the game can leave the game silent

- **Symptom:** no sound in games after changing the volume in the settings menu, whose preview beep worked.
- **Cause:** `M5.Speaker.tone()` starts the speaker with the default config, whose task is not pinned to a core
  (`task_pinned_core = ~0`). It can land on core 1, which the emulator keeps busy without yielding, so the speaker
  task never runs. The game's own config (task on core 0) was only applied `if (!M5.Speaker.isRunning())`.
- **Fix:** the NES and Game Boy sound init always call `M5.Speaker.begin()` after setting the config, which restarts
  the speaker with it (`firmware/src/nes_sound.cpp`, `firmware/src/gb/gb_sound.cpp`).
- **Online:** not found.

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
- **Fix:** embedded games come from a pack the pre-build script always creates (just a header when
  `firmware/data/roms/` is empty), the optional logo gets a 1-byte placeholder (the linker's object copy step
  rejects empty files), and a "No games found" screen still reaches the settings menu.

## Power

### 15. AXP2101 charge settings outlive a reset and a new firmware

- **Why it matters:** the charge mode (Normal, Fast, Smart, Off) writes AXP2101 registers. After "Off" or a paused
  "Smart", the device keeps not charging, even after flashing another firmware, until the mode is changed back (or
  USB is removed with the rear switch on "O").
- **Not available:** this unit's power chip does not report battery current (M5Unified returns 0).

### 17. The Faces base charger is invisible to the CoreS3

- **Symptom:** charging through the base's USB-C or the magnetic dock, the battery screen says "On battery" and the
  icon has no bolt; charge modes have no effect.
- **Cause:** the base has its own TP4057 charger wired straight to the cell; its CHRG and STDBY status pins are not
  connected (Faces Bottom3 schematic, sheet 2). The CoreS3 power chip (AXP2101) sees no VBUS and reports
  "discharging". The four pogo pins on the back are GND, 5 V in (charger input, and through a diode the bus 5 V),
  and the bus pins named G21/G22, which on the CoreS3 are the internal I2C bus (GPIO 12/11). The Charging3 dock is
  passive: a USB-C receptacle, two 5.1 kOhm CC resistors and the pogo pins.
- **Fix:** with no USB on the CoreS3, compare the average battery voltage of the last 30 s with the average of 60 to
  120 s ago: +20 mV means charging, -10 mV means it stopped (`firmware/src/battery.cpp`). An exact reading would need
  a current sensor in the dock on that I2C bus.
- **Online:** not found.

### 18. M5.Speaker streaming: blocks must outlast a mixing pass, and playRaw can block

- **Symptom:** pops in the NES sound even though the emulator ran at 60 frames per second and the queue counters
  looked healthy; later, frames lost (44 per second) right after "fixing" the queue to always hold two blocks.
- **Cause:** the M5Unified speaker task mixes one DMA buffer per pass (512 samples at the default length) and, when
  no block is queued on the channel at that moment, it writes a buffer of silence to the DMA: a 23 ms gap inserted
  into the stream, heard as a pop. With one-frame blocks (367 samples at 22050 Hz) and the two-slot queue, a pass
  regularly needed a second block that had not been queued yet. Pushing more eagerly made it worse: with a fixed
  channel, `playRaw()` waits until a slot is free, and a slot comes back only on the task's next pass, so the
  call blocked for 13 to 22 ms and the emulator lost frames.
- **Fix** (`firmware/src/nes_sound.cpp`): the APU renders one frame of samples every frame into a ring; the ring
  feeds the speaker in blocks of at least two frames (up to three), only while `isPlaying(channel) < 2`; the ring
  starts with ten frames of silence so the DMA (4 x 256 samples, kept small to limit the delay) is full and the
  task is paced by it; the ring level trims the sample count by one per frame to follow the drift between
  `esp_timer` and the I2S clock. Also: ask the APU every frame. Nofrendo applies register writes immediately and
  renders them only when asked for samples, so skipping a frame merged two frames of writes and lost one-frame
  notes (the drum hits of fast music sounded "groggy").
- **Online:** the M5Unified header documents the two-slot queue and "wait for isPlaying(channel) to drop below 2
  before refilling"; the silence insertion is visible in `Speaker_Class.inl` (the "no data" path).

### 19. The panel's SPI bus caps whole frames at ~40 per second; interlace to show all 60

- **Symptom:** NES animations that alternate frames (the SMB3 tail spin, sprite flicker) stuttered; the display
  task logged 20 to 39 frames per second, unevenly.
- **Cause:** a 256x240 RGB565 frame is 983 kbit; at the 40 MHz the ILI9342C accepts (80 MHz corrupts the picture)
  that is ~25 ms, more than the 16.7 ms frame. Drawing "when the display is free" skipped frames at random.
- **Fix** (`firmware/src/nes_display.cpp`): send the even lines of one frame and the odd lines of the next, one
  `setWindow` per line, ~14 ms per half frame, so every frame contributes. Half the lines are 1/60 s older, which
  shows as a fine comb on fast horizontal motion. A steady 30 Hz (every other frame) and a 30 Hz average of frame
  pairs were tried and judged worse on the device. The Game Boy keeps whole frames at a steady 30 Hz (one draw
  every two frames) because its smaller picture made the judder less visible.
- **Online:** not found for this panel.

