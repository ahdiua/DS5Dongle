#ifndef DS5_BRIDGE_XINPUT_H
#define DS5_BRIDGE_XINPUT_H

#include <cstdint>

// Feed a 63-byte DualSense USB-format input payload (the bytes after the
// Bluetooth 0x31 header) into the Xbox 360 report mapper.
void xinput_on_dualsense_report(const uint8_t *data, uint16_t len);

// Initialize synchronization for the mapped input report.
void xinput_init();

// Submit a mapped state only after fresh Bluetooth input arrives.
void xinput_task();

// Clear host-visible input state after a controller or mode disconnect.
void xinput_reset_input();

// Clear effects that cannot be represented after changing USB identity.
void xinput_clear_controller_effects();

#endif // DS5_BRIDGE_XINPUT_H
