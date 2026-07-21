#include "xinput.h"

#include <cstring>

#include "bt.h"
#include "device/usbd_pvt.h"
#include "pico/critical_section.h"
#include "tusb.h"
#include "usb_mode.h"
#include "utils.h"

namespace {

constexpr uint16_t XINPUT_DPAD_UP        = 0x0001;
constexpr uint16_t XINPUT_DPAD_DOWN      = 0x0002;
constexpr uint16_t XINPUT_DPAD_LEFT      = 0x0004;
constexpr uint16_t XINPUT_DPAD_RIGHT     = 0x0008;
constexpr uint16_t XINPUT_START          = 0x0010;
constexpr uint16_t XINPUT_BACK           = 0x0020;
constexpr uint16_t XINPUT_LEFT_THUMB     = 0x0040;
constexpr uint16_t XINPUT_RIGHT_THUMB    = 0x0080;
constexpr uint16_t XINPUT_LEFT_SHOULDER  = 0x0100;
constexpr uint16_t XINPUT_RIGHT_SHOULDER = 0x0200;
constexpr uint16_t XINPUT_GUIDE          = 0x0400;
constexpr uint16_t XINPUT_A              = 0x1000;
constexpr uint16_t XINPUT_B              = 0x2000;
constexpr uint16_t XINPUT_X              = 0x4000;
constexpr uint16_t XINPUT_Y              = 0x8000;

struct __attribute__((packed)) XInputReport {
    uint8_t report_id;
    uint8_t report_size;
    uint16_t buttons;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint8_t reserved[6];
};

static_assert(sizeof(XInputReport) == 20);

// Map the asymmetric 0..255 DualSense range onto the full signed XInput range
// while keeping the physical centre (128) exactly at zero.
constexpr int16_t scale_axis(uint8_t value, bool invert) {
    const int32_t centered = static_cast<int32_t>(value) - 128;
    if (!invert) {
        return centered < 0
                   ? static_cast<int16_t>(centered * 256)
                   : static_cast<int16_t>((centered * 32767) / 127);
    }
    return centered < 0
               ? static_cast<int16_t>(((-centered) * 32767) / 128)
               : static_cast<int16_t>(-((centered * 32768) / 127));
}

static_assert(scale_axis(0, false) == -32768);
static_assert(scale_axis(128, false) == 0);
static_assert(scale_axis(255, false) == 32767);
static_assert(scale_axis(0, true) == 32767);
static_assert(scale_axis(128, true) == 0);
static_assert(scale_axis(255, true) == -32768);

XInputReport latest_report{.report_id = 0x00, .report_size = sizeof(XInputReport)};
alignas(4) XInputReport tx_report{.report_id = 0x00,
                                 .report_size = sizeof(XInputReport)};
critical_section_t report_cs;
bool report_dirty = false;
alignas(4) uint8_t out_buffer[32]{};
uint8_t endpoint_in = 0;
uint8_t endpoint_out = 0;
uint8_t device_rhport = 0;

uint16_t map_dpad(uint8_t direction) {
    switch (direction) {
        case 0: return XINPUT_DPAD_UP;
        case 1: return XINPUT_DPAD_UP | XINPUT_DPAD_RIGHT;
        case 2: return XINPUT_DPAD_RIGHT;
        case 3: return XINPUT_DPAD_RIGHT | XINPUT_DPAD_DOWN;
        case 4: return XINPUT_DPAD_DOWN;
        case 5: return XINPUT_DPAD_DOWN | XINPUT_DPAD_LEFT;
        case 6: return XINPUT_DPAD_LEFT;
        case 7: return XINPUT_DPAD_LEFT | XINPUT_DPAD_UP;
        default: return 0;
    }
}

void apply_rumble(uint8_t large_motor, uint8_t small_motor) {
    SetStateData state{};
    state.UseRumbleNotHaptics = 1;
    state.EnableImprovedRumbleEmulation = 1;
    state.RumbleEmulationLeft = large_motor;
    state.RumbleEmulationRight = small_motor;
    update_state(state);
}

void apply_player_led(uint8_t animation) {
    uint8_t indicators = 0x1F;
    switch (animation) {
        case 0x00: // all off
            indicators = 0x00;
            break;
        case 0x02: // flash player 1, then on
        case 0x06: // player 1 on
            indicators = 0x04;
            break;
        case 0x03: // flash player 2, then on
        case 0x07: // player 2 on
            indicators = 0x06;
            break;
        case 0x04: // flash player 3, then on
        case 0x08: // player 3 on
            indicators = 0x15;
            break;
        case 0x05: // flash player 4, then on
        case 0x09: // player 4 on
            indicators = 0x1B;
            break;
        default:
            // Blink/rotate patterns have no direct DualSense equivalent.
            // Lighting all five indicators still acknowledges the command.
            break;
    }

    SetStateData state{};
    state.AllowPlayerIndicators = 1;
    state.PlayerLight1 = indicators & 0x01;
    state.PlayerLight2 = (indicators >> 1) & 0x01;
    state.PlayerLight3 = (indicators >> 2) & 0x01;
    state.PlayerLight4 = (indicators >> 3) & 0x01;
    state.PlayerLight5 = (indicators >> 4) & 0x01;
    update_state(state);
}

bool endpoint_xfer(uint8_t rhport, uint8_t endpoint, uint8_t *buffer,
                   uint16_t len) {
#if TUSB_VERSION_NUMBER >= 2100
    return usbd_edpt_xfer(rhport, endpoint, buffer, len, false);
#else
    return usbd_edpt_xfer(rhport, endpoint, buffer, len);
#endif
}

bool arm_out_endpoint() {
    if (endpoint_out == 0 || usbd_edpt_busy(device_rhport, endpoint_out)) {
        return false;
    }
    return endpoint_xfer(device_rhport, endpoint_out, out_buffer,
                         sizeof(out_buffer));
}

void driver_init() {
    endpoint_in = 0;
    endpoint_out = 0;
}

bool driver_deinit() {
    endpoint_in = 0;
    endpoint_out = 0;
    return true;
}

void driver_reset(uint8_t rhport) {
    device_rhport = rhport;
    endpoint_in = 0;
    endpoint_out = 0;
}

uint16_t driver_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc,
                     uint16_t max_len) {
    if (!usb_xinput_mode() ||
        itf_desc->bInterfaceClass != 0xFF ||
        itf_desc->bInterfaceSubClass != 0x5D ||
        itf_desc->bInterfaceProtocol != 0x01) {
        return 0;
    }

    const uint8_t *desc = reinterpret_cast<const uint8_t *>(itf_desc);
    uint16_t consumed = sizeof(tusb_desc_interface_t);
    uint8_t found_endpoints = 0;
    desc = tu_desc_next(desc);

    while (consumed < max_len && found_endpoints < itf_desc->bNumEndpoints) {
        const uint8_t desc_len = tu_desc_len(desc);
        if (desc_len == 0 || consumed + desc_len > max_len) {
            return 0;
        }
        if (tu_desc_type(desc) == TUSB_DESC_ENDPOINT) {
            const auto *ep_desc = reinterpret_cast<const tusb_desc_endpoint_t *>(desc);
            if (!usbd_edpt_open(rhport, ep_desc)) {
                return 0;
            }
            if (tu_edpt_dir(ep_desc->bEndpointAddress) == TUSB_DIR_IN) {
                endpoint_in = ep_desc->bEndpointAddress;
            } else {
                endpoint_out = ep_desc->bEndpointAddress;
            }
            found_endpoints++;
        }
        consumed += desc_len;
        desc = tu_desc_next(desc);
    }

    if (found_endpoints != 2 || endpoint_in == 0 || endpoint_out == 0) {
        endpoint_in = 0;
        endpoint_out = 0;
        return 0;
    }

    device_rhport = rhport;
    memset(out_buffer, 0, sizeof(out_buffer));
    arm_out_endpoint();
    return consumed;
}

