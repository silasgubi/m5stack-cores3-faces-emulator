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
// 4. Geometry: a 16 px wide strip at each edge of the picture, cut into 5 rectangles, one per
//    LED (48 lines each on the NES). The LED shows the dominant color of its rectangle,
//    background included (an earlier version looked 64 px in, split each rectangle in 3 and
//    favored what departed from the background; it lit colors that were not at the edge).
//
// Two frame sources, same logic (templates over the source, so the NES path does not change):
// - Paletted (Nofrendo): lines of 8-bit indices + osd_pal_rgb palette.
// - RGB565 (Game Boy, ledEffectsSetSource565): proportional (160x144: 10 px strips, rectangles
//   of ~29 lines), bucket computed from the 565 value in integers (small cache: the Game Boy
//   has few colors).
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
// 0-7 blank). Rectangles of 48 lines cover the whole height, so the ground lands in the bottom one.
static constexpr int NES_W = 256, NES_H = 240;
static constexpr int EDGE_W = 16;                   // strip width (Game Boy: proportional, w / 16)
static constexpr int EDGE_X0 = 8, EDGE_X1 = EDGE_X0 + EDGE_W;        // left edge of the game
static constexpr int EDGE_X2 = NES_W - 8 - EDGE_W, EDGE_X3 = NES_W - 8;  // right edge

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
    int stepX, stepY;  // 1 pixel every step (NES: 2 and 4; Game Boy 160x144: 1 and 3)
    int x0[2], x1[2];  // edge columns on each side
};
static Geometry s_geo = {NES_W, NES_H, 2, 4, {EDGE_X2, EDGE_X0}, {EDGE_X3, EDGE_X1}};
static const uint16_t *s_src565 = nullptr;
static int s_srcPitch = 0;

static inline int rightIndex(int h) { return h; }
static inline int leftIndex(int h) { return LED_COUNT - 1 - h; }
static inline int bandY(int h) { return h * s_geo.h / LEVELS; }

static void setGeometry(int w, int h, bool nes) {
    s_geo.w = w;
    s_geo.h = h;
    // ~8 columns per strip and ~60 lines: ~100 samples per rectangle, ~1000 per frame.
    const int edge = nes ? EDGE_W : max(8, w / 16);
    s_geo.stepX = max(1, edge / 8);
    s_geo.stepY = max(1, (h + 59) / 60);
    if (nes) {
        s_geo.x0[0] = EDGE_X2; s_geo.x1[0] = EDGE_X3;
        s_geo.x0[1] = EDGE_X0; s_geo.x1[1] = EDGE_X1;
    } else {
        s_geo.x0[0] = w - edge; s_geo.x1[0] = w;
        s_geo.x0[1] = 0;        s_geo.x1[1] = edge;
    }
}

void ledEffectsSetSource565(const uint16_t *fb, int width, int height, int pitchPixels) {
    // Source too small for 5 rectangles and an 8 px strip: back to the NES source.
    if (!fb || width < 64 || height < LEVELS) {
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

void ledEffectsShowAmbilight() {
    s_volumeUntilMs = 0;
    s_brightUntilMs = 0;
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

// Dominant color of a rectangle (see the file header): the hue bucket with the most
// pixels, averaged over its pixels; dark or gray when the rectangle has little color.
template <typename S>
static int pickColor(const S &src, const Region &r, uint32_t out[3]) {
    const uint16_t *count = r.count;
    int total = 0, colored = 0, best = -1;
    for (int b = 0; b < BUCKETS; ++b) total += count[b];
    for (int b = 2; b < BUCKETS; ++b) {
        colored += count[b];
        if (count[b] && (best < 0 || count[b] > count[best])) best = b;
    }
    if (best < 0 || colored * 100 < total * COLORED_MIN_PCT) {
        best = count[BUCKET_GRAY] > count[BUCKET_DARK] ? BUCKET_GRAY : BUCKET_DARK;
    }

    uint32_t sum[3] = {0, 0, 0}, n = 0;
    for (int y = r.y0; y < r.y1; y += s_geo.stepY)
        for (int x = r.x0; x < r.x1; x += s_geo.stepX)
            if (src.addIfBucket(x, y, best, sum)) n++;
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
    static uint32_t s_logAt = 0;
    const bool doLog = (int32_t)(millis() - s_logAt) >= 0;
    if (doLog) s_logAt = millis() + 30000;
    char line[120];
    int pos = doLog ? snprintf(line, sizeof(line), "led |") : 0;

    // One rectangle per LED: side 0 = right, 1 = left; h = 0 near the screen.
    for (int h = 0; h < LEVELS; ++h) {
        for (int side = 0; side < 2; ++side) {
            Region r;
            r.x0 = s_geo.x0[side];
            r.x1 = s_geo.x1[side];
            r.y0 = bandY(h);
            r.y1 = bandY(h + 1);
            countRegion(src, r);
            uint32_t rgb[3];
            const int chosen = pickColor(src, r, rgb);
            smoothTo(side ? leftIndex(h) : rightIndex(h), rgb);
            if (doLog && pos < (int)sizeof(line) - 12)
                pos += snprintf(line + pos, sizeof(line) - pos, " %c%d:b%d", side ? 'L' : 'R', h, chosen);
        }
    }
    if (doLog) log_printf("%s\n", line);
}

void ledEffectsFrame() {
    initOnce();
    const uint32_t now = millis();
    if (s_volumeUntilMs && (int32_t)(now - s_volumeUntilMs) >= 0) s_volumeUntilMs = 0;
    if (s_brightUntilMs && (int32_t)(now - s_brightUntilMs) >= 0) s_brightUntilMs = 0;
    ledsSetBrightness(settingsLedBrightness());
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
