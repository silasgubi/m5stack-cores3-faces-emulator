// Charge modes through the CoreS3's AXP2101, and battery helpers for the UI.
//
// What is controlled: only charging from the CoreS3 USB-C, done by the AXP2101. The Faces
// Charging3 dock feeds the Bottom3 base's own TP4057 (~330 mA, 4.2 V, fixed), straight to
// the cell, so no mode applies on the dock. The cell is the base's 600 mAh LiPo, reached
// through the rear switch ("I") and the M-Bus BAT pin.
//
// Modes:
// - Normal: 200 mA, 4.2 V. Factory behaviour (the chip's EFUSE default current; M5.begin
//   sets 4.2 V). Set explicitly so switching back from another mode restores it.
// - Fast: 400 mA (~0.7C), 4.2 V. Below the usual 1C limit for a LiPo. The board has no
//   battery thermistor (M5Unified sets TS as a plain input), so the charger cannot see
//   the cell temperature; the AXP2101 still cuts the current on its own die temperature.
// - Smart: keeps the cell between ~40% and ~80%, which slows LiPo wear for a console that
//   lives on the cable. Two layers:
//   1. The chip's charge voltage is set to 4.0 V (~80%). This is the real ceiling: it
//      holds without the firmware (console switched off on USB, emulator stuck, between
//      the 10 s ticks) and the chip tapers the current near the top on its own.
//   2. A firmware latch for the lower bound: once the cell is full at 4.0 V (reading
//      >= 4000 mV, or the chip reports "charge done" in case its CV lands a hair low),
//      charging is disabled; it comes back only below 3750 mV (~40%). Without the latch
//      the chip would top the cell up on every plug-in.
//   The pause survives an ESP32 restart (Select+Start back to the menu), because the
//   AXP2101 keeps its registers; at boot the latch is read back from the chip.
// - Off: charging disabled (e.g. the console stays on a cable for days).
// The AXP2101 keeps these settings until it loses all power (USB unplugged with the rear
// switch on "O"; the CoreS3 SE has no cell of its own). M5.begin restores only the 4.2 V
// target, so after flashing another firmware in Off (or a paused Smart) the cell does
// not charge until that full power loss.
//
// Register access beyond the M5.Power API (AXP2101 datasheet; same values as XPowersLib):
// 0x01 bits 2:0 charger state (4 = charge done), 0x18 bit 1 charge enable,
// 0x64 bits 2:0 CV target (1 = 4.0 V, 3 = 4.2 V). The CV target is read back after each
// change because older M5Unified releases mapped requests below their lowest step onto a
// reserved code; if the chip did not get what was asked, charging is switched off.

#include <M5Unified.h>
#include "power_mode.h"

static constexpr uint16_t CHARGE_MA_NORMAL = 200;
static constexpr uint16_t CHARGE_MA_FAST = 400;
static constexpr uint16_t CV_FULL_MV = 4200;   // the cell is a 4.2 V type
static constexpr uint16_t CV_SMART_MV = 4000;  // ~80%
static constexpr int SMART_STOP_MV = 4000;     // latch: pause charging at or above this
static constexpr int SMART_RESUME_MV = 3750;   // latch: resume below this (~40%)
static constexpr int SMART_DONE_CHECK_MV = 3900;  // only ask the chip for "done" near the top
static constexpr int NO_BATTERY_MV = 2500;     // rear switch on "O": the AXP2101 reads ~400 mV

static constexpr uint8_t REG_CHARGER_STATUS = 0x01;
static constexpr uint8_t CHARGER_DONE = 4;
static constexpr uint8_t REG_CHARGE_CTRL = 0x18;
static constexpr uint8_t CHARGE_ENABLE_BIT = 0x02;
static constexpr uint8_t REG_CV_TARGET = 0x64;

static int s_mode = POWER_MODE_NORMAL;
static bool s_applied = false;   // first powerModeApply since boot
static bool s_chargeOn = true;   // what was last written to the chip
static bool s_fault = false;     // CV read-back mismatch: charging stays off

static bool isAxp2101() { return M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp2101; }

static void setCharge(bool on) {
    if (s_fault) on = false;
    M5.Power.setBatteryCharge(on);
    s_chargeOn = on;
}

// Sets the CV target and checks that the chip really got it (see the header comment).
static bool setChargeVoltageChecked(uint16_t mv) {
    M5.Power.setChargeVoltage(mv);
    const uint8_t want = mv == CV_SMART_MV ? 1 : 3;
    const uint8_t got = M5.Power.Axp2101.readRegister8(REG_CV_TARGET) & 0x07;
    if (got == want) return true;
    log_printf("power: CV target code %u, expected %u; charging disabled\n", got, want);
    s_fault = true;
    return false;
}

