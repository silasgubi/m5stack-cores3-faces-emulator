// "Screen edge" effect (ambilight style), volume and brightness levels and battery alert on the
// base strip.
//
// Measured physical order ("U" chain, GRB strip): right side 0 (near the screen)
// to 4 (near the buttons); left side 5 (near the buttons) to 9 (near the screen).
// At height h (0 = near the screen) sit index h on the right and 9 - h on the left.
//
// How each LED color is chosen (simpler approaches turned everything white):
// 1. Each sampled pixel falls into a hue bucket (12 of 30 degrees), or into "dark" or "gray".
//    On the NES the bucket is tabulated per palette index, so it costs one lookup per pixel.
//    That way the orange and brown of the bricks vote together and beat the green of the bush.
// 2. RGB average of the pixels in the winning bucket (averaging everything gave a pale tone).
// 3. Stretch the saturation (the classic NES sky (111,132,255) is lavender; on the LED it
//    turns whitish purple) and apply gamma 2.2, because the LED at low current washes out colors.
// 4. Background: the bucket seen most along the whole edge (the sky). Each LED covers 1/5 of
//    the height (48 lines on the NES), sampled in 3 strips (16 lines); the LED shows the strip
//    that departs most from the background (pipe and bush are 16 px tall and fill a strip;
//    diluted over 48 lines they never went above 17%). If no strip has OBJECT_MIN_PCT of
//    another color, the sum of the 3 is used.
//
// Two frame sources, same logic (templates over the source, so the NES path does not change):
// - Paletted (Nofrendo): lines of 8-bit indices + osd_pal_rgb palette.
// - RGB565 (Game Boy, ledEffectsSetSource565): bands and strips proportional to the height
//   (160x144: 5 bands of ~29 lines, strips of ~10), edges = 25% of the width on each side,
//   bucket computed from the 565 value in integers (small cache: the Game Boy has few colors).
//
// Priority on each side: level overlay (1.5 s after a change: volume on the right, brightness on
// the left, mirroring the bars on the screen) > battery alert > edge effect. The other side keeps
// the effect, so the edge effect also runs while an overlay shows.

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "led_effects.h"
#include "leds.h"
#include "settings.h"
#include "hud.h"  // BRIGHTNESS_ACTIVE_MS

extern "C" {
const uint8_t **osd_frame_lines(void);
extern uint8_t osd_pal_rgb[256][3];
extern volatile uint32_t osd_pal_version;
}

static constexpr int LEVELS = 5;  // LEDs per side
static constexpr uint32_t LED_BAR_MS = 1500;

// Paletted NES (256x240): skips the 8 outermost columns (many games leave columns
// 0-7 blank). Bands of 48 lines cover the whole screen, so the ground lands in the bottom one.
static constexpr int NES_W = 256, NES_H = 240;
static constexpr int EDGE_X0 = 8, EDGE_X1 = 72;     // left edge of the game
static constexpr int EDGE_X2 = 184, EDGE_X3 = 248;  // right edge
static constexpr int STRIPS = 3;                    // strips per band (NES: 3 x 16 lines)

// Assumption about the Game Boy RGB565 framebuffer: it is in the panel byte order
// (bytes swapped relative to the ESP32 native RGB565), same as the NES palette in
// cgs/nes_osd.c, so the blit goes straight to DMA without setSwapBytes. If the Game Boy
// port writes RGB565 in CPU byte order, just set this to false.
static constexpr bool SRC565_BYTES_SWAPPED = true;

static constexpr int SMOOTH_DIV = 3;  // each update moves 1/3 of the way to the new color

// Buckets: 0 dark, 1 gray, 2..13 hue (30 degrees each).
static constexpr int BUCKET_DARK = 0, BUCKET_GRAY = 1, BUCKETS = 14;
static constexpr int DARK_MAX = 40;      // max(r,g,b) below this is "dark"
static constexpr int GRAY_SPREAD = 30;   // max-min below this is "gray"
static constexpr int COLORED_MIN_PCT = 25;  // less color than this in the sample: use dark/gray
static constexpr int OBJECT_MIN_PCT = 25;   // a non-background color with this share of the strip beats the background

static constexpr uint32_t BATTERY_PULSE_MS = 1000;  // red pulse period (~1 Hz)
static constexpr uint8_t BATTERY_PULSE_FLOOR = 12;  // never fully off: reads as a pulse, not a blink

