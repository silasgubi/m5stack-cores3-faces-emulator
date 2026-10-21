// Battery: reading, log and low battery alert on the LEDs.
//
// AXP2101 read (I2C) every 10 s, off the per-frame path. Alert only while
// discharging: below 3550 mV the two bottom LEDs pulse red; below 3400 mV,
// all 10. 100 mV hysteresis so it does not flicker at the threshold (the voltage
// drops under speaker load and comes back). Charging, or above the threshold: no alert.
// Base rear switch on "O": the AXP2101 reads ~400 mV with no battery connected;
// below that (batteryPresent, power_mode.h) it is not treated as a low battery.

#include <M5Unified.h>
#include "battery.h"
#include "led_effects.h"
#include "power_mode.h"

static constexpr uint32_t READ_MS = 10000;  // I2C read
static constexpr uint32_t LOG_MS = 30000;   // "battery ..." line on serial
static constexpr int LOW_MV = 3550;         // level 1: bottom LEDs
static constexpr int CRIT_MV = 3400;        // level 2: all
static constexpr int HYST_MV = 100;         // only steps down HYST_MV above the threshold

static uint32_t s_nextReadMs = 0, s_nextLogMs = 0;
static int s_mv = 0;
static m5::Power_Class::is_charging_t s_charging = m5::Power_Class::is_charging_t::charge_unknown;
static bool s_usb = false;
static int s_level = 0;

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
}

int batteryMv() { return s_mv; }
bool batteryCharging() { return s_charging == m5::Power_Class::is_charging; }
bool batteryUsb() { return s_usb; }

bool batteryTick() {
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextReadMs) < 0) return false;
    s_nextReadMs = now + READ_MS;
    batteryRead();

    int level = 0;
    // Only while running on the cell: with charging set to Off the AXP2101 also reports
    // "discharging" on USB-C, where a low cell is no risk.
    if (s_charging == m5::Power_Class::is_discharging && !s_usb && batteryPresent(s_mv)) level = nextLevel(s_level, s_mv);
    if (level != s_level) {
        log_printf("battery alert level=%d (%dmV, was %d)\n", level, s_mv, s_level);
        s_level = level;
        ledEffectsSetBatteryAlert(level);
    }

    if ((int32_t)(now - s_nextLogMs) >= 0) {
        s_nextLogMs = now + LOG_MS;
        log_printf("battery %dmV %s usb=%d est=%d%% level=%ld%% bus5v=%d\n", s_mv, chargingName(s_charging),
                   (int)s_usb, batteryPercentFromMv(s_mv), (long)M5.Power.getBatteryLevel(),
                   (int)M5.Power.getExtOutput());
    }
    return true;
}
