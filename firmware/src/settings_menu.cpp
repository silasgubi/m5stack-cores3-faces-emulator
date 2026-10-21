// Settings screen (Select in the game menu). Runs in the menu loop on core 1, before any
// emulator, so it draws and reads the battery (I2C) directly.
//
// Rows: Battery (A opens the battery screen), Brightness, Ambilight and Volume (left/right,
// level bars), File transfer (A starts it) and Help. Up/down moves, B goes back. Only the
// rows and values that change are redrawn.
// Bars: the first Ambilight box is off and the first Volume box is mute (red when chosen);
// the first Brightness box is the dimmest step, since the panel needs its backlight.
// Battery screen: level, voltage and state, refreshed every 2 s, and the charge mode
// (left/right), applied to the AXP2101 and saved at once.

#include <M5Unified.h>
#include <string.h>
#include "settings_menu.h"
#include "battery.h"
#include "faces_pad.h"
#include "leds.h"
#include "menu.h"
#include "power_mode.h"
#include "settings.h"
#include "ui_text.h"
#include "wifi_transfer.h"

enum Item : int { ITEM_BATTERY, ITEM_BRIGHTNESS, ITEM_AMBILIGHT, ITEM_VOLUME, ITEM_TRANSFER, ITEM_HELP, ITEM_COUNT };
static_assert(ITEM_COUNT == (int)TXT_SET_ITEM_COUNT, "one label per settings row (ui_text.h)");
static_assert(POWER_MODE_COUNT == (int)TXT_CHARGE_MODE_COUNT, "one name per charge mode (ui_text.h)");

static constexpr int FOOT_Y = 240 - 9;
static bool s_ledsUsed = false;  // the ambilight preview has set up the LED strip
static constexpr uint16_t SEL_BG = TFT_DARKGREEN;

// Settings rows: size-2 label on the left, value on the right.
static constexpr int ROW_Y0 = MENU_HEAD_H + 4, ROW_PITCH = 26, ROW_H = 22, ROW_TEXT_DY = (ROW_H - 16) / 2;
static constexpr int ARROW_R_X = 320 - 8 - 12;  // ">" at text size 2
static constexpr int SEG_W = 16, SEG_GAP = 4, SEG_H = 12;  // brightness and ambilight boxes
static constexpr int VOL_SEG_W = 8, VOL_SEG_GAP = 2;         // volume: more steps, narrower boxes
static_assert(AMBILIGHT_LEVELS == SETTINGS_BRIGHTNESS_LEVELS, "both bars share the box layout");
static_assert(VOLUME_STEP_COUNT * (VOL_SEG_W + VOL_SEG_GAP) <= SETTINGS_BRIGHTNESS_LEVELS * (SEG_W + SEG_GAP),
              "the volume bar must fit the space of the others");
static constexpr int SEG_X0 = ARROW_R_X - 6 - SETTINGS_BRIGHTNESS_LEVELS * (SEG_W + SEG_GAP) + SEG_GAP;
static constexpr int ARROW_L_X = SEG_X0 - 6 - 12;

// Battery screen: size-1 labels, size-2 values from x=80 (ui::VALUE_MAX_2 chars).
static constexpr int VALUE_X = 80;
static constexpr int Y_LEVEL = 46, Y_VOLTAGE = 70, Y_STATE = 94, Y_DETAIL = 118;
static constexpr int Y_MODE_LABEL = 138, Y_MODE = 150, Y_MODE_INFO = 172, Y_NOTE = 194;
static constexpr uint32_t BATTERY_SCREEN_MS = 2000;  // faster than the menus: changes are expected here
static constexpr uint32_t MODE_SETTLE_MS = 500;      // the charger state follows a mode change

// New presses only: a button still held when a screen opens does not count.
struct PadEdges {
    uint8_t prev = facesPadRead();
    uint8_t poll() {
        const uint8_t p = facesPadRead();
        const uint8_t pressed = p & ~prev;
        prev = p;
        return pressed;
    }
    void resync() { prev = facesPadRead(); }
};

