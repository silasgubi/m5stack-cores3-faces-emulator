// Save state location (see state_store.h).

#include <Arduino.h>
#include <LittleFS.h>
#include <stdio.h>
#include <sys/stat.h>
#include "state_store.h"
#include "rom_source.h"
#include "sd_roms.h"

static constexpr char FLASH_MOUNT[] = "/flash";
static constexpr char FLASH_PARTITION[] = "spiffs";  // label in default_16MB.csv (~3.4 MB)
static constexpr char CARD_DIR[] = "/sd/states";
static constexpr char FLASH_DIR[] = "/flash/states";

// Mounted on the first save or load, not at boot: games that never use states pay nothing.
// A partition that was never formatted (or holds another file system) is formatted once.
static bool flashMount() {
    static int s_state = 0;  // 0 not tried, 1 mounted, -1 failed
    if (s_state == 0) {
        s_state = LittleFS.begin(true, FLASH_MOUNT, 5, FLASH_PARTITION) ? 1 : -1;
        if (s_state > 0)
            log_printf("states: flash mounted, %u of %u KB used\n", (unsigned)(LittleFS.usedBytes() / 1024),
                       (unsigned)(LittleFS.totalBytes() / 1024));
        else
            log_printf("states: flash mount failed\n");
    }
    return s_state > 0;
}

extern "C" bool stateOnCard(void) { return romActive().onCard; }

extern "C" bool statePath(char *dst, size_t len) {
    const RomSource &rom = romActive();
    if (!rom.name[0]) return false;
    const char *dir = rom.onCard ? CARD_DIR : FLASH_DIR;
    if (rom.onCard ? !sdMounted() : !flashMount()) return false;
    mkdir(dir, 0777);  // error ignored: it already exists, or the file write fails and says so
    snprintf(dst, len, "%s/%s.state", dir, rom.name);
    return true;
}
