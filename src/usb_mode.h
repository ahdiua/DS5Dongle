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

// Schedule XInput to return to the native identity after the controller link
// ends. The actual USB detach is deferred while the host bus is suspended.
void usb_gamepad_on_controller_disconnect();
void usb_gamepad_task();

#endif // DS5_BRIDGE_USB_MODE_H
