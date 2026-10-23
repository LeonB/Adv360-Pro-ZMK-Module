#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Hooks used by the `stp` behavior; the rest of the LED control is zmk_rgb_underglow_*(). */
int adv360_status_leds_set_layer(uint8_t layer);
int adv360_status_leds_set_on(bool on);