static uint8_t GAMMA[256];
static uint8_t s_bucketOf[256];  // bucket per palette index (NES)
static uint32_t s_palSeen = 0xFFFFFFFF;

static uint32_t s_volumeUntilMs = 0, s_brightUntilMs = 0;  // 0: overlay off
static uint8_t s_volume = 0;
static bool s_muted = false;
static int s_brightLit = 0;  // LEDs lit by the brightness overlay
static int s_batteryLevel = 0;        // 0 none, 1 bottom LEDs (4 and 5), 2 all
static uint16_t s_cur[LED_COUNT][3];  // smoothed color, 8.8 fixed point so small steps are not lost

// Sampling geometry, derived from the source. [0] = right side, [1] = left.
struct Geometry {
    int w, h;
    int stepX, stepY;  // 1 pixel every step (NES: 4 and 4; Game Boy 160x144: 2 and 3)
    int x0[2], x1[2];  // edge columns on each side
};
static Geometry s_geo = {NES_W, NES_H, 4, 4, {EDGE_X2, EDGE_X0}, {EDGE_X3, EDGE_X1}};
static const uint16_t *s_src565 = nullptr;
static int s_srcPitch = 0;

static inline int rightIndex(int h) { return h; }
static inline int leftIndex(int h) { return LED_COUNT - 1 - h; }
static inline int bandY(int h) { return h * s_geo.h / LEVELS; }

static void setGeometry(int w, int h, bool nes) {
    s_geo.w = w;
    s_geo.h = h;
    // Same density as the NES (~2000 samples per frame): ~16 columns per side and ~60 lines.
    s_geo.stepX = max(1, w / 64);
    s_geo.stepY = max(1, (h + 59) / 60);
    if (nes) {
        s_geo.x0[0] = EDGE_X2; s_geo.x1[0] = EDGE_X3;
        s_geo.x0[1] = EDGE_X0; s_geo.x1[1] = EDGE_X1;
    } else {
        const int q = w / 4;
        s_geo.x0[0] = w - q; s_geo.x1[0] = w;
        s_geo.x0[1] = 0;     s_geo.x1[1] = q;
    }
}

void ledEffectsSetSource565(const uint16_t *fb, int width, int height, int pitchPixels) {
    // Source too small for 5 bands x 3 strips and 1/4 of the width: back to the NES source.
    if (!fb || width < 64 || height < LEVELS * STRIPS) {
        s_src565 = nullptr;
        setGeometry(NES_W, NES_H, true);
        return;
    }
    s_src565 = fb;
    s_srcPitch = pitchPixels > 0 ? pitchPixels : width;
    setGeometry(width, height, false);
}

// Integer hue (0..359) from 8-bit RGB; same rule as the float version used for the palette.
static int bucketOfRgb(int r, int g, int b) {
    const int mx = max(r, max(g, b)), mn = min(r, min(g, b));
    if (mx < DARK_MAX) return BUCKET_DARK;
    if (mx - mn < GRAY_SPREAD) return BUCKET_GRAY;
    const int d = mx - mn;
    int h;
    if (mx == r) { h = 60 * (g - b) / d; if (h < 0) h += 360; }
    else if (mx == g) h = 120 + 60 * (b - r) / d;
    else h = 240 + 60 * (r - g) / d;
    return 2 + (h / 30) % 12;
}

static inline int r8of565(uint16_t v) { const int c = v >> 11; return (c << 3) | (c >> 2); }
static inline int g8of565(uint16_t v) { const int c = (v >> 5) & 0x3F; return (c << 2) | (c >> 4); }
static inline int b8of565(uint16_t v) { const int c = v & 0x1F; return (c << 3) | (c >> 2); }

// Direct-mapped cache of 256 entries: the Game Boy shows 4 shades (DMG) or up to 56 colors
// (GBC), so almost every pixel hits and the hue is only computed for the first samples.
static uint16_t s_c565Key[256];
static uint8_t s_c565Val[256];  // 0xFF = empty

static inline int bucketOf565(uint16_t v) {
    const int i = (v ^ (v >> 7) ^ (v >> 13)) & 0xFF;
    if (s_c565Key[i] == v && s_c565Val[i] != 0xFF) return s_c565Val[i];
    const int b = bucketOfRgb(r8of565(v), g8of565(v), b8of565(v));
    s_c565Key[i] = v;
    s_c565Val[i] = (uint8_t)b;
    return b;
}