static void drawFooter(const char *text) {
    M5.Display.fillRect(0, FOOT_Y - 1, 320, 10, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(8, FOOT_Y);
    M5.Display.print(text);
}

static int rowY(int item) { return ROW_Y0 + item * ROW_PITCH; }

// ---- Settings list ----

// Volume as shown and set here: mute counts as step 0.
static int volumeShown() { return settingsMuted() ? 0 : settingsVolumeStep(); }

// "<", one box per level and ">". Boxes up to the value are lit; with zeroOff the first box
// means off/mute instead: red when chosen, a red outline otherwise.
static void drawLevelBar(int item, bool sel, int count, int value, int w, int gap, bool zeroOff) {
    const int y = rowY(item), sy = y + (ROW_H - SEG_H) / 2;
    const uint16_t bg = sel ? SEL_BG : TFT_BLACK, fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(value > 0 ? fg : TFT_DARKGREY, bg);
    M5.Display.setCursor(ARROW_L_X, y + ROW_TEXT_DY);
    M5.Display.print('<');
    for (int k = 0; k < count; ++k) {
        const int x = SEG_X0 + k * (w + gap);
        const bool off = zeroOff && k == 0;
        if (off ? value == 0 : k <= value) {
            M5.Display.fillRect(x, sy, w, SEG_H, off ? TFT_RED : TFT_GREEN);
        } else {
            M5.Display.fillRect(x, sy, w, SEG_H, bg);
            M5.Display.drawRect(x, sy, w, SEG_H, off ? TFT_RED : TFT_DARKGREY);
        }
    }
    M5.Display.setTextColor(value < count - 1 ? fg : TFT_DARKGREY, bg);
    M5.Display.setCursor(ARROW_R_X, y + ROW_TEXT_DY);
    M5.Display.print('>');
}

static void drawLevelValue(int item, bool sel) {
    if (item == ITEM_BRIGHTNESS)
        drawLevelBar(item, sel, SETTINGS_BRIGHTNESS_LEVELS, settingsBrightnessLevel(), SEG_W, SEG_GAP, false);
    else if (item == ITEM_AMBILIGHT)
        drawLevelBar(item, sel, AMBILIGHT_LEVELS, settingsAmbilightLevel(), SEG_W, SEG_GAP, true);
    else
        drawLevelBar(item, sel, VOLUME_STEP_COUNT, volumeShown(), VOL_SEG_W, VOL_SEG_GAP, true);
}

// Step 0 sets mute and keeps the stored step, so the in-game unmute (finger on the screen)
// brings back a real level.
static void setVolumeShown(int step) {
    step = constrain(step, 0, VOLUME_STEP_COUNT - 1);
    if (step == 0) {
        settingsSetMuted(true);
        return;
    }
    settingsSetVolumeStep(step);
    if (settingsMuted()) settingsSetMuted(false);
}

// Preview colors from the top (near the screen) down, the same on both sides: the rainbow
// flag stripes, with blue and violet merged into one, since each side has 5 LEDs.
static constexpr uint8_t PREVIEW_RGB[LED_COUNT / 2][3] = {
    {255, 0, 0}, {255, 96, 0}, {255, 220, 0}, {0, 200, 0}, {64, 0, 255}};

// While the Ambilight row is selected, the strip shows PREVIEW_RGB at the chosen level, so
// the change can be seen (in the game the colors come from the screen). Off otherwise.
static void previewAmbilight(bool show) {
    if (!show && !s_ledsUsed) return;
    s_ledsUsed = s_ledsUsed || ledsInit();
    if (!s_ledsUsed) return;
    if (!show || !settingsAmbilight()) {
        ledsClear();
    } else {
        ledsSetBrightness(settingsLedBrightness());
        for (int h = 0; h < LED_COUNT / 2; ++h) {  // height h: index h on the right, 9 - h on the left
            const uint8_t *c = PREVIEW_RGB[h];
            ledsSet(h, c[0], c[1], c[2]);
            ledsSet(LED_COUNT - 1 - h, c[0], c[1], c[2]);
        }
    }
    ledsShow();
}

// A short beep at the new volume, so the change can be heard (the game sets its own level later).
static void previewVolume() {
    if (volumeShown() == 0) return;
    M5.Speaker.setVolume(VOLUME_STEPS[settingsVolumeStep()]);
    M5.Speaker.tone(880, 80);
}

static void drawRow(int item, bool sel) {
    const int y = rowY(item);
    const uint16_t bg = sel ? SEL_BG : TFT_BLACK, fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    M5.Display.fillRect(0, y, 320, ROW_H, bg);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(fg, bg);
    M5.Display.setCursor(8, y + ROW_TEXT_DY);
    M5.Display.print(TXT_SET_ITEMS[item]);
    if (item == ITEM_BRIGHTNESS || item == ITEM_AMBILIGHT || item == ITEM_VOLUME) {
        drawLevelValue(item, sel);
        return;
    }
    if (item == ITEM_BATTERY) {  // the current charge mode, visible without opening the screen
        const char *mode = TXT_CHARGE_MODES[settingsChargeMode()];
        M5.Display.setTextSize(1);
        M5.Display.setCursor(ARROW_R_X - 6 - 6 * (int)strlen(mode), y + (ROW_H - 8) / 2);
        M5.Display.print(mode);
        M5.Display.setTextSize(2);
    }
    {  // ">": A opens or starts something
        M5.Display.setCursor(ARROW_R_X, y + ROW_TEXT_DY);
        M5.Display.print('>');
    }
}

static void drawSettings(int sel) {
    M5.Display.fillScreen(TFT_BLACK);
    menuDrawTitleBar(TXT_SETTINGS, nullptr);
    for (int i = 0; i < ITEM_COUNT; ++i) drawRow(i, i == sel);
    drawFooter(TXT_SET_FOOTER);
}

// ---- Battery screen ----

static char s_shown[3][ui::VALUE_MAX_2 + 1];  // values on screen, so unchanged ones are not redrawn
static char s_shownDetail[ui::LINE_MAX_1 + 1];

static void drawLabel(int y, const char *label) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.print(label);
}

