#include "usb_mode.h"

#include <cstdio>

#include "audio.h"
#include "bt.h"
#include "config.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb.h"
#include "wake.h"
#include "xinput.h"

extern bool spk_active;

namespace {

UsbGamepadMode current_mode = UsbGamepadMode::DualSense;
bool reset_to_dualsense_pending = false;

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
    if (audio_mic_active()) {
        set_mic_active(false);
    }
    spk_active = false;
    xinput_reset_input();

    wake_on_bt_disconnect();
    wake_note_usb_reconnect();
    tud_disconnect();
    sleep_ms(150);

    reset_to_dualsense_pending = false;
    set_mode(usb_xinput_mode() ? UsbGamepadMode::DualSense
                               : UsbGamepadMode::XInput);
    usb_keyboard_only = !controller_connected && !usb_xinput_mode() && get_config().enable_wake;
    usb_reconfiguring = true;

    // Without a controller, expose only the native wake keyboard when enabled.
    if (controller_connected || usb_keyboard_only) {
        tud_connect();
    }
}

void usb_gamepad_on_controller_disconnect() {
    if (usb_xinput_mode()) {
        reset_to_dualsense_pending = true;
    }
}

void usb_gamepad_task() {
    if (!reset_to_dualsense_pending || tud_suspended()) {
        return;
    }

    // End the temporary XInput identity before the next controller connection.
    // If Bluetooth already came back while USB was suspended, re-enumerate the
    // connected controller immediately using the native descriptors.
    const bool controller_connected = bt_is_connected();
    wake_note_usb_reconnect();
    tud_disconnect();
    set_mode(UsbGamepadMode::DualSense);
    reset_to_dualsense_pending = false;
    usb_keyboard_only = !controller_connected && get_config().enable_wake;
    usb_reconfiguring = true;

    if (controller_connected || usb_keyboard_only) {
        sleep_ms(150);
        tud_connect();
    }
}
