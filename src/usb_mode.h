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
// written to flash and only lasts for the current controller connection.
void usb_gamepad_toggle_mode();

// Call first thing when the Bluetooth link ends. XInput switches back to the
// native identity right away; the upstream disconnect / resume handling that
// follows then hides the device or re-exposes the wake keyboard, deferring the
// re-enumeration while the host is suspended exactly as for native mode.
void usb_gamepad_on_controller_disconnect();

#endif // DS5_BRIDGE_USB_MODE_H
