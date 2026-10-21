#pragma once

// Battery: AXP2101 reads, log and low battery alert on the LEDs (ledEffectsSetBatteryAlert).
// Everything runs on core 1 (the menus, then frame_hook), the core that owns the I2C bus.

// In a game, once per frame from frame_hook: it only does I2C every 10 s and returns true
// on those frames, so the caller can pass the new reading on (charge latch, HUD icon).
bool batteryTick();
// Reads the chip now. For the menus, which have no frame hook.
void batteryRead();

// Last reading (0 mV before the first one).
int batteryMv();
bool batteryCharging();  // the AXP2101 is charging the cell right now
bool batteryUsb();       // power on the CoreS3 USB-C
