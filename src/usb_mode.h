#ifndef DS5_BRIDGE_USB_MODE_H
#define DS5_BRIDGE_USB_MODE_H

#include <cstdint>

enum class UsbGamepadMode : uint8_t {
    DualSense,
    XInput,
};

UsbGamepadMode usb_gamepad_mode();
bool usb_xinput_mode();

// Toggle the runtime-only USB gamepad identity. The selected mode is not
// written to flash and therefore returns to DualSense after any reboot.
void usb_gamepad_toggle_mode();

#endif // DS5_BRIDGE_USB_MODE_H
