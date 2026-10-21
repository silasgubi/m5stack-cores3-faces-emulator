#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "sd_roms.h"
#include "rom_source.h"

static bool s_mounted = false;
static constexpr uint32_t SD_SPI_FREQ = 25000000;

bool sdMount() {
    const int sck = M5.getPin(m5::pin_name_t::sd_spi_sclk);
    const int miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
    const int mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
    const int cs = M5.getPin(m5::pin_name_t::sd_spi_ss);
    if (sck < 0 || miso < 0 || mosi < 0 || cs < 0) {
        log_printf("SD: board has no SD pins\n");
        return false;
    }
    // The display already opened this SPI bus in M5.begin; the same pins again just share it.
    SPI.begin(sck, miso, mosi, cs);
    s_mounted = SD.begin((uint8_t)cs, SPI, SD_SPI_FREQ, "/sd");
    if (!s_mounted) {
        log_printf("SD: no card\n");
        return false;
    }
    if (!SD.exists("/roms")) SD.mkdir("/roms");
    log_printf("SD: mounted, %llu MB free\n", (unsigned long long)((SD.totalBytes() - SD.usedBytes()) >> 20));
    return true;
}

bool sdMounted() { return s_mounted; }

static int cmpNames(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

int sdListRoms(char names[][SD_NAME_MAX], int max) {
    if (!s_mounted) return 0;
    File dir = SD.open("/roms");
    if (!dir || !dir.isDirectory()) return 0;
    int n = 0;
    while (n < max) {
        File f = dir.openNextFile();
        if (!f) break;
        const char *name = f.name();
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;
        const bool ok = !f.isDirectory() && name[0] != '.' && consoleFromName(name) != Console::None &&
                        strlen(name) < SD_NAME_MAX;
        if (ok) snprintf(names[n++], SD_NAME_MAX, "%s", name);
        f.close();
    }
    dir.close();
    qsort(names, n, SD_NAME_MAX, cmpNames);
    return n;
}