static void initOnce() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    for (int i = 0; i < 256; ++i) GAMMA[i] = (uint8_t)(powf(i / 255.0f, 2.2f) * 255.0f + 0.5f);
    memset(s_c565Val, 0xFF, sizeof(s_c565Val));
}

// Bucket per NES palette index. Kept in float as it always was: the NES path does not change.
static void buildPaletteBuckets() {
    for (int i = 0; i < 256; ++i) {
        const int r = osd_pal_rgb[i][0], g = osd_pal_rgb[i][1], b = osd_pal_rgb[i][2];
        const int mx = max(r, max(g, b)), mn = min(r, min(g, b));
        if (mx < DARK_MAX) { s_bucketOf[i] = BUCKET_DARK; continue; }
        if (mx - mn < GRAY_SPREAD) { s_bucketOf[i] = BUCKET_GRAY; continue; }
        float h;  // hue in degrees, 0..360
        const float d = (float)(mx - mn);
        if (mx == r) h = 60.0f * fmodf((g - b) / d, 6.0f);
        else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
        else h = 60.0f * ((r - g) / d + 4.0f);
        if (h < 0) h += 360.0f;
        s_bucketOf[i] = 2 + (int)(h / 30.0f) % 12;
    }
}

// Frame sources. bucket(): the pixel's bucket; addIfBucket(): adds the pixel RGB if it is
// in the requested bucket (a single read, so the 565 value is not decoded twice).
struct PalSource {
    const uint8_t **lines;
    inline int bucket(int x, int y) const { return s_bucketOf[lines[y][x]]; }
    inline bool addIfBucket(int x, int y, int want, uint32_t sum[3]) const {
        const uint8_t idx = lines[y][x];
        if (s_bucketOf[idx] != want) return false;
        sum[0] += osd_pal_rgb[idx][0];
        sum[1] += osd_pal_rgb[idx][1];
        sum[2] += osd_pal_rgb[idx][2];
        return true;
    }
};

struct Src565 {
    const uint16_t *fb;
    int pitch;
    inline uint16_t at(int x, int y) const {
        const uint16_t v = fb[y * pitch + x];
        return SRC565_BYTES_SWAPPED ? (uint16_t)((v >> 8) | (v << 8)) : v;
    }
    inline int bucket(int x, int y) const { return bucketOf565(at(x, y)); }
    inline bool addIfBucket(int x, int y, int want, uint32_t sum[3]) const {
        const uint16_t v = at(x, y);
        if (bucketOf565(v) != want) return false;
        sum[0] += r8of565(v);
        sum[1] += g8of565(v);
        sum[2] += b8of565(v);
        return true;
    }
};

// Overlay end time, never 0 (0 means off).
static uint32_t overlayUntil() { return (millis() + LED_BAR_MS) | 1; }

void ledEffectsShowVolume(uint8_t volume, bool muted) {
    s_volume = volume;
    s_muted = muted;
    s_volumeUntilMs = overlayUntil();
    s_brightUntilMs = 0;
}

void ledEffectsShowBrightness(int level, int levels) {
    if (levels < 1) return;
    s_brightLit = ((constrain(level, 0, levels - 1) + 1) * LEVELS + levels - 1) / levels;
    s_brightUntilMs = (millis() + BRIGHTNESS_ACTIVE_MS) | 1;
    s_volumeUntilMs = 0;
}

void ledEffectsSetBatteryAlert(int level) { s_batteryLevel = constrain(level, 0, 2); }

// A level on one side, lit from the bottom (near the buttons) up. The smoothed color of
// those LEDs is reset, so the edge effect fades back in from black when the overlay ends.
static void drawLevel(bool right, int lit, uint8_t r, uint8_t g) {
    for (int h = 0; h < LEVELS; ++h) {
        const bool on = (LEVELS - 1 - h) < lit;
        const int i = right ? rightIndex(h) : leftIndex(h);
        ledsSet(i, on ? r : 0, on ? g : 0, 0);
        for (int c = 0; c < 3; ++c) s_cur[i][c] = 0;
    }
}

static void drawVolume() {
    if (s_muted)
        drawLevel(true, LEVELS, 255, 0);  // all red, like the red bar on the screen
    else
        drawLevel(true, (s_volume * LEVELS + 254) / 255, 0, 255);
}

