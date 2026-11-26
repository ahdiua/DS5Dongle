#include "xinput_descriptors.h"

#include <cstddef>

#include "tusb.h"

namespace {

// String indices shared with the native descriptors in usb_descriptors.cpp
// (STRID_MANUFACTURER / STRID_PRODUCT / STRID_SERIAL).
constexpr uint8_t STRID_MANUFACTURER = 1;
constexpr uint8_t STRID_PRODUCT = 2;
constexpr uint8_t STRID_SERIAL = 3;

// Wired Xbox 360-compatible identity used only for the runtime XInput mode.
// The interface descriptor below, rather than HID, is what binds Windows to
// its built-in XUSB driver.
tusb_desc_device_t const desc_device_xinput = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0xFF,
    .bDeviceSubClass = 0xFF,
    .bDeviceProtocol = 0xFF,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x045E,
    .idProduct = 0x028E,
    .bcdDevice = 0x0572,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

// Xbox 360 wired-controller interface. The 16-byte class-specific descriptor
// and endpoint layout match the protocol consumed by Windows' XUSB driver.
uint8_t descriptor_configuration_xinput[] = {
    0x09, TUSB_DESC_CONFIGURATION,
    0x30, 0x00, // wTotalLength = 48
    0x01,       // bNumInterfaces
    0x01,       // bConfigurationValue
    0x00,       // iConfiguration
    0x80,       // bus powered, no remote wakeup
    0xFA,       // 500 mA

    0x09, TUSB_DESC_INTERFACE,
    0x00,       // bInterfaceNumber
    0x00,       // bAlternateSetting
    0x02,       // IN + OUT endpoints
    0xFF,       // vendor-specific
    0x5D,       // XInput subclass
    0x01,       // XInput protocol
    0x00,       // iInterface

    0x10, 0x21, 0x10, 0x01, 0x01, 0x24, 0x81, 0x14,
    0x03, 0x00, 0x03, 0x13, 0x02, 0x00, 0x03, 0x00,

    0x07, TUSB_DESC_ENDPOINT,
    0x81,       // EP1 IN
    0x03,       // interrupt
    0x20, 0x00, // 32 bytes
    0x04,       // 4 ms, replaced with the configured interval

    0x07, TUSB_DESC_ENDPOINT,
    0x02,       // EP2 OUT
    0x03,       // interrupt
    0x20, 0x00, // 32 bytes
    0x08,       // 8 ms
};
static_assert(sizeof(descriptor_configuration_xinput) == 48);
constexpr size_t XINPUT_IN_INTERVAL_OFFSET = 40;

uint16_t string_buffer[32];

uint16_t const *ascii_string_descriptor(const char *text) {
    size_t count = 0;
    while (text[count] != '\0' && count < sizeof(string_buffer) / sizeof(string_buffer[0]) - 1) {
        string_buffer[1 + count] = static_cast<uint8_t>(text[count]);
        count++;
    }
    string_buffer[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return string_buffer;
}

} // namespace

uint8_t const *xinput_device_descriptor() {
    return reinterpret_cast<uint8_t const *>(&desc_device_xinput);
}

uint8_t const *xinput_configuration_descriptor(uint8_t in_interval) {
    descriptor_configuration_xinput[XINPUT_IN_INTERVAL_OFFSET] = in_interval;
    return descriptor_configuration_xinput;
}

uint16_t const *xinput_string_descriptor(uint8_t index) {
    switch (index) {
        case STRID_MANUFACTURER:
            return ascii_string_descriptor("DS5Dongle");
        case STRID_PRODUCT:
            return ascii_string_descriptor("Xbox 360 Controller");
        default:
            return nullptr;
    }
}
