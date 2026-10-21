// Battery icons (see battery_icon.h). Every pixel of the icon area is written on each
// call: outline, a 1 px gap in the background color, then the fill and the empty part,
// so no clear-then-draw pass is needed and the icon does not blink when refreshed.

#include <M5Unified.h>
#include "battery_icon.h"

static constexpr uint16_t OUTLINE = TFT_WHITE;
static constexpr uint16_t OUTLINE_ABSENT = TFT_DARKGREY;
static constexpr uint16_t CROSS = TFT_RED;
static constexpr uint16_t BOLT = TFT_WHITE;
static constexpr uint16_t FILL_OK = TFT_GREEN;
static constexpr uint16_t FILL_LOW = TFT_ORANGE;  // amber, 255/180/0
static constexpr uint16_t FILL_CRIT = TFT_RED;
static constexpr int LOW_PCT = 40, CRIT_PCT = 20;

// Lightning bolts as one horizontal run per row (columns x0..x1 inclusive). Runs are
// used instead of a bitmap so a bolt costs one SPI line write per row.
struct Run {
    int8_t x0, x1;
};
struct Bolt {
    const Run *rows;
    int w, h;
};
static constexpr Run BOLT_SMALL_ROWS[] = {  // 7x9, fits the 9 px inside the horizontal icon
    {5, 6}, {4, 5}, {3, 4}, {2, 3}, {1, 6}, {4, 5}, {3, 4}, {2, 3}, {1, 2},
};
static constexpr Run BOLT_LARGE_ROWS[] = {  // 8x10, for the taller vertical icon
    {5, 7}, {4, 6}, {3, 5}, {2, 4}, {1, 7}, {0, 6}, {3, 5}, {2, 4}, {1, 3}, {0, 2},
};
static constexpr Bolt BOLT_SMALL = {BOLT_SMALL_ROWS, 7, 9};
static constexpr Bolt BOLT_LARGE = {BOLT_LARGE_ROWS, 8, 10};

static uint16_t fillColor(int percent) {
    return percent >= LOW_PCT ? FILL_OK : percent >= CRIT_PCT ? FILL_LOW : FILL_CRIT;
}

// Fill length in pixels, rounded; at least 1 px above 0 % so a nearly empty battery
// still shows some color.
static int fillLength(int len, int percent) {
    const int n = (len * percent + 50) / 100;
    return (percent > 0 && n == 0) ? 1 : n;
}

// Bolt centered in the area inside the outline (ix, iy, iw, ih), with a 1 px halo in
// the background color so it stays readable over any fill color. The halo is clipped
// to that area, so the outline is never touched.
static void drawBolt(int ix, int iy, int iw, int ih, uint16_t bg) {
    const Bolt *b = nullptr;
    if (iw >= BOLT_LARGE.w + 2 && ih >= BOLT_LARGE.h + 2)
        b = &BOLT_LARGE;
    else if (iw >= BOLT_SMALL.w + 2 && ih >= BOLT_SMALL.h)
        b = &BOLT_SMALL;
    if (!b) return;  // too small: the fill alone has to do
    const int bx = ix + (iw - b->w) / 2, by = iy + (ih - b->h) / 2;

    // Halo: each row covers the runs of itself and both neighbors, widened by 1 px
    // (an 8-neighbor dilation). The strokes are diagonal and connected, so the union
    // of three adjacent runs is still a single run.
    for (int r = -1; r <= b->h; ++r) {
        const int y = by + r;
        if (y < iy || y >= iy + ih) continue;
        int lo = 127, hi = -128;
        for (int k = r - 1; k <= r + 1; ++k) {
            if (k < 0 || k >= b->h) continue;
            lo = min(lo, (int)b->rows[k].x0);
            hi = max(hi, (int)b->rows[k].x1);
        }
        const int x0 = max(bx + lo - 1, ix), x1 = min(bx + hi + 1, ix + iw - 1);
        if (x0 <= x1) M5.Display.drawFastHLine(x0, y, x1 - x0 + 1, bg);
    }
    for (int r = 0; r < b->h; ++r)
        M5.Display.drawFastHLine(bx + b->rows[r].x0, by + r, b->rows[r].x1 - b->rows[r].x0 + 1, BOLT);
}

