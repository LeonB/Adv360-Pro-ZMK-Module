/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zmk_keyboard_adv360/status_render.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static struct adv360_rgb scale_rgb(uint32_t hex, uint16_t permille) {
    return (struct adv360_rgb){
        .r = (uint8_t)((((hex >> 16) & 0xFF) * permille) / 1000),
        .g = (uint8_t)((((hex >> 8) & 0xFF) * permille) / 1000),
        .b = (uint8_t)((((hex >> 0) & 0xFF) * permille) / 1000),
    };
}

static const uint32_t layer_colors[] = {
    0x000000, 0xFFFFFF, 0x0000FF, 0x00FF00, 0xFF0000, 0xFF00FF, 0x00FFFF, 0xFFFF00,
};

static void layer_to_color_indices(uint8_t layer, uint8_t *left, uint8_t *right) {
    if (layer < ARRAY_LEN(layer_colors)) {
        *left = layer;
        *right = layer;
        return;
    }

    *left = 1 + ((layer - ARRAY_LEN(layer_colors)) / 6);
    *right = 1 + ((layer - ARRAY_LEN(layer_colors)) % 6);

    if (*left <= *right) {
        *right += 1;
    }

    *left %= ARRAY_LEN(layer_colors);
    *right %= ARRAY_LEN(layer_colors);
}

/* Bluetooth profile 0-3 colours (profile 4 and above have no colour and stay dark). */
static const uint32_t bt_colors[] = {0xFFFFFF, 0x0000FF, 0xFF0000, 0x00FF00};

/*
 * True while a blinking LED is in its dark phase. The official firmware flips a shared counter
 * every (limit + 1) 50 ms ticks, starting dark.
 */
static bool blink_dark(uint32_t tick, uint8_t limit) { return ((tick / (limit + 1)) % 2) == 0; }

static struct adv360_rgb battery_color(uint8_t soc, uint16_t permille) {
    if (soc >= 80) {
        return scale_rgb(0x00FF00, permille);
    } else if (soc >= 50) {
        return scale_rgb(0xFFFF00, permille);
    } else if (soc >= 20) {
        return scale_rgb(0xFF8C00, permille);
    } else {
        return scale_rgb(0xFF0000, permille);
    }
}

int adv360_status_blink_limit(const struct adv360_status_input *in) {
    switch (in->side) {
    case ADV360_SIDE_LEFT:
        if (in->bt_profile_open) {
            return ADV360_BLINK_FAST_LIMIT;
        }
        return in->bt_profile_connected ? -1 : ADV360_BLINK_SLOW_LIMIT;
    case ADV360_SIDE_RIGHT:
        if (!in->link_bonded) {
            return ADV360_BLINK_FAST_LIMIT;
        }
        return in->link_connected ? -1 : ADV360_BLINK_SLOW_LIMIT;
    default:
        return -1;
    }
}

uint32_t adv360_blink_next_edge_ms(uint8_t limit, uint32_t elapsed_ms) {
    const uint32_t phase_ms = (uint32_t)(limit + 1) * ADV360_BLINK_TICK_MS;

    return phase_ms - (elapsed_ms % phase_ms);
}

void adv360_status_render(const struct adv360_status_input *in,
                          struct adv360_rgb out[ADV360_STATUS_LED_COUNT]) {
    uint8_t left_color;
    uint8_t right_color;

    memset(out, 0, sizeof(out[0]) * ADV360_STATUS_LED_COUNT);

    if (!in->enabled && !in->battery_preview) {
        return;
    }

    if (in->battery_preview) {
        struct adv360_rgb color = battery_color(in->battery_soc, in->brightness_permille);
        for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
            out[i] = color;
        }
        return;
    }

    layer_to_color_indices(in->layer, &left_color, &right_color);

    const struct adv360_rgb lock = scale_rgb(in->lock_color, in->brightness_permille);
    const struct adv360_rgb off = scale_rgb(0x000000, in->brightness_permille);

    switch (in->side) {
    case ADV360_SIDE_LEFT: {
        const int limit = adv360_status_blink_limit(in);
        const bool blinking = limit >= 0 && blink_dark(in->tick, limit);

        out[0] = in->caps_lock ? lock : off;
        out[1] = (in->bt_profile < ARRAY_LEN(bt_colors) && !blinking)
                     ? scale_rgb(bt_colors[in->bt_profile], in->brightness_permille)
                     : off;
        out[2] = scale_rgb(layer_colors[left_color], in->brightness_permille);
        break;
    }
    case ADV360_SIDE_RIGHT: {
        const int limit = adv360_status_blink_limit(in);
        const bool blinking = limit >= 0 && blink_dark(in->tick, limit);

        out[0] = scale_rgb(layer_colors[right_color], in->brightness_permille);
        out[1] = in->scroll_lock ? lock : off;
        out[2] = in->num_lock ? lock : off;

        if (limit >= 0) {
            /* A broken link overrides every LED with blinking red. */
            const struct adv360_rgb color = blinking ? off : scale_rgb(0xFF0000, in->brightness_permille);
            for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
                out[i] = color;
            }
        }
        break;
    }
    default:
        out[0] = scale_rgb(layer_colors[left_color], in->brightness_permille);
        break;
    }
}
