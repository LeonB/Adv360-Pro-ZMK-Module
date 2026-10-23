/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Pure (no Zephyr/ZMK dependencies) RGB underglow engine for the three Adv360 LEDs: the effect
 * list, HSB colour state and the commands behind `&rgb_ug`, with the semantics of the Kinesis fork
 * of ZMK (refil/zmk, app/src/rgb_underglow.c). The Zephyr glue in src/adv360_status_leds.c drives it
 * from a timer and ZMK events; the host tests drive it directly.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zmk_keyboard_adv360/status_render.h>

#define ADV360_UG_HUE_MAX 360
#define ADV360_UG_SAT_MAX 100
#define ADV360_UG_BRT_MAX 100
#define ADV360_UG_SPD_MIN 1
#define ADV360_UG_SPD_MAX 5

/* Order and numbering match the fork: `&rgb_ug RGB_EFS n` and `stp` depend on it. */
enum adv360_ug_effect {
    ADV360_UG_EFFECT_SOLID,
    ADV360_UG_EFFECT_BREATHE,
    ADV360_UG_EFFECT_SPECTRUM,
    ADV360_UG_EFFECT_SWIRL,
    ADV360_UG_EFFECT_KINESIS, /* layer / lock / Bluetooth / link indicators (the official default) */
    ADV360_UG_EFFECT_BATTERY,
    ADV360_UG_EFFECT_TEST,
    ADV360_UG_EFFECT_NUMBER,
};

struct adv360_ug_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

struct adv360_ug_config {
    uint16_t hue_start;
    uint8_t sat_start;
    uint8_t brt_start;
    uint8_t spd_start;
    uint8_t eff_start;
    bool on_start;
    uint16_t hue_step;
    uint8_t sat_step;
    uint8_t brt_step;
    uint8_t brt_min;
    uint8_t brt_max;
};

struct adv360_ug {
    const struct adv360_ug_config *cfg;
    struct adv360_ug_hsb color;
    uint8_t speed;
    uint8_t effect;
    uint16_t animation_step;
    bool on;
};

void adv360_ug_init(struct adv360_ug *ug, const struct adv360_ug_config *cfg);

/* State changes only: switching the LED power and the animation timer is the glue's job. */
void adv360_ug_on(struct adv360_ug *ug);
void adv360_ug_off(struct adv360_ug *ug);

int adv360_ug_calc_effect(const struct adv360_ug *ug, int direction);
int adv360_ug_select_effect(struct adv360_ug *ug, int effect);
int adv360_ug_cycle_effect(struct adv360_ug *ug, int direction);

int adv360_ug_set_hsb(struct adv360_ug *ug, struct adv360_ug_hsb color);
struct adv360_ug_hsb adv360_ug_calc_hue(const struct adv360_ug *ug, int direction);
struct adv360_ug_hsb adv360_ug_calc_sat(const struct adv360_ug *ug, int direction);
struct adv360_ug_hsb adv360_ug_calc_brt(const struct adv360_ug *ug, int direction);
void adv360_ug_change_hue(struct adv360_ug *ug, int direction);
void adv360_ug_change_sat(struct adv360_ug *ug, int direction);
void adv360_ug_change_brt(struct adv360_ug *ug, int direction);
void adv360_ug_change_spd(struct adv360_ug *ug, int direction);

/* Is `effect` one that changes over time at all? */
bool adv360_ug_effect_animates(uint8_t effect);

/*
 * Does the *current* frame sequence still change? Like adv360_ug_effect_animates(), but the
 * self-test effect stops changing once its sweep has reached the final all-white frame, so the
 * timer can stop too. When this is false, further ticks render exactly the same frame.
 */
bool adv360_ug_animating(const struct adv360_ug *ug);

/* Is the shared LED state (effect, colour, on/off) what a freshly booted half starts with? */
bool adv360_ug_is_default(const struct adv360_ug *ug);

enum adv360_wake {
    ADV360_WAKE_NONE,       /* nothing will change on its own: no timer */
    ADV360_WAKE_TICK,       /* an animation needs the next tick: keep a pending timer */
    ADV360_WAKE_BLINK_EDGE, /* an indicator blinks: wake when its phase next changes */
};

struct adv360_wake_plan {
    enum adv360_wake kind;
    uint32_t delay_ms;
};

/*
 * When must the glue next redraw on its own? `blink_limit` is adv360_status_blink_limit() of the
 * current state (-1 for none) and only counts for the status effect; `blink_elapsed_ms` is the time
 * since that blinking began.
 */
struct adv360_wake_plan adv360_ug_plan_wake(const struct adv360_ug *ug, int blink_limit,
                                            uint32_t blink_elapsed_ms);

/*
 * Advance the current effect by one 50 ms tick and render it. `ctx` supplies what the indicator
 * effects show (layer, locks, Bluetooth/link state, battery, blink tick, colour scale).
 */
void adv360_ug_tick(struct adv360_ug *ug, const struct adv360_status_input *ctx,
                    struct adv360_rgb out[ADV360_STATUS_LED_COUNT]);
