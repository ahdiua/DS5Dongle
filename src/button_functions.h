//
// BOOTSEL button gestures, split out of bt.cpp.
//

#ifndef DS5_BRIDGE_BUTTON_FUNCTIONS_H
#define DS5_BRIDGE_BUTTON_FUNCTIONS_H

// Poll the BOOTSEL button at 10 Hz and dispatch gestures:
//   single click  -> toggle DualSense / XInput USB mode
//   double click  -> pair another controller   (bt_bootsel_controller_action)
//   triple click  -> reboot the Pico
//   four clicks   -> reboot into BOOTSEL
//   ~1.5 s hold   -> clear all pairings         (bt_bootsel_hold_action)
void button_check();

// Temporary mode-confirmation LED pattern. Call after the normal Bluetooth
// and battery LED tasks so the confirmation has priority while active.
void button_feedback_tick();

#endif // DS5_BRIDGE_BUTTON_FUNCTIONS_H
