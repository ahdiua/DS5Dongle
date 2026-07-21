#include "usb_mode.h"

#include <cstdio>

#include "audio.h"
#include "bt.h"
#include "pico/time.h"
#include "tusb.h"
#include "wake.h"
#include "xinput.h"

extern bool spk_active;

namespace {

UsbGamepadMode current_mode = UsbGamepadMode::DualSense;

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

    current_mode = usb_xinput_mode() ? UsbGamepadMode::DualSense
                                     : UsbGamepadMode::XInput;
    printf("[USB] Runtime gamepad mode -> %s\n",
           usb_xinput_mode() ? "XInput" : "DualSense");

    // Preserve the project's existing behavior: no controller means no USB
    // gamepad. The Bluetooth connect path will call tud_connect() later.
    if (controller_connected) {
        tud_connect();
    }
}
