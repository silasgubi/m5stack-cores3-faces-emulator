// NES and Game Boy on the M5Stack CoreS3 + Faces Gamepad.
// Flow: mount the SD card (if any) -> game menu (a card ROM is loaded into PSRAM; the
// optional embedded ROMs from data/ come last) -> emulator picked by the extension.
// Nofrendo reads the ROM through the /xip virtual file system (cgs/vfs_xip.c).
// Select + Start for 2 s in a game sets a flag and restarts into the menu (menu.cpp).

#include <M5Unified.h>
#include <Preferences.h>
#include "faces_pad.h"
#include "leds.h"
#include "menu.h"
#include "pause_menu.h"
#include "power_mode.h"
#include "rom_source.h"
#include "sd_roms.h"
#include "settings.h"
#include "ui_text.h"
#include "gb/run_gb.h"

extern "C" {
#include "cgs/vfs_xip.h"
int nofrendo_main(int argc, char *argv[]);
void sram_autosave_force_flush(void);  // cgs/nes_save.c
bool nes_pause_save(const char *path);  // cgs/nes_save.c
bool nes_pause_load(const char *path);
void osd_pause_begin(void);  // cgs/nes_osd.c
void osd_pause_end(void);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    settingsInit();                        // also applies the saved screen brightness
    powerModeApply(settingsChargeMode());  // M5.begin sets a 4.2 V charge target: restore the saved mode
    log_printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());

    // go_menu is set by Select+Start in a game right before the restart (menu.cpp).
    Preferences p;
    p.begin("nes", false);
    const bool fromGame = p.getBool("go_menu", false);
    if (fromGame) p.putBool("go_menu", false);
    p.end();

    menuSplash();
    // Boot screen: 3 s at power-on, 1 s when coming back from a game to pick another one.
    const uint32_t splashUntil = millis() + (fromGame ? 1000 : 3000);
    facesPadInit();
    // The LEDs keep their last color through any restart (a crash too): the game list starts dark.
    if (ledsInit()) ledsShow();
    sdMount();  // mounting runs while the boot screen is up
    while ((int32_t)(millis() - splashUntil) < 0) delay(10);

    const RomSource rom = menuChooseRom();
    romSetActive(rom);
    M5.Display.fillScreen(TFT_BLACK);

    // The emulator loops never yield the CPU; without this the watchdog resets the board.
    disableCore0WDT();
    disableCore1WDT();

    if (rom.console == Console::GB) {
        log_printf("starting gnuboy: %s\n", rom.name);
        run_gb(rom.data, rom.size, rom.name);
        menuMessage(TXT_GB_FAILED, rom.name, 0);
    }

    vfs_xip_register();
    menuOnQuit(sram_autosave_force_flush);  // "Quit to menu" restarts: write the SRAM first
    pauseSetHandlers({nes_pause_save, nes_pause_load, osd_pause_begin, osd_pause_end});
    char path[80];
    snprintf(path, sizeof(path), "/xip/%s", rom.name);  // the name becomes the save name (cgs/nes_save.c)
    char *argv[1] = {path};
    log_printf("starting nofrendo: %s\n", path);
    nofrendo_main(1, argv);  // returns only if the emulator exits
    menuMessage(TXT_NES_FAILED, rom.name, 0);
}

void loop() { delay(1000); }