// Body without the tip: outline, 1 px gap, fill (from the left when horizontal, from
// the bottom otherwise) and the overlay. Covers exactly (x, y, w, h).
static void drawBody(int x, int y, int w, int h, bool horizontal, int percent, bool charging, bool present,
                     uint16_t bg) {
    const int fx = x + 2, fy = y + 2, fw = w - 4, fh = h - 4;
    M5.Display.drawRect(x, y, w, h, present ? OUTLINE : OUTLINE_ABSENT);
    M5.Display.drawRect(x + 1, y + 1, w - 2, h - 2, bg);
    if (!present) {
        M5.Display.fillRect(fx, fy, fw, fh, bg);
        M5.Display.drawLine(fx, fy, fx + fw - 1, fy + fh - 1, CROSS);
        M5.Display.drawLine(fx + fw - 1, fy, fx, fy + fh - 1, CROSS);
        return;
    }
    percent = constrain(percent, 0, 100);
    const uint16_t color = fillColor(percent);
    if (horizontal) {
        const int n = fillLength(fw, percent);
        M5.Display.fillRect(fx, fy, n, fh, color);
        M5.Display.fillRect(fx + n, fy, fw - n, fh, bg);
    } else {
        const int n = fillLength(fh, percent);
        M5.Display.fillRect(fx, fy, fw, fh - n, bg);
        M5.Display.fillRect(fx, fy + fh - n, fw, n, color);
    }
    if (charging) drawBolt(x + 1, y + 1, w - 2, h - 2, bg);
}

void drawBatteryIcon(int x, int y, int percent, bool charging, bool present, uint16_t bg) {
    static constexpr int BODY_W = 22, TIP_W = BATTERY_ICON_W - BODY_W, TIP_H = 5;
    static constexpr int TIP_Y = (BATTERY_ICON_H - TIP_H) / 2;
    M5.Display.startWrite();
    drawBody(x, y, BODY_W, BATTERY_ICON_H, true, percent, charging, present, bg);
    const int tx = x + BODY_W;
    M5.Display.fillRect(tx, y, TIP_W, TIP_Y, bg);
    M5.Display.fillRect(tx, y + TIP_Y, TIP_W, TIP_H, present ? OUTLINE : OUTLINE_ABSENT);
    M5.Display.fillRect(tx, y + TIP_Y + TIP_H, TIP_W, BATTERY_ICON_H - TIP_Y - TIP_H, bg);
    M5.Display.endWrite();
}

void drawBatteryIconVertical(int x, int y, int w, int h, int percent, bool charging, bool present,
                             uint16_t bg) {
    M5.Display.startWrite();
    if (w < BATTERY_ICON_V_MIN_W || h < BATTERY_ICON_V_MIN_H) {
        M5.Display.fillRect(x, y, w, h, bg);
        M5.Display.endWrite();
        return;
    }
    // Tip about half the width, with an even margin on both sides so it stays centered.
    const int tipH = max(2, h / 12);
    int tipW = w / 2;
    if ((w - tipW) & 1) tipW--;
    const int tipX = x + (w - tipW) / 2;
    M5.Display.fillRect(x, y, tipX - x, tipH, bg);
    M5.Display.fillRect(tipX, y, tipW, tipH, present ? OUTLINE : OUTLINE_ABSENT);
    M5.Display.fillRect(tipX + tipW, y, x + w - tipX - tipW, tipH, bg);
    drawBody(x, y + tipH, w, h - tipH, false, percent, charging, present, bg);
    M5.Display.endWrite();
}
