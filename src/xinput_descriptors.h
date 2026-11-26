#ifndef DS5_BRIDGE_XINPUT_DESCRIPTORS_H
#define DS5_BRIDGE_XINPUT_DESCRIPTORS_H

#include <cstdint>

// USB descriptors of the runtime XInput identity. usb_descriptors.cpp only
// dispatches to these while usb_xinput_mode() is active, so the upstream
// descriptor code stays untouched.

uint8_t const *xinput_device_descriptor();

// in_interval: bInterval (1 ms frames) of the input endpoint.
uint8_t const *xinput_configuration_descriptor(uint8_t in_interval);

// Manufacturer and product strings; nullptr for every other index, which the
// native string callback then serves (language id, serial number).
uint16_t const *xinput_string_descriptor(uint8_t index);

#endif // DS5_BRIDGE_XINPUT_DESCRIPTORS_H