// Red pulse over the effect: triangle ramp through gamma, so the rise looks linear to the
// eye. It only overrides the output; s_cur keeps following the effect and fades back after.
static void applyBatteryAlert() {
    if (!s_batteryLevel) return;
    const int half = BATTERY_PULSE_MS / 2;
    const int ph = (int)(millis() % BATTERY_PULSE_MS);
    const int v = (ph < half ? ph : (int)BATTERY_PULSE_MS - ph) * 255 / half;
    const uint8_t r = max(GAMMA[v], BATTERY_PULSE_FLOOR);
    if (s_batteryLevel >= 2) {
        for (int i = 0; i < LED_COUNT; ++i) ledsSet(i, r, 0, 0);
    } else {
        ledsSet(rightIndex(LEVELS - 1), r, 0, 0);  // index 4
        ledsSet(leftIndex(LEVELS - 1), r, 0, 0);   // index 5
    }
}

struct Region { int x0, x1, y0, y1; uint16_t count[BUCKETS]; };

template <typename S>
static void countRegion(const S &src, Region &r) {
    memset(r.count, 0, sizeof(r.count));
    for (int y = r.y0; y < r.y1; y += s_geo.stepY)
        for (int x = r.x0; x < r.x1; x += s_geo.stepX) r.count[src.bucket(x, y)]++;
}

// Representative color of a set of strips (see the file header). count = sum of the
// strips' buckets; background = background bucket of the whole edge, or -1 if none.
template <typename S>
static int pickColor(const S &src, const Region *strips, int nStrips, const uint16_t count[BUCKETS],
                     int background, uint32_t out[3]) {
    int total = 0, colored = 0, best = -1, bestOther = -1;
    for (int b = 0; b < BUCKETS; ++b) total += count[b];
    for (int b = 2; b < BUCKETS; ++b) {
        colored += count[b];
        if (count[b] && (best < 0 || count[b] > count[best])) best = b;
        if (b != background && count[b] && (bestOther < 0 || count[b] > count[bestOther])) bestOther = b;
    }
    if (bestOther >= 0 && count[bestOther] * 100 >= total * OBJECT_MIN_PCT) best = bestOther;
    if (best < 0 || colored * 100 < total * COLORED_MIN_PCT) {
        best = count[BUCKET_GRAY] > count[BUCKET_DARK] ? BUCKET_GRAY : BUCKET_DARK;
    }

    // Same lines as the count (strip by strip): on the Game Boy the strip is not a multiple of the step.
    uint32_t sum[3] = {0, 0, 0}, n = 0;
    for (int t = 0; t < nStrips; ++t) {
        const Region &r = strips[t];
        for (int y = r.y0; y < r.y1; y += s_geo.stepY)
            for (int x = r.x0; x < r.x1; x += s_geo.stepX)
                if (src.addIfBucket(x, y, best, sum)) n++;
    }
    if (!n) { out[0] = out[1] = out[2] = 0; return best; }
    int rgb[3];
    for (int c = 0; c < 3; ++c) rgb[c] = sum[c] / n;

    // Stretch the saturation keeping brightness (max) and hue; gray and dark stay as they are.
    const int mx = max(rgb[0], max(rgb[1], rgb[2])), mn = min(rgb[0], min(rgb[1], rgb[2]));
    if (best >= 2 && mx - mn > 0) {
        for (int c = 0; c < 3; ++c) rgb[c] = (rgb[c] - mn) * mx / (mx - mn);
    }
    for (int c = 0; c < 3; ++c) out[c] = GAMMA[rgb[c]];
    return best;
}

// Share (%) of the largest non-background color, to pick the most "interesting" strip.
static int objectPct(const Region &r, int background) {
    int total = 0, best = 0;
    for (int b = 0; b < BUCKETS; ++b) total += r.count[b];
    for (int b = 2; b < BUCKETS; ++b) if (b != background && r.count[b] > best) best = r.count[b];
    return total ? best * 100 / total : 0;
}

static void smoothTo(int index, const uint32_t target[3]) {
    for (int c = 0; c < 3; ++c) {
        const int32_t t = (int32_t)target[c] << 8, cur = s_cur[index][c];
        // The last step lands exactly on the target: integer division stopped 2 short when
        // rising (output target-1) and above it when falling, so the same image gave different colors.
        int32_t step = (t - cur) / SMOOTH_DIV;
        if (step == 0) step = t - cur;
        s_cur[index][c] = (uint16_t)(cur + step);
    }
    ledsSet(index, s_cur[index][0] >> 8, s_cur[index][1] >> 8, s_cur[index][2] >> 8);
}

