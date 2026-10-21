// Settings screen (Select in the game menu). Runs in the menu loop on core 1, before any
// emulator, so it draws and reads the battery (I2C) directly.
//
// Rows: Battery (A opens the battery screen), Brightness (left/right, applied live),
// File transfer (A starts it), Help and Back. Up/down moves, B goes back. Only the rows
// and values that change are redrawn.
// Battery screen: level, voltage and state, refreshed every 2 s, and the charge mode
// (left/right), applied to the AXP2101 and saved at once.

#include <M5Unified.h>
#include <string.h>
#include "settings_menu.h"
#include "battery.h"
#include "faces_pad.h"
#include "menu.h"
#include "power_mode.h"
#include "settings.h"
#include "ui_text.h"
#include "wifi_transfer.h"

enum Item : int { ITEM_BATTERY, ITEM_BRIGHTNESS, ITEM_VOLUME, ITEM_MUTE, ITEM_AMBILIGHT, ITEM_TRANSFER, ITEM_HELP, ITEM_COUNT };
static_assert(ITEM_COUNT == (int)TXT_SET_ITEM_COUNT, "one label per settings row (ui_text.h)");
static_assert(POWER_MODE_COUNT == (int)TXT_CHARGE_MODE_COUNT, "one name per charge mode (ui_text.h)");

static constexpr int FOOT_Y = 240 - 9;
static constexpr uint16_t SEL_BG = TFT_DARKGREEN;

// Settings rows: size-2 label on the left, value on the right.
static constexpr int ROW_Y0 = MENU_HEAD_H + 4, ROW_PITCH = 26, ROW_H = 22, ROW_TEXT_DY = (ROW_H - 16) / 2;
static constexpr int ARROW_R_X = 320 - 8 - 12;  // ">" at text size 2
static constexpr int SEG_W = 16, SEG_GAP = 4, SEG_H = 12;  // brightness level boxes
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

static void drawBrightnessValue(bool sel) {
    const int y = rowY(ITEM_BRIGHTNESS), level = settingsBrightnessLevel();
    const uint16_t bg = sel ? SEL_BG : TFT_BLACK, fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(level > 0 ? fg : TFT_DARKGREY, bg);
    M5.Display.setCursor(ARROW_L_X, y + ROW_TEXT_DY);
    M5.Display.print('<');
    for (int k = 0; k < SETTINGS_BRIGHTNESS_LEVELS; ++k) {
        const int x = SEG_X0 + k * (SEG_W + SEG_GAP), sy = y + (ROW_H - SEG_H) / 2;
        if (k <= level) {
            M5.Display.fillRect(x, sy, SEG_W, SEG_H, TFT_GREEN);
        } else {
            M5.Display.fillRect(x, sy, SEG_W, SEG_H, bg);
            M5.Display.drawRect(x, sy, SEG_W, SEG_H, TFT_DARKGREY);
        }
    }
    M5.Display.setTextColor(level < SETTINGS_BRIGHTNESS_LEVELS - 1 ? fg : TFT_DARKGREY, bg);
    M5.Display.setCursor(ARROW_R_X, y + ROW_TEXT_DY);
    M5.Display.print('>');
}

// Volume: ten boxes (steps 1..10); step 0 shows none. Mute greys the boxes.
static void drawVolumeValue(bool sel) {
    constexpr int W = 8, GAP = 2, H = 12;
    const int y = rowY(ITEM_VOLUME), step = settingsVolumeStep();
    const uint16_t bg = sel ? SEL_BG : TFT_BLACK, fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(step > 0 ? fg : TFT_DARKGREY, bg);
    M5.Display.setCursor(ARROW_L_X, y + ROW_TEXT_DY);
    M5.Display.print('<');
    for (int k = 1; k < VOLUME_STEP_COUNT; ++k) {
        const int x = SEG_X0 + (k - 1) * (W + GAP), sy = y + (ROW_H - H) / 2;
        if (k <= step) {
            M5.Display.fillRect(x, sy, W, H, settingsMuted() ? TFT_DARKGREY : TFT_GREEN);
        } else {
            M5.Display.fillRect(x, sy, W, H, bg);
            M5.Display.drawRect(x, sy, W, H, TFT_DARKGREY);
        }
    }
    M5.Display.setTextColor(step < VOLUME_STEP_COUNT - 1 ? fg : TFT_DARKGREY, bg);
    M5.Display.setCursor(ARROW_R_X, y + ROW_TEXT_DY);
    M5.Display.print('>');
}

// A short beep at the new volume, so the change can be heard (the game sets its own level later).
static void previewVolume() {
    if (settingsMuted() || settingsVolumeStep() == 0) return;
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
    if (item == ITEM_BRIGHTNESS) {
        drawBrightnessValue(sel);
        return;
    }
    if (item == ITEM_VOLUME) {
        drawVolumeValue(sel);
        return;
    }
    if (item == ITEM_AMBILIGHT || item == ITEM_MUTE) {  // On/Off shown in place of the ">" arrow
        const bool on = item == ITEM_MUTE ? settingsMuted() : settingsAmbilight();
        const char *v = on ? TXT_ON : TXT_OFF;
        // Mute "On" is the state to notice (red); ambilight "On" is the normal state (green).
        M5.Display.setTextColor(item == ITEM_MUTE ? (on ? TFT_RED : TFT_DARKGREY) : (on ? TFT_GREEN : TFT_DARKGREY), bg);
        M5.Display.setCursor(ARROW_R_X + 12 - 12 * (int)strlen(v), y + ROW_TEXT_DY);
        M5.Display.print(v);
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
        const uint8_t pressed = pad.poll();
        if (pressed & (FACES_UP | FACES_DOWN)) {
            const int old = sel;
            sel = (sel + ((pressed & FACES_UP) ? -1 : 1) + ITEM_COUNT) % ITEM_COUNT;
            drawRow(old, false);
            drawRow(sel, true);
        }
        if (sel == ITEM_BRIGHTNESS && (pressed & (FACES_LEFT | FACES_RIGHT))) {
            settingsSetBrightnessLevel(settingsBrightnessLevel() + ((pressed & FACES_LEFT) ? -1 : 1));
            drawBrightnessValue(true);
        }
        if (sel == ITEM_VOLUME && (pressed & (FACES_LEFT | FACES_RIGHT))) {
            settingsSetVolumeStep(settingsVolumeStep() + ((pressed & FACES_LEFT) ? -1 : 1));
            if (settingsMuted()) settingsSetMuted(false);  // changing the volume unmutes, like in the game
            drawRow(ITEM_VOLUME, true);
            drawRow(ITEM_MUTE, false);
            previewVolume();
            continue;
        }
        if (sel == ITEM_MUTE && (pressed & (FACES_LEFT | FACES_RIGHT | FACES_A))) {
            settingsSetMuted(!settingsMuted());
            drawRow(ITEM_MUTE, true);
            drawRow(ITEM_VOLUME, false);
            continue;
        }
        if (sel == ITEM_AMBILIGHT && (pressed & (FACES_LEFT | FACES_RIGHT | FACES_A))) {
            settingsSetAmbilight(!settingsAmbilight());
            drawRow(ITEM_AMBILIGHT, true);
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
    settingsFlush();
    log_printf("settings: close (brightness=%d charge_mode=%d)\n", settingsBrightnessLevel(), settingsChargeMode());
}
