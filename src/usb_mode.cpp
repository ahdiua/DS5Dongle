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

    // Stop effects owned by the old USB identity before removing it. A USB
    // disconnect does not itself guarantee that the wireless controller gets
    // a final zero-rumble packet.
    xinput_stop_rumble();
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
