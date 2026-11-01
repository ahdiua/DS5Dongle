"""Host regression: python tests/test_usb_mode.py (requires g++ on PATH)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class UsbModeTests(unittest.TestCase):
    def test_wake_keyboard_and_xinput_transitions(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            for name in ('usb_mode.cpp', 'usb_mode.h'):
                shutil.copyfile(ROOT / 'src' / name, tmp / name)
            (tmp / 'pico').mkdir()
            for name in ('audio.h', 'bt.h', 'config.h', 'pico/time.h',
                         'tusb.h', 'usb.h', 'wake.h', 'xinput.h'):
                (tmp / name).write_text('#include "platform.h"\n')
            (tmp / 'platform.h').write_text('''#pragma once
#include <cstdint>
inline bool connected = false, suspended = false, attached = false;
inline bool usb_keyboard_only = false, usb_reconfiguring = false;
inline bool spk_active = false;
inline unsigned connects = 0, disconnects = 0, effects_cleared = 0;
struct Config { bool enable_wake = false; };
inline Config config;
inline Config& get_config() { return config; }
inline bool bt_is_connected() { return connected; }
inline void bt_discard_pending_output() {}
inline void xinput_clear_controller_effects() { ++effects_cleared; }
inline void xinput_reset_input() {}
inline bool audio_mic_active() { return false; }
inline void set_mic_active(bool) {}
inline void wake_on_bt_disconnect() {}
inline void wake_note_usb_reconnect() {}
inline void sleep_ms(uint32_t) {}
inline void tud_disconnect() { attached = false; ++disconnects; }
inline void tud_connect() { attached = true; ++connects; }
inline bool tud_suspended() { return suspended; }
''')
            (tmp / 'test.cpp').write_text('''#include <cassert>
#include "platform.h"
#include "usb_mode.h"
int main() {
    // Booted as a wake keyboard, BOOTSEL selects XInput before BT connects.
    config.enable_wake = true;
    usb_keyboard_only = attached = true;
    usb_gamepad_toggle_mode();
    assert(usb_xinput_mode() && !usb_keyboard_only && !attached);
    usb_gamepad_toggle_mode();
    assert(!usb_xinput_mode() && usb_keyboard_only && attached);

    // A connected controller exposes the full device in either mode.
    connected = true;
    usb_gamepad_toggle_mode();
    assert(usb_xinput_mode() && !usb_keyboard_only && attached);
    assert(usb_reconfiguring && effects_cleared == 1);

    // Losing Bluetooth restores only the wake keyboard.
    connected = false;
    usb_gamepad_on_controller_disconnect();
    usb_gamepad_task();
    assert(!usb_xinput_mode() && usb_keyboard_only && attached);
    auto previous_connects = connects;
    usb_gamepad_task();
    assert(connects == previous_connects);

    // With wake disabled, losing Bluetooth removes the USB device.
    config.enable_wake = false;
    connected = true;
    usb_gamepad_toggle_mode();
    connected = false;
    usb_gamepad_on_controller_disconnect();
    usb_gamepad_task();
    assert(!usb_xinput_mode() && !usb_keyboard_only && !attached);

    // Do not re-enumerate while the host is asleep. If BT reconnects before
    // resume, restore a full native controller when the host wakes.
    config.enable_wake = connected = true;
    usb_gamepad_toggle_mode();
    connected = false;
    suspended = true;
    usb_gamepad_on_controller_disconnect();
    auto previous_disconnects = disconnects;
    usb_gamepad_task();
    assert(usb_xinput_mode() && disconnects == previous_disconnects);
    connected = true;
    suspended = false;
    usb_gamepad_task();
    assert(!usb_xinput_mode() && !usb_keyboard_only && attached);

    // Resume with no controller keeps only the wake keyboard.
    usb_gamepad_toggle_mode();
    connected = false;
    suspended = true;
    usb_gamepad_on_controller_disconnect();
    usb_gamepad_task();
    assert(usb_xinput_mode());
    suspended = false;
    usb_gamepad_task();
    assert(!usb_xinput_mode() && usb_keyboard_only && attached);
}
''')
            exe = tmp / 'test.exe'
            subprocess.run(['g++', '-std=c++17', '-I', str(tmp),
                            str(tmp / 'usb_mode.cpp'), str(tmp / 'test.cpp'),
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
