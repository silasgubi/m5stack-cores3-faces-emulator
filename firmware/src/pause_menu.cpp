// In-game pause menu (see pause_menu.h). A box over the middle of the picture: the next
// game frame redraws it, since both pictures (NES 256 px, Game Boy 266 px) cover it.

#include <M5Unified.h>
#include <sys/stat.h>
#include "pause_menu.h"
#include "faces_pad.h"
#include "menu.h"
#include "state_store.h"
#include "ui_text.h"

enum Item : int { ITEM_CONTINUE, ITEM_SAVE, ITEM_LOAD, ITEM_QUIT, ITEM_COUNT };
static_assert(ITEM_COUNT == (int)TXT_PAUSE_ITEM_COUNT, "one label per pause row (ui_text.h)");

static constexpr int BOX_X = 40, BOX_Y = 36, BOX_W = 240, BOX_H = 168;
static constexpr int TITLE_H = 20;
static constexpr int ROW_Y0 = BOX_Y + TITLE_H + 6, ROW_PITCH = 24, ROW_H = 20;
static constexpr int INFO_Y = BOX_Y + BOX_H - 36, MSG_Y = INFO_Y + 12, FOOT_Y = MSG_Y + 12;
static constexpr uint16_t SEL_BG = TFT_DARKGREEN;
static constexpr uint32_t SAVED_MS = 700, LOADED_MS = 400;  // result shown before the game resumes

static PauseHandlers s_handlers = {};

void pauseSetHandlers(const PauseHandlers &h) { s_handlers = h; }

static bool fileExists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

static void drawRow(int item, bool sel, bool enabled) {
    const int y = ROW_Y0 + item * ROW_PITCH;
    const uint16_t bg = sel ? SEL_BG : TFT_BLACK;
    M5.Display.fillRect(BOX_X + 4, y, BOX_W - 8, ROW_H, bg);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(enabled ? TFT_WHITE : TFT_DARKGREY, bg);
    M5.Display.setCursor(BOX_X + 12, y + (ROW_H - 16) / 2);
    M5.Display.print(TXT_PAUSE_ITEMS[item]);
}

static void drawLine(int y, const char *text, uint16_t color) {
    M5.Display.fillRect(BOX_X + 2, y, BOX_W - 4, 8, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setCursor(BOX_X + 12, y);
    M5.Display.print(text);
}

void pauseMenuRun() {
    if (!s_handlers.begin || !s_handlers.end) return;
    s_handlers.begin();
    log_printf("pause: open\n");

    char path[96];
    const bool storage = statePath(path, sizeof(path));
    bool hasState = storage && fileExists(path);
    auto enabled = [&](int item) {
        return (item != ITEM_SAVE || storage) && (item != ITEM_LOAD || hasState);
    };

    M5.Display.fillRect(BOX_X, BOX_Y, BOX_W, BOX_H, TFT_BLACK);
    M5.Display.drawRect(BOX_X, BOX_Y, BOX_W, BOX_H, TFT_WHITE);
    M5.Display.fillRect(BOX_X + 1, BOX_Y + 1, BOX_W - 2, TITLE_H, TFT_BLUE);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Display.setCursor(BOX_X + 12, BOX_Y + 3);
    M5.Display.print(TXT_PAUSED);
    int sel = ITEM_CONTINUE;
    for (int i = 0; i < ITEM_COUNT; ++i) drawRow(i, i == sel, enabled(i));
    drawLine(INFO_Y, !storage ? TXT_PAUSE_NO_STORAGE : stateOnCard() ? TXT_PAUSE_ON_CARD : TXT_PAUSE_ON_CONSOLE,
             storage ? TFT_LIGHTGREY : TFT_ORANGE);
    drawLine(FOOT_Y, TXT_PAUSE_FOOTER, TFT_DARKGREY);

    // The combo that opened the menu is still held: act only on presses after it is released.
    while (facesPadRead() & (FACES_SELECT | FACES_START)) delay(10);
    uint8_t prev = facesPadRead();
    for (;;) {
        const uint8_t pad = facesPadRead();
        const uint8_t pressed = pad & ~prev;
        prev = pad;
        if (pressed & (FACES_UP | FACES_DOWN)) {
            const int old = sel;
            sel = (sel + ((pressed & FACES_UP) ? -1 : 1) + ITEM_COUNT) % ITEM_COUNT;
            drawRow(old, false, enabled(old));
            drawRow(sel, true, enabled(sel));
        }
        if (pressed & FACES_B) break;
        if (pressed & (FACES_A | FACES_START)) {
            if (sel == ITEM_CONTINUE) break;
            if (sel == ITEM_QUIT) menuQuitToGameList();  // does not return
            if (sel == ITEM_SAVE && storage) {
                drawLine(MSG_Y, TXT_PAUSE_SAVING, TFT_YELLOW);
                const uint32_t t0 = millis();
                const bool ok = s_handlers.save(path);
                log_printf("pause: save %s (%s, %lu ms)\n", ok ? "ok" : "FAILED", path, (unsigned long)(millis() - t0));
                drawLine(MSG_Y, ok ? TXT_PAUSE_SAVED : TXT_PAUSE_FAILED, ok ? TFT_GREEN : TFT_RED);
                if (ok) {
                    delay(SAVED_MS);
                    break;
                }
                hasState = fileExists(path);
                drawRow(ITEM_LOAD, false, enabled(ITEM_LOAD));
            } else if (sel == ITEM_LOAD && hasState) {
                const bool ok = s_handlers.load(path);
                log_printf("pause: load %s (%s)\n", ok ? "ok" : "FAILED", path);
                drawLine(MSG_Y, ok ? TXT_PAUSE_LOADED : TXT_PAUSE_FAILED, ok ? TFT_GREEN : TFT_RED);
                if (ok) {
                    delay(LOADED_MS);
                    break;
                }
            } else {
                drawLine(MSG_Y, sel == ITEM_LOAD ? TXT_PAUSE_NO_STATE : TXT_PAUSE_NO_STORAGE, TFT_ORANGE);
            }
        }
        delay(16);
    }
    // The button that closed the menu must not reach the game as a press.
    while (facesPadRead() & (FACES_A | FACES_B | FACES_START)) delay(10);
    log_printf("pause: close\n");
    s_handlers.end();
}
