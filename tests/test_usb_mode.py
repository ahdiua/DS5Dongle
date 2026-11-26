"""Host regression: python tests/test_usb_mode.py (requires g++, or MSVC cl in a VS dev shell)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

PLATFORM = '''#pragma once
#include <cstdint>
inline bool connected = false, attached = false;
inline bool usb_keyboard_only = false, usb_reconfiguring = false, usb_sleep_mode = false;
inline unsigned connects = 0, disconnects = 0, reconnects = 0;
inline unsigned effects_cleared = 0, inputs_reset = 0;
struct Config { bool enable_wake = false; };
inline Config config;
inline Config& get_config() { return config; }
inline bool bt_is_connected() { return connected; }
inline void bt_discard_pending_output() {}
inline void xinput_clear_controller_effects() { ++effects_cleared; }
inline void xinput_reset_input() { ++inputs_reset; }
inline void wake_on_bt_disconnect() {}
inline void wake_note_usb_reconnect() {}
inline void tud_disconnect() { attached = false; ++disconnects; }
inline void tud_connect() { attached = true; ++connects; }
// Mirrors src/usb.cpp usb_reconnect().
inline void usb_reconnect(bool keyboard_only) {
    wake_note_usb_reconnect();
    tud_disconnect();
    usb_keyboard_only = keyboard_only;
    if (!keyboard_only) usb_sleep_mode = false;
    usb_reconfiguring = true;
    ++reconnects;
    tud_connect();
}
'''

TEST = '''#include <cassert>
#include "platform.h"
#include "usb_mode.h"
int main() {
#if ENABLE_SERIAL
    // Serial builds keep the full device so the CDC console survives, and
    // XInput is not ended by a Bluetooth disconnect.
    usb_gamepad_toggle_mode();
    assert(usb_xinput_mode() && attached && !usb_keyboard_only && reconnects == 1);
    usb_gamepad_on_controller_disconnect();
    assert(usb_xinput_mode());
    usb_gamepad_toggle_mode();
    assert(!usb_xinput_mode() && attached && reconnects == 2);
    return 0;
#else
    // Wake keyboard (here the dock-sleep identity) and no controller: XInput
    // has nothing to expose until a controller connects.
    config.enable_wake = true;
    usb_keyboard_only = attached = true;
    usb_sleep_mode = true;
    usb_gamepad_toggle_mode();
    assert(usb_xinput_mode() && !usb_keyboard_only && !attached && !usb_sleep_mode);
    assert(reconnects == 0);

    // Back to native: the wake keyboard returns through usb_reconnect(true).
    usb_gamepad_toggle_mode();
    assert(!usb_xinput_mode() && usb_keyboard_only && attached && reconnects == 1);

    // With a controller both directions re-enumerate the full device and
    // clear actuator state that the new identity cannot represent.
    connected = true;
    usb_gamepad_toggle_mode();
    assert(usb_xinput_mode() && !usb_keyboard_only && attached && usb_reconfiguring);
    assert(reconnects == 2 && effects_cleared == 1);
    usb_gamepad_toggle_mode();
    assert(!usb_xinput_mode() && attached && reconnects == 3 && effects_cleared == 2);

    // Losing the controller ends XInput right away and leaves the USB
    // transition to the upstream disconnect / resume handling.
    usb_gamepad_toggle_mode();
    connected = false;
    const unsigned reconnects_before = reconnects, disconnects_before = disconnects;
    const unsigned resets_before = inputs_reset;
    usb_gamepad_on_controller_disconnect();
    assert(!usb_xinput_mode() && inputs_reset == resets_before + 1);
    assert(reconnects == reconnects_before && disconnects == disconnects_before);

    // Native mode ignores the hook.
    usb_gamepad_on_controller_disconnect();
    assert(!usb_xinput_mode() && inputs_reset == resets_before + 1);

    // Wake disabled and no controller: neither identity exposes anything.
    config.enable_wake = false;
    usb_keyboard_only = attached = false;
    usb_gamepad_toggle_mode();
    assert(usb_xinput_mode() && !attached);
    usb_gamepad_toggle_mode();
    assert(!usb_xinput_mode() && !attached && !usb_keyboard_only);
    return 0;
#endif
}
'''


def build_and_run(tmp: Path, sources, defines, name):
    exe = tmp / f'{name}.exe'
    if shutil.which('g++'):
        cmd = ['g++', '-std=c++17', '-Wall', '-I', str(tmp)]
        cmd += [f'-D{d}' for d in defines]
        cmd += [str(s) for s in sources] + ['-o', str(exe)]
    elif shutil.which('cl'):
        obj = tmp / f'{name}_obj'
        obj.mkdir()
        cmd = ['cl', '/nologo', '/std:c++17', '/EHsc', '/W3', f'/I{tmp}']
        cmd += [f'/D{d}' for d in defines]
        cmd += [str(s) for s in sources] + [f'/Fo{obj}\\', f'/Fe{exe}']
    else:
        raise unittest.SkipTest('no C++ compiler (g++ or cl) on PATH')
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True)


class UsbModeTests(unittest.TestCase):
    def test_mode_transitions(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            for name in ('usb_mode.cpp', 'usb_mode.h'):
                shutil.copyfile(ROOT / 'src' / name, tmp / name)
            for name in ('bt.h', 'config.h', 'tusb.h', 'usb.h', 'wake.h', 'xinput.h'):
                (tmp / name).write_text('#include "platform.h"\n')
            (tmp / 'platform.h').write_text(PLATFORM)
            (tmp / 'test.cpp').write_text(TEST)
            sources = [tmp / 'usb_mode.cpp', tmp / 'test.cpp']
            build_and_run(tmp, sources, [], 'native')
            build_and_run(tmp, sources, ['ENABLE_SERIAL=1'], 'serial')


if __name__ == '__main__':
    unittest.main()
