#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Internal `stp` parameters, never written in a keymap (the keymap-facing ones are in stp.h). */
#define ADV360_STP_SET_LAYER 2
#define ADV360_STP_SET_ON 3

/* Hooks used by the `stp` behavior; the rest of the LED control is zmk_rgb_underglow_*(). */
int adv360_status_leds_set_layer(uint8_t layer);
int adv360_status_leds_set_on(bool on);
