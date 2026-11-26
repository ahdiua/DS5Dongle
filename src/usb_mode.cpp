#include "usb_mode.h"

#include <cstdio>

#include "bt.h"
#include "config.h"
#include "tusb.h"
#include "usb.h"
#include "wake.h"
#include "xinput.h"

#ifndef ENABLE_SERIAL
#define ENABLE_SERIAL 0
#endif

namespace {

UsbGamepadMode current_mode = UsbGamepadMode::DualSense;

void set_mode(UsbGamepadMode mode) {
    current_mode = mode;
    printf("[USB] Runtime gamepad mode -> %s\n",
           current_mode == UsbGamepadMode::XInput ? "XInput" : "DualSense");
}

} // namespace

UsbGamepadMode usb_gamepad_mode() {
    return current_mode;
}

bool usb_xinput_mode() {
    return current_mode == UsbGamepadMode::XInput;
}

void usb_gamepad_toggle_mode() {
    const bool controller_connected = bt_is_connected();

    // Discard audio/haptic packets owned by the old USB identity before
    // enqueueing the final actuator state. This guarantees queue capacity for
    // the zero-rumble and adaptive-trigger-off packet; a USB disconnect alone
    // does not change state on the wireless controller.
    if (controller_connected) {
        bt_discard_pending_output();
        xinput_clear_controller_effects();
    }
    xinput_reset_input();
    wake_on_bt_disconnect(); // drop wake/shortcut state tied to the old identity

    set_mode(usb_xinput_mode() ? UsbGamepadMode::DualSense
                               : UsbGamepadMode::XInput);
    usb_sleep_mode = false;

    // Re-enumerate through the upstream helper so the timing, watchdog and
    // audio handling stay in one place. Without a controller, expose what the
    // native firmware would: the wake keyboard, or nothing until a controller
    // connects (its feature init then calls usb_reconnect(false)). Serial
    // builds always keep the full device so the CDC console survives.
    if (controller_connected || ENABLE_SERIAL) {
        usb_reconnect(false);
    } else if (!usb_xinput_mode() && get_config().enable_wake) {
        usb_reconnect(true);
    } else {
        wake_note_usb_reconnect();
        tud_disconnect();
        usb_keyboard_only = false;
    }
}

void usb_gamepad_on_controller_disconnect() {
#if ENABLE_SERIAL
    // Serial builds never re-enumerate on Bluetooth events (the CDC console
    // must stay attached), so XInput simply lasts until the next BOOTSEL click.
    return;
#else
    if (!usb_xinput_mode()) return;
    xinput_reset_input();
    set_mode(UsbGamepadMode::DualSense);
#endif
}
