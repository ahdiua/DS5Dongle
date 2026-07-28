#ifndef DS5_BRIDGE_STATUS_GPIO_H
#define DS5_BRIDGE_STATUS_GPIO_H

#include <cstdint>

constexpr uint8_t STATUS_GPIO_DISABLED = 0xff;
constexpr uint8_t STATUS_GPIO_MODE_BUTTON = 1;

bool status_gpio_pin_valid(uint8_t pin);
void gpio_on_connect();
void gpio_on_disconnect();

#endif // DS5_BRIDGE_STATUS_GPIO_H