bool driver_control_xfer(uint8_t rhport, uint8_t stage,
                         tusb_control_request_t const *request) {
    (void) rhport;
    (void) stage;
    (void) request;
    return false;
}

bool driver_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result,
                 uint32_t transferred) {
    if (ep_addr == endpoint_out) {
        if (result == XFER_RESULT_SUCCESS) {
            if (transferred >= 5 && out_buffer[0] == 0x00 &&
                out_buffer[1] == 0x08) {
                // Wired Xbox 360 output: byte 3 = large/left motor,
                // byte 4 = small/right motor.
                apply_rumble(out_buffer[3], out_buffer[4]);
            } else if (transferred >= 3 && out_buffer[0] == 0x01 &&
                       out_buffer[1] == 0x03) {
                apply_player_led(out_buffer[2]);
            }
        }
        memset(out_buffer, 0, sizeof(out_buffer));
        return arm_out_endpoint();
    }
    return ep_addr == endpoint_in;
}

const usbd_class_driver_t xinput_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "XINPUT",
#else
    .name = nullptr,
#endif
    .init = driver_init,
    .deinit = driver_deinit,
    .reset = driver_reset,
    .open = driver_open,
    .control_xfer_cb = driver_control_xfer,
    .xfer_cb = driver_xfer,
    .xfer_isr = nullptr,
    .sof = nullptr,
};

} // namespace

