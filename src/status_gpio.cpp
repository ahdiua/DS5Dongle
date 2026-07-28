#include "status_gpio.h"

#include "config.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#define BUTTON_PRESS_MS 200

static volatile alarm_id_t button_alarm_id;

bool status_gpio_pin_valid(const uint8_t pin) {
    if (pin == STATUS_GPIO_DISABLED) return true;
    if (pin >= NUM_BANK0_GPIOS) return false;

#ifdef PICO_DEFAULT_UART_TX_PIN
    if (pin == PICO_DEFAULT_UART_TX_PIN) return false;
#endif
#ifdef PICO_DEFAULT_UART_RX_PIN
    if (pin == PICO_DEFAULT_UART_RX_PIN) return false;
#endif
#ifdef PICO_VSYS_PIN
    if (pin == PICO_VSYS_PIN) return false;
#endif
#ifdef PICO_VBUS_PIN
    if (pin == PICO_VBUS_PIN) return false;
#endif
#ifdef PICO_SMPS_MODE_PIN
    if (pin == PICO_SMPS_MODE_PIN) return false;
#endif
#ifdef CYW43_DEFAULT_PIN_WL_REG_ON
    if (pin == CYW43_DEFAULT_PIN_WL_REG_ON) return false;
#endif
#ifdef CYW43_DEFAULT_PIN_WL_DATA_OUT
    if (pin == CYW43_DEFAULT_PIN_WL_DATA_OUT) return false;
#endif
#ifdef CYW43_DEFAULT_PIN_WL_DATA_IN
    if (pin == CYW43_DEFAULT_PIN_WL_DATA_IN) return false;
#endif
#ifdef CYW43_DEFAULT_PIN_WL_HOST_WAKE
    if (pin == CYW43_DEFAULT_PIN_WL_HOST_WAKE) return false;
#endif
#ifdef CYW43_DEFAULT_PIN_WL_CLOCK
    if (pin == CYW43_DEFAULT_PIN_WL_CLOCK) return false;
#endif
#ifdef CYW43_DEFAULT_PIN_WL_CS
    if (pin == CYW43_DEFAULT_PIN_WL_CS) return false;
#endif

    return true;
}

void gpio_on_connect() {
    gpio_on_disconnect();

    const auto &config = get_config();
    if (config.status_gpio_pin == STATUS_GPIO_DISABLED) return;

    gpio_put(config.status_gpio_pin, true);

    if (config.status_gpio_mode == STATUS_GPIO_MODE_BUTTON) {
        button_alarm_id = add_alarm_in_ms(BUTTON_PRESS_MS, [](alarm_id_t, void *user_data) -> int64_t {
            gpio_put((uint) (uintptr_t) user_data, false);
            button_alarm_id = 0;
            return 0;
        }, (void *) (uintptr_t) config.status_gpio_pin, false);

        if (button_alarm_id <= 0) {
            button_alarm_id = 0;
            gpio_put(config.status_gpio_pin, false);
        }
    }
}

void gpio_on_disconnect() {
    if (button_alarm_id > 0) {
        cancel_alarm(button_alarm_id);
        button_alarm_id = 0;
    }

    const auto &config = get_config();
    if (config.status_gpio_pin == STATUS_GPIO_DISABLED) return;

    gpio_init(config.status_gpio_pin);
    gpio_put(config.status_gpio_pin, false);
    gpio_set_dir(config.status_gpio_pin, GPIO_OUT);
}