template <typename S>
static void edgeEffect(const S &src) {
    // regions[(2h + side) * STRIPS + strip]: side 0 = right, 1 = left
    static Region regions[LEVELS * 2 * STRIPS];
    uint32_t global[BUCKETS] = {0};
    for (int h = 0; h < LEVELS; ++h) {
        const int by0 = bandY(h), by1 = bandY(h + 1);
        for (int side = 0; side < 2; ++side) {
            for (int t = 0; t < STRIPS; ++t) {
                Region &r = regions[(2 * h + side) * STRIPS + t];
                r.x0 = s_geo.x0[side];
                r.x1 = s_geo.x1[side];
                r.y0 = by0 + t * (by1 - by0) / STRIPS;
                r.y1 = by0 + (t + 1) * (by1 - by0) / STRIPS;
                countRegion(src, r);
                for (int b = 0; b < BUCKETS; ++b) global[b] += r.count[b];
            }
        }
    }
    int background = -1;
    for (int b = 2; b < BUCKETS; ++b) {
        if (global[b] && (background < 0 || global[b] > global[background])) background = b;
    }

    static uint32_t s_logAt = 0;
    const bool doLog = (int32_t)(millis() - s_logAt) >= 0;
    if (doLog) s_logAt = millis() + 30000;
    char line[160];
    int pos = doLog ? snprintf(line, sizeof(line), "led bg=%d |", background) : 0;

    for (int h = 0; h < LEVELS; ++h) {
        for (int side = 0; side < 2; ++side) {
            const Region *strips = &regions[(2 * h + side) * STRIPS];
            // Strip with the most non-background color; if none reaches the minimum, sum all 3.
            int bestT = 0, bestPct = 0;
            for (int t = 0; t < STRIPS; ++t) {
                const int p = objectPct(strips[t], background);
                if (p > bestPct) { bestPct = p; bestT = t; }
            }
            uint16_t merged[BUCKETS];
            const Region *pick = strips;
            int nPick = STRIPS;
            if (bestPct >= OBJECT_MIN_PCT) {
                pick = &strips[bestT];
                nPick = 1;
                memcpy(merged, pick->count, sizeof(merged));
            } else {
                memcpy(merged, strips[0].count, sizeof(merged));
                for (int t = 1; t < STRIPS; ++t)
                    for (int b = 0; b < BUCKETS; ++b) merged[b] += strips[t].count[b];
            }
            uint32_t rgb[3];
            const int chosen = pickColor(src, pick, nPick, merged, background, rgb);
            smoothTo(side ? leftIndex(h) : rightIndex(h), rgb);
            if (doLog && pos < (int)sizeof(line) - 20)
                pos += snprintf(line + pos, sizeof(line) - pos, " %c%d:b%d/%d%%", side ? 'L' : 'R', h, chosen, bestPct);
        }
    }
    if (doLog) log_printf("%s\n", line);
}

void ledEffectsFrame() {
    initOnce();
    const uint32_t now = millis();
    if (s_volumeUntilMs && (int32_t)(now - s_volumeUntilMs) >= 0) s_volumeUntilMs = 0;
    if (s_brightUntilMs && (int32_t)(now - s_brightUntilMs) >= 0) s_brightUntilMs = 0;
    // Ambilight off: black strip each frame (ledsSet ignores unchanged colors, so this is
    // cheap); the overlays and the battery pulse below still draw on top.
    const bool ambilight = settingsAmbilight();
    if (!ambilight) ledsClear();
    // ~30 Hz is enough for the eye and spares half the frames this cost.
    static uint8_t s_skip = 0;
    if (ambilight && !(++s_skip & 1)) {
        if (s_src565) {
            const Src565 src = {s_src565, s_srcPitch};
            edgeEffect(src);
        } else {
            const uint8_t **lines = osd_frame_lines();
            if (lines) {
                if (s_palSeen != osd_pal_version) {
                    s_palSeen = osd_pal_version;
                    buildPaletteBuckets();
                }
                const PalSource src = {lines};
                edgeEffect(src);
            }
        }
    }
    applyBatteryAlert();  // every frame, so the pulse is smooth; drawn over the effect
    if (s_volumeUntilMs) drawVolume();  // the overlays win on their side
    if (s_brightUntilMs) drawLevel(false, s_brightLit, 0, 255);
}