void xinput_init() {
    critical_section_init(&report_cs);
}

void xinput_on_dualsense_report(const uint8_t *data, uint16_t len) {
    if (data == nullptr || len < 10) return;

    XInputReport report{.report_id = 0x00, .report_size = sizeof(XInputReport)};
    report.buttons = map_dpad(data[7] & 0x0F);

    if (data[7] & 0x10) report.buttons |= XINPUT_X; // Square
    if (data[7] & 0x20) report.buttons |= XINPUT_A; // Cross
    if (data[7] & 0x40) report.buttons |= XINPUT_B; // Circle
    if (data[7] & 0x80) report.buttons |= XINPUT_Y; // Triangle
    if (data[8] & 0x01) report.buttons |= XINPUT_LEFT_SHOULDER;
    if (data[8] & 0x02) report.buttons |= XINPUT_RIGHT_SHOULDER;
    if (data[8] & 0x10) report.buttons |= XINPUT_BACK; // Create
    if (data[8] & 0x20) report.buttons |= XINPUT_START; // Options
    if (data[8] & 0x40) report.buttons |= XINPUT_LEFT_THUMB;
    if (data[8] & 0x80) report.buttons |= XINPUT_RIGHT_THUMB;
    if (data[9] & 0x01) report.buttons |= XINPUT_GUIDE; // PS
    if (data[9] & 0x02) report.buttons |= XINPUT_BACK; // Touchpad click

    report.left_trigger = data[4];
    report.right_trigger = data[5];
    report.left_x = scale_axis(data[0], false);
    report.left_y = scale_axis(data[1], true);
    report.right_x = scale_axis(data[2], false);
    report.right_y = scale_axis(data[3], true);

    critical_section_enter_blocking(&report_cs);
    latest_report = report;
    report_dirty = true;
    critical_section_exit(&report_cs);
}

void xinput_task() {
    if (!usb_xinput_mode() || endpoint_in == 0 || !tud_mounted() ||
        usbd_edpt_busy(device_rhport, endpoint_in)) {
        return;
    }

    critical_section_enter_blocking(&report_cs);
    const bool should_send = report_dirty;
    critical_section_exit(&report_cs);
    if (!should_send) return;

    if (!usbd_edpt_claim(device_rhport, endpoint_in)) return;
    // TinyUSB/DCD owns this buffer until the asynchronous transfer completes.
    // Keep it separate from latest_report, which Bluetooth input may update.
    critical_section_enter_blocking(&report_cs);
    tx_report = latest_report;
    report_dirty = false;
    critical_section_exit(&report_cs);
    if (!endpoint_xfer(device_rhport, endpoint_in,
                       reinterpret_cast<uint8_t *>(&tx_report),
                       sizeof(tx_report))) {
        critical_section_enter_blocking(&report_cs);
        report_dirty = true;
        critical_section_exit(&report_cs);
        usbd_edpt_release(device_rhport, endpoint_in);
    }
}

void xinput_reset_input() {
    critical_section_enter_blocking(&report_cs);
    latest_report = XInputReport{.report_id = 0x00,
                                 .report_size = sizeof(XInputReport)};
    report_dirty = true;
    critical_section_exit(&report_cs);
}

void xinput_clear_controller_effects() {
    if (!bt_is_connected()) return;

    SetStateData state{};
    // Select compatibility rumble so the zero motor values are consumed, and
    // explicitly disable both adaptive-trigger programs. Zero is the trigger
    // effect-off mode; merely changing the USB descriptor does not clear it.
    state.UseRumbleNotHaptics = 1;
    state.EnableImprovedRumbleEmulation = 1;
    state.AllowRightTriggerFFB = 1;
    state.AllowLeftTriggerFFB = 1;
    update_state(state);
}

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &xinput_driver;
}
