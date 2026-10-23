/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Pure (no Zephyr/ZMK dependencies) renderer for the three Adv360 status LEDs, so the colour
 * logic can be unit-tested on the host. The driver in src/adv360_status_leds.c gathers the
 * inputs from ZMK events and hands the resulting pixels to the LED strip.
 */

#include <stdbool.h>
#include <stdint.h>

#define ADV360_STATUS_LED_COUNT 3

/* A blink phase lasts (limit + 1) ticks: unpaired blinks fast, disconnected blinks slowly. */
#define ADV360_BLINK_FAST_LIMIT 2
#define ADV360_BLINK_SLOW_LIMIT 13

struct adv360_rgb {
    uint8_t r, g, b;
};

enum adv360_side {
    ADV360_SIDE_LEFT,
    ADV360_SIDE_RIGHT,
    /* Neither Adv360 half (e.g. a single-board build): only the layer colour is shown. */
    ADV360_SIDE_OTHER,
};

struct adv360_status_input {
    enum adv360_side side;
    bool enabled;
    bool battery_preview;
    uint8_t layer;
    bool caps_lock, num_lock, scroll_lock;
    uint8_t battery_soc;
    /* 0xRRGGBB shown by the Caps/Num/Scroll Lock LEDs (before brightness scaling). */
    uint32_t lock_color;

    /* Left half only: Bluetooth state of the active profile. */
    uint8_t bt_profile;
    bool bt_profile_open;
    bool bt_profile_connected;

    /* Right half only: link to the left half (the split central). */
    bool link_bonded;
    bool link_connected;

    /* 50 ms animation ticks since the LEDs were switched on, counting from 1. Drives blinking. */
    uint32_t tick;
    /* Every colour channel is scaled by brightness_permille / 1000 (0-1000, rounded down). */
    uint16_t brightness_permille;
};

void adv360_status_render(const struct adv360_status_input *in,
                          struct adv360_rgb out[ADV360_STATUS_LED_COUNT]);