static void drawValue(int slot, int y, const char *text, uint16_t color) {
    if (strcmp(s_shown[slot], text) == 0) return;
    snprintf(s_shown[slot], sizeof(s_shown[slot]), "%s", text);
    M5.Display.fillRect(VALUE_X, y, 320 - 8 - VALUE_X, 16, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setCursor(VALUE_X, y);
    M5.Display.print(text);
}

static void drawDetail(const char *text, uint16_t color) {
    if (strcmp(s_shownDetail, text) == 0) return;
    snprintf(s_shownDetail, sizeof(s_shownDetail), "%s", text);
    M5.Display.fillRect(0, Y_DETAIL, 320, 8, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setCursor(8, Y_DETAIL);
    M5.Display.print(text);
}

static uint16_t levelColor(int pct) { return pct >= 40 ? TFT_GREEN : pct >= 20 ? TFT_ORANGE : TFT_RED; }

// State from the last reading. "Paused" and "off" only make sense with USB power, which
// is when the user wonders why it does not charge.
static const char *stateText(int mv, uint16_t &color) {
    if (!batteryPresent(mv)) {
        color = TFT_RED;
        return TXT_BAT_ABSENT;
    }
    if (batteryCharging()) {
        color = TFT_GREEN;
        return TXT_BAT_CHARGING;
    }
    if (batteryUsb()) {
        color = TFT_YELLOW;
        if (settingsChargeMode() == POWER_MODE_OFF) return TXT_BAT_CHARGE_OFF;
        if (!powerModeChargeEnabled()) return TXT_BAT_PAUSED;
        return TXT_BAT_NOT_CHARGING;
    }
    color = TFT_WHITE;
    return TXT_BAT_ON_BATTERY;
}

static void drawBatteryValues() {
    const int mv = batteryMv();
    const bool present = batteryPresent(mv);
    const int pct = batteryPercentFromMv(mv);
    char text[24];
    if (present) {
        snprintf(text, sizeof(text), "%d%%", pct);
        drawValue(0, Y_LEVEL, text, levelColor(pct));
        snprintf(text, sizeof(text), "%d.%02d V", mv / 1000, mv % 1000 / 10);
        drawValue(1, Y_VOLTAGE, text, TFT_WHITE);
    } else {  // the AXP2101 reads ~0.4 V on an open input: not worth showing
        drawValue(0, Y_LEVEL, "--", TFT_DARKGREY);
        drawValue(1, Y_VOLTAGE, "--", TFT_DARKGREY);
    }
    uint16_t color;
    const char *state = stateText(mv, color);
    drawValue(2, Y_STATE, state, color);

    // The CoreS3 AXP2101 has no current sensor (M5Unified returns 0 there); boards with
    // one get a current line, positive while charging.
    const int ma = (int)M5.Power.getBatteryCurrent();
    if (!present) {
        drawDetail(TXT_BAT_ABSENT_HINT, TFT_LIGHTGREY);
    } else if (ma != 0) {
        snprintf(text, sizeof(text), "%s: %+d mA", TXT_BAT_CURRENT, ma);
        drawDetail(text, TFT_LIGHTGREY);
    } else {
        drawDetail("", TFT_BLACK);
    }
}

static void drawChargeMode() {
    const int mode = settingsChargeMode();
    M5.Display.fillRect(0, Y_MODE, 320, 16, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, Y_MODE);
    M5.Display.setTextColor(mode > 0 ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    M5.Display.print("< ");
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.print(TXT_CHARGE_MODES[mode]);
    M5.Display.setTextColor(mode < POWER_MODE_COUNT - 1 ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    M5.Display.print(" >");
    M5.Display.fillRect(0, Y_MODE_INFO, 320, 8, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(8, Y_MODE_INFO);
    M5.Display.print(TXT_CHARGE_MODE_INFO[mode]);
}

static void batteryScreen() {
    M5.Display.fillScreen(TFT_BLACK);
    menuDrawTitleBar(TXT_BATTERY, nullptr);
    drawLabel(Y_LEVEL + 4, TXT_BAT_LEVEL);
    drawLabel(Y_VOLTAGE + 4, TXT_BAT_VOLTAGE);
    drawLabel(Y_STATE + 4, TXT_BAT_STATE);
    drawLabel(Y_MODE_LABEL, TXT_BAT_MODE);
    for (size_t i = 0; i < TXT_BAT_NOTE_COUNT; ++i) drawLabel(Y_NOTE + 12 * (int)i, TXT_BAT_NOTE_LINES[i]);
    drawFooter(TXT_BAT_FOOTER);
    drawChargeMode();
    // Nothing shown yet: every value differs from "" and gets drawn; an empty detail line
    // is already blank after the clear.
    memset(s_shown, 0, sizeof(s_shown));
    memset(s_shownDetail, 0, sizeof(s_shownDetail));

    PadEdges pad;
    uint32_t nextRead = millis();
    for (;;) {
        const uint8_t pressed = pad.poll();
        const uint32_t now = millis();
        if (pressed & FACES_B) return;
        if (pressed & (FACES_LEFT | FACES_RIGHT)) {
            const int mode = settingsChargeMode() + ((pressed & FACES_LEFT) ? -1 : 1);
            if (mode >= 0 && mode < POWER_MODE_COUNT) {
                settingsSetChargeMode(mode);  // saved at once
                powerModeApply(mode);         // and applied at once
                drawChargeMode();
                nextRead = now + MODE_SETTLE_MS;
            }
        }
        if ((int32_t)(now - nextRead) >= 0) {
            nextRead = now + BATTERY_SCREEN_MS;
            menuBatteryPoll(0);  // read now; also redraws the title bar battery
            drawBatteryValues();
        }
        settingsTick();
        delay(16);
    }
}

// ---- Entry point ----

void settingsMenuRun() {
    log_printf("settings: open\n");
    int sel = ITEM_BATTERY;
    drawSettings(sel);
    PadEdges pad;
    for (;;) {
        ledsShow();  // a preview change that fell inside the 5 ms gap goes out now
        const uint8_t pressed = pad.poll();
        if (pressed & (FACES_UP | FACES_DOWN)) {
            const int old = sel;
            sel = (sel + ((pressed & FACES_UP) ? -1 : 1) + ITEM_COUNT) % ITEM_COUNT;
            drawRow(old, false);
            drawRow(sel, true);
            previewAmbilight(sel == ITEM_AMBILIGHT);
        }
        const int dir = (pressed & FACES_LEFT) ? -1 : (pressed & FACES_RIGHT) ? 1 : 0;
        if (dir && (sel == ITEM_BRIGHTNESS || sel == ITEM_AMBILIGHT || sel == ITEM_VOLUME)) {
            if (sel == ITEM_BRIGHTNESS) settingsSetBrightnessLevel(settingsBrightnessLevel() + dir);
            if (sel == ITEM_AMBILIGHT) settingsSetAmbilightLevel(settingsAmbilightLevel() + dir);
            if (sel == ITEM_VOLUME) {
                setVolumeShown(volumeShown() + dir);
                previewVolume();
            }
            drawLevelValue(sel, true);
            previewAmbilight(sel == ITEM_AMBILIGHT);
            continue;
        }
        if (pressed & FACES_B) break;
        if (pressed & FACES_A) {
            if (sel == ITEM_TRANSFER) {
                settingsFlush();  // the transfer mode ends in a restart
                fileTransferRun();
            }
            if (sel == ITEM_BATTERY || sel == ITEM_HELP) {
                if (sel == ITEM_BATTERY)
                    batteryScreen();
                else
                    menuShowHelp();
                drawSettings(sel);
                pad.resync();  // the B that left that screen may still be held
                continue;
            }
        }
        menuBatteryPoll(MENU_BATTERY_MS);
        settingsTick();
        delay(16);
    }
    previewAmbilight(false);
    delay(6);  // ledsShow keeps 5 ms between transfers
    ledsShow();
    settingsFlush();
    log_printf("settings: close (brightness=%d charge_mode=%d)\n", settingsBrightnessLevel(), settingsChargeMode());
}
