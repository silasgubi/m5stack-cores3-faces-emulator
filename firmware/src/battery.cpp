// Battery: reading, log and low battery alert on the LEDs.
//
// AXP2101 read (I2C) every 10 s, off the per-frame path. Alert only while
// discharging: below 3550 mV the two bottom LEDs pulse red; below 3400 mV,
// all 10. 100 mV hysteresis so it does not flicker at the threshold (the voltage
// drops under speaker load and comes back). Charging, or above the threshold: no alert.
// Base rear switch on "O": the AXP2101 reads ~400 mV with no battery connected;
// below that (batteryPresent, power_mode.h) it is not treated as a low battery.
//
// Charging through the Faces base (its USB-C or the magnetic dock): the base has its own
// charger wired straight to the cell, invisible to the AXP2101 (no VBUS, "discharging").
// It is inferred from the voltage: with no USB on the CoreS3, the average of the last 30 s
// against the average of 60 to 120 s ago. Plugging a charger in lifts the cell voltage at
// once (charge current times the cell's internal resistance) and keeps it rising; while
// playing on battery it only falls. Takes about 1 to 2 min to show and to clear; near full
// the rise is small, so the last minutes of a charge may not show.

#include <M5Unified.h>
#include "battery.h"
#include "led_effects.h"
#include "power_mode.h"

static constexpr uint32_t READ_MS = 10000;  // I2C read
static constexpr uint32_t LOG_MS = 30000;   // "battery ..." line on serial
static constexpr int LOW_MV = 3550;         // level 1: bottom LEDs
static constexpr int CRIT_MV = 3400;        // level 2: all
static constexpr int HYST_MV = 100;         // only steps down HYST_MV above the threshold

static constexpr uint32_t SAMPLE_MIN_MS = 5000;           // the battery screen reads every 2 s
static constexpr uint32_t RECENT_MS = 30000;               // "now" window
static constexpr uint32_t OLD_FROM_MS = 60000, OLD_TO_MS = 120000;  // "before" window
static constexpr int BASE_ON_MV = 20;                      // rise that means charging
static constexpr int BASE_OFF_MV = -10;                    // fall that means it stopped
static constexpr int SAMPLES = 32;                         // > 120 s at one sample per 5 s

static uint32_t s_nextReadMs = 0, s_nextLogMs = 0;
static int s_mv = 0;
static m5::Power_Class::is_charging_t s_charging = m5::Power_Class::is_charging_t::charge_unknown;
static bool s_usb = false;
static bool s_baseCharging = false;
static int s_level = 0;

struct Sample {
    uint32_t ms;
    int mv;
};
static Sample s_samples[SAMPLES];
static int s_sampleCount = 0, s_sampleNext = 0;

// Average of the samples taken between fromMs and toMs ago; false if there are none.
static bool averageMv(uint32_t now, uint32_t fromMs, uint32_t toMs, int &avg) {
    long sum = 0;
    int n = 0;
    for (int i = 0; i < s_sampleCount; ++i) {
        const uint32_t age = now - s_samples[i].ms;
        if (age >= fromMs && age <= toMs) {
            sum += s_samples[i].mv;
            n++;
        }
    }
    if (n) avg = (int)(sum / n);
    return n > 0;
}

static void updateBaseCharging() {
    const uint32_t now = millis();
    const int last = s_sampleCount ? (s_sampleNext + SAMPLES - 1) % SAMPLES : -1;
    if (last < 0 || now - s_samples[last].ms >= SAMPLE_MIN_MS) {
        s_samples[s_sampleNext] = {now, s_mv};
        s_sampleNext = (s_sampleNext + 1) % SAMPLES;
        if (s_sampleCount < SAMPLES) s_sampleCount++;
    }
    if (s_usb || !batteryPresent(s_mv)) {  // the AXP2101 sees this one, or there is no cell
        s_baseCharging = false;
        return;
    }
    int recent, old;
    if (!averageMv(now, 0, RECENT_MS, recent) || !averageMv(now, OLD_FROM_MS, OLD_TO_MS, old)) return;
    const int rise = recent - old;
    if (rise >= BASE_ON_MV) s_baseCharging = true;
    if (rise <= BASE_OFF_MV) s_baseCharging = false;
}

// Steps up a level right away; steps down only once HYST_MV above the threshold.
static int nextLevel(int cur, int mv) {
    if (mv < CRIT_MV) return 2;
    if (cur == 2 && mv < CRIT_MV + HYST_MV) return 2;
    if (mv < LOW_MV) return 1;
    if (cur >= 1 && mv < LOW_MV + HYST_MV) return 1;
    return 0;
}

static const char *chargingName(m5::Power_Class::is_charging_t c) {
    return c == m5::Power_Class::is_charging      ? "charging"
           : c == m5::Power_Class::is_discharging ? "discharging"
                                                  : "unknown";
}

void batteryRead() {
    s_mv = M5.Power.getBatteryVoltage();
    s_charging = M5.Power.isCharging();
    // VBUS good flag (one register): tells "paused on USB" from "on battery" in the menu.
    s_usb = M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp2101 && M5.Power.Axp2101.isVBUS();
    updateBaseCharging();
}

int batteryMv() { return s_mv; }
bool batteryCharging() { return s_charging == m5::Power_Class::is_charging; }
bool batteryBaseCharging() { return s_baseCharging; }
bool batteryAnyCharging() { return batteryCharging() || s_baseCharging; }
bool batteryUsb() { return s_usb; }

bool batteryTick() {
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextReadMs) < 0) return false;
    s_nextReadMs = now + READ_MS;
    batteryRead();

    int level = 0;
    // Only while running on the cell: with charging set to Off the AXP2101 also reports
    // "discharging" on USB-C, where a low cell is no risk.
    if (s_charging == m5::Power_Class::is_discharging && !s_usb && !s_baseCharging && batteryPresent(s_mv))
        level = nextLevel(s_level, s_mv);
    if (level != s_level) {
        log_printf("battery alert level=%d (%dmV, was %d)\n", level, s_mv, s_level);
        s_level = level;
        ledEffectsSetBatteryAlert(level);
    }

    if ((int32_t)(now - s_nextLogMs) >= 0) {
        s_nextLogMs = now + LOG_MS;
        log_printf("battery %dmV %s usb=%d base=%d est=%d%% level=%ld%% bus5v=%d\n", s_mv, chargingName(s_charging),
                   (int)s_usb, (int)s_baseCharging, batteryPercentFromMv(s_mv), (long)M5.Power.getBatteryLevel(),
                   (int)M5.Power.getExtOutput());
    }
    return true;
}