void powerModeApply(int mode) {
    if (mode < 0 || mode >= POWER_MODE_COUNT) mode = POWER_MODE_NORMAL;
    const bool boot = !s_applied;
    s_applied = true;
    s_mode = mode;
    if (!isAxp2101()) {
        log_printf("power: no AXP2101, charge mode ignored\n");
        return;
    }
    s_fault = false;

    switch (mode) {
    case POWER_MODE_FAST:
        M5.Power.setChargeCurrent(CHARGE_MA_FAST);
        setChargeVoltageChecked(CV_FULL_MV);
        setCharge(true);
        break;
    case POWER_MODE_SMART: {
        M5.Power.setChargeCurrent(CHARGE_MA_NORMAL);
        setChargeVoltageChecked(CV_SMART_MV);
        const int mv = M5.Power.getBatteryVoltage();
        bool on = true;  // no battery, or below the band: charge
        if (batteryPresent(mv) && mv >= SMART_STOP_MV) {
            on = false;
        } else if (batteryPresent(mv) && mv >= SMART_RESUME_MV) {
            // Inside the band. At boot keep what the chip holds (a pause survives the
            // restart back to the menu; after a full power loss the chip is back to its
            // default, charging). Picked in the menu: top up to ~80% now.
            on = boot ? (M5.Power.Axp2101.readRegister8(REG_CHARGE_CTRL) & CHARGE_ENABLE_BIT) != 0 : true;
        }
        setCharge(on);
        break;
    }
    case POWER_MODE_OFF:
        setCharge(false);
        break;
    default:  // POWER_MODE_NORMAL
        M5.Power.setChargeCurrent(CHARGE_MA_NORMAL);
        setChargeVoltageChecked(CV_FULL_MV);
        setCharge(true);
        break;
    }
    log_printf("power: charge mode %d, charging %s\n", mode, s_chargeOn ? "on" : "off");
}

void powerModeTick(int16_t mv) {
    if (s_mode != POWER_MODE_SMART || s_fault || !batteryPresent(mv) || !isAxp2101()) return;
    if (s_chargeOn) {
        bool full = mv >= SMART_STOP_MV;
        if (!full && mv >= SMART_DONE_CHECK_MV)
            full = (M5.Power.Axp2101.readRegister8(REG_CHARGER_STATUS) & 0x07) == CHARGER_DONE;
        if (full) {
            setCharge(false);
            log_printf("power: smart pause at %dmV\n", mv);
        }
    } else if (mv < SMART_RESUME_MV) {
        setCharge(true);
        log_printf("power: smart resume at %dmV\n", mv);
    }
}

bool powerModeChargeEnabled() { return s_chargeOn; }

// Charge level from voltage, for a 1S LiPo at the console's load (~0.5C), 25 C.
// A generic curve, anchored so 4.00 V ~ 80% and 3.75 V ~ 40% (the Smart band) and so the
// low battery alert (battery.cpp: 3.55 V) shows ~5%. Refine it from a full discharge
// (the "battery ...mV" log every 30 s): the % should fall evenly with time.
// Caveats for the UI:
// - While charging the reading is higher than the real level (charge current through
//   the cell's resistance, then held at the CV target), so the % jumps up on plug-in and
//   down on unplug. Show the charging state next to it.
// - Voltage rather than M5.Power.getBatteryLevel(): the AXP2101 fuel gauge is still
//   learning this cell (it needs full cycles), so early on the voltage is more reliable.
struct MvPercent {
    int16_t mv;
    int8_t pct;
};
static constexpr MvPercent LIPO_CURVE[] = {
    {4150, 100}, {4080, 90}, {4000, 80}, {3940, 70}, {3880, 60}, {3820, 50},
    {3750, 40},  {3710, 30}, {3670, 20}, {3620, 10}, {3550, 5},  {3400, 0},
};
static constexpr int LIPO_CURVE_N = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

int batteryPercentFromMv(int mv) {
    if (mv >= LIPO_CURVE[0].mv) return 100;
    for (int i = 1; i < LIPO_CURVE_N; i++) {
        const MvPercent &hi = LIPO_CURVE[i - 1], &lo = LIPO_CURVE[i];
        if (mv >= lo.mv) return lo.pct + (mv - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv);
    }
    return 0;
}

bool batteryPresent(int mv) { return mv >= NO_BATTERY_MV; }
