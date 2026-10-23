/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Effect and command logic ported from the Kinesis ZMK fork (refil/zmk, app/src/rgb_underglow.c),
 * which is itself derived from ZMK's rgb_underglow.c.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zmk_keyboard_adv360/underglow.h>

#define CLAMP_INT(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

static struct adv360_ug_hsb hsb_scale_min_max(const struct adv360_ug *ug, struct adv360_ug_hsb hsb) {
    hsb.b = ug->cfg->brt_min +
            (ug->cfg->brt_max - ug->cfg->brt_min) * hsb.b / ADV360_UG_BRT_MAX;
    return hsb;
}

static struct adv360_ug_hsb hsb_scale_zero_max(const struct adv360_ug *ug, struct adv360_ug_hsb hsb) {
    hsb.b = hsb.b * ug->cfg->brt_max / ADV360_UG_BRT_MAX;
    return hsb;
}

static struct adv360_rgb hsb_to_rgb(struct adv360_ug_hsb hsb) {
    float r = 0, g = 0, b = 0;

    uint8_t i = hsb.h / 60;
    float v = hsb.b / ((float)ADV360_UG_BRT_MAX);
    float s = hsb.s / ((float)ADV360_UG_SAT_MAX);
    float f = hsb.h / ((float)ADV360_UG_HUE_MAX) * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    return (struct adv360_rgb){.r = r * 255, .g = g * 255, .b = b * 255};
}

static void effect_solid(struct adv360_ug *ug, struct adv360_rgb *out) {
    for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
        out[i] = hsb_to_rgb(hsb_scale_min_max(ug, ug->color));
    }
}

static void effect_breathe(struct adv360_ug *ug, struct adv360_rgb *out) {
    for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.b = abs(ug->animation_step - 1200) / 12;

        out[i] = hsb_to_rgb(hsb_scale_zero_max(ug, hsb));
    }

    ug->animation_step += ug->speed * 10;

    if (ug->animation_step > 2400) {
        ug->animation_step = 0;
    }
}

static void effect_spectrum(struct adv360_ug *ug, struct adv360_rgb *out) {
    for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.h = ug->animation_step;

        out[i] = hsb_to_rgb(hsb_scale_min_max(ug, hsb));
    }

    ug->animation_step += ug->speed;
    ug->animation_step = ug->animation_step % ADV360_UG_HUE_MAX;
}

static void effect_swirl(struct adv360_ug *ug, struct adv360_rgb *out) {
    for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.h = (ADV360_UG_HUE_MAX / ADV360_STATUS_LED_COUNT * i + ug->animation_step) %
                ADV360_UG_HUE_MAX;

        out[i] = hsb_to_rgb(hsb_scale_min_max(ug, hsb));
    }

    ug->animation_step += ug->speed * 2;
    ug->animation_step = ug->animation_step % ADV360_UG_HUE_MAX;
}

/* Power-on self test: a hue sweep through each LED in turn, ending on all white. */
static void effect_test(struct adv360_ug *ug, struct adv360_rgb *out) {
    struct adv360_rgb rgb = {0, 0, 0};

    for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.h = ug->animation_step;

        out[i] = hsb_to_rgb(hsb_scale_min_max(ug, hsb));
    }
    if (ug->animation_step < (ADV360_UG_HUE_MAX * 3)) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.h = ug->animation_step;
        rgb.r = 0;

        out[0] = rgb;
        out[1] = rgb;
        out[2] = hsb_to_rgb(hsb_scale_min_max(ug, hsb));
    }
    if (ug->animation_step < (ADV360_UG_HUE_MAX * 2)) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.h = ug->animation_step - ADV360_UG_HUE_MAX;
        rgb.r = 0;
        rgb.g = 0;
        rgb.b = 0;
        out[0] = rgb;
        out[1] = hsb_to_rgb(hsb_scale_min_max(ug, hsb));
        out[2] = rgb;
    }
    if (ug->animation_step < ADV360_UG_HUE_MAX) {
        struct adv360_ug_hsb hsb = ug->color;
        hsb.h = ug->animation_step;
        rgb.r = 0;
        rgb.g = 0;
        rgb.b = 0;
        out[0] = hsb_to_rgb(hsb_scale_min_max(ug, hsb));
        out[1] = rgb;
        out[2] = rgb;
    }

    ug->animation_step += 20;
    if (ug->animation_step > (ADV360_UG_HUE_MAX * 3)) {
        rgb.r = 255;
        rgb.g = 255;
        rgb.b = 255;
        for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
            out[i] = rgb;
        }
    }
}

void adv360_ug_init(struct adv360_ug *ug, const struct adv360_ug_config *cfg) {
    *ug = (struct adv360_ug){
        .cfg = cfg,
        .color = {.h = cfg->hue_start, .s = cfg->sat_start, .b = cfg->brt_start},
        .speed = cfg->spd_start,
        .effect = cfg->eff_start,
        .animation_step = 0,
        .on = cfg->on_start,
    };
}

void adv360_ug_on(struct adv360_ug *ug) {
    ug->on = true;
    ug->animation_step = 0;
}

void adv360_ug_off(struct adv360_ug *ug) { ug->on = false; }

int adv360_ug_calc_effect(const struct adv360_ug *ug, int direction) {
    return (ug->effect + ADV360_UG_EFFECT_NUMBER + direction) % ADV360_UG_EFFECT_NUMBER;
}

int adv360_ug_select_effect(struct adv360_ug *ug, int effect) {
    if (effect < 0 || effect >= ADV360_UG_EFFECT_NUMBER) {
        return -EINVAL;
    }

    ug->effect = effect;
    ug->animation_step = 0;
    return 0;
}

int adv360_ug_cycle_effect(struct adv360_ug *ug, int direction) {
    return adv360_ug_select_effect(ug, adv360_ug_calc_effect(ug, direction));
}

int adv360_ug_set_hsb(struct adv360_ug *ug, struct adv360_ug_hsb color) {
    if (color.h > ADV360_UG_HUE_MAX || color.s > ADV360_UG_SAT_MAX || color.b > ADV360_UG_BRT_MAX) {
        return -ENOTSUP;
    }

    ug->color = color;
    return 0;
}

struct adv360_ug_hsb adv360_ug_calc_hue(const struct adv360_ug *ug, int direction) {
    struct adv360_ug_hsb color = ug->color;

    color.h += ADV360_UG_HUE_MAX + (direction * ug->cfg->hue_step);
    color.h %= ADV360_UG_HUE_MAX;

    return color;
}

struct adv360_ug_hsb adv360_ug_calc_sat(const struct adv360_ug *ug, int direction) {
    struct adv360_ug_hsb color = ug->color;

    int s = color.s + (direction * ug->cfg->sat_step);
    if (s < 0) {
        s = 0;
    } else if (s > ADV360_UG_SAT_MAX) {
        s = ADV360_UG_SAT_MAX;
    }
    color.s = s;

    return color;
}

struct adv360_ug_hsb adv360_ug_calc_brt(const struct adv360_ug *ug, int direction) {
    struct adv360_ug_hsb color = ug->color;

    int b = color.b + (direction * ug->cfg->brt_step);
    color.b = CLAMP_INT(b, 0, ADV360_UG_BRT_MAX);

    return color;
}

void adv360_ug_change_hue(struct adv360_ug *ug, int direction) {
    ug->color = adv360_ug_calc_hue(ug, direction);
}

void adv360_ug_change_sat(struct adv360_ug *ug, int direction) {
    ug->color = adv360_ug_calc_sat(ug, direction);
}

void adv360_ug_change_brt(struct adv360_ug *ug, int direction) {
    ug->color = adv360_ug_calc_brt(ug, direction);
}

void adv360_ug_change_spd(struct adv360_ug *ug, int direction) {
    if (ug->speed == 1 && direction < 0) {
        return;
    }

    ug->speed += direction;

    if (ug->speed > ADV360_UG_SPD_MAX) {
        ug->speed = ADV360_UG_SPD_MAX;
    }
}

void adv360_ug_battery_peek_begin(struct adv360_ug *ug, struct adv360_ug_peek *peek) {
    /* A second press while one is held must not overwrite what the first one saved. */
    if (peek->active) {
        return;
    }

    *peek = (struct adv360_ug_peek){.active = true, .effect = ug->effect, .on = ug->on};

    adv360_ug_select_effect(ug, ADV360_UG_EFFECT_BATTERY);
    if (!ug->on) {
        adv360_ug_on(ug);
    }
}

void adv360_ug_battery_peek_end(struct adv360_ug *ug, struct adv360_ug_peek *peek) {
    if (!peek->active) {
        return;
    }
    peek->active = false;

    /* If something else changed the effect during the hold, keep that change. */
    if (ug->effect != ADV360_UG_EFFECT_BATTERY) {
        return;
    }

    adv360_ug_select_effect(ug, peek->effect);
    if (!peek->on && ug->on) {
        adv360_ug_off(ug);
    }
}

bool adv360_ug_effect_animates(uint8_t effect) {
    switch (effect) {
    case ADV360_UG_EFFECT_BREATHE:
    case ADV360_UG_EFFECT_SPECTRUM:
    case ADV360_UG_EFFECT_SWIRL:
    case ADV360_UG_EFFECT_TEST:
        return true;
    default:
        return false;
    }
}

bool adv360_ug_animating(const struct adv360_ug *ug) {
    if (!adv360_ug_effect_animates(ug->effect)) {
        return false;
    }

    /* effect_test() ends on all white once its step passes three full hue turns, and stays there. */
    return !(ug->effect == ADV360_UG_EFFECT_TEST && ug->animation_step > ADV360_UG_HUE_MAX * 3);
}

bool adv360_ug_is_default(const struct adv360_ug *ug) {
    return ug->color.h == ug->cfg->hue_start && ug->color.s == ug->cfg->sat_start &&
           ug->color.b == ug->cfg->brt_start && ug->effect == ug->cfg->eff_start &&
           ug->on == ug->cfg->on_start;
}

struct adv360_wake_plan adv360_ug_plan_wake(const struct adv360_ug *ug, int blink_limit,
                                            uint32_t blink_elapsed_ms) {
    if (!ug->on) {
        return (struct adv360_wake_plan){ADV360_WAKE_NONE, 0};
    }

    if (adv360_ug_animating(ug)) {
        return (struct adv360_wake_plan){ADV360_WAKE_TICK, ADV360_BLINK_TICK_MS};
    }

    if (ug->effect == ADV360_UG_EFFECT_KINESIS && blink_limit >= 0) {
        return (struct adv360_wake_plan){ADV360_WAKE_BLINK_EDGE,
                                         adv360_blink_next_edge_ms(blink_limit, blink_elapsed_ms)};
    }

    return (struct adv360_wake_plan){ADV360_WAKE_NONE, 0};
}

void adv360_ug_tick(struct adv360_ug *ug, const struct adv360_status_input *ctx,
                    struct adv360_rgb out[ADV360_STATUS_LED_COUNT]) {
    struct adv360_status_input in;

    switch (ug->effect) {
    case ADV360_UG_EFFECT_SOLID:
        effect_solid(ug, out);
        break;
    case ADV360_UG_EFFECT_BREATHE:
        effect_breathe(ug, out);
        break;
    case ADV360_UG_EFFECT_SPECTRUM:
        effect_spectrum(ug, out);
        break;
    case ADV360_UG_EFFECT_SWIRL:
        effect_swirl(ug, out);
        break;
    case ADV360_UG_EFFECT_KINESIS:
        in = *ctx;
        in.enabled = true;
        in.battery_preview = false;
        adv360_status_render(&in, out);
        break;
    case ADV360_UG_EFFECT_BATTERY:
        in = *ctx;
        in.enabled = true;
        in.battery_preview = true;
        adv360_status_render(&in, out);
        break;
    case ADV360_UG_EFFECT_TEST:
        effect_test(ug, out);
        break;
    default:
        memset(out, 0, sizeof(out[0]) * ADV360_STATUS_LED_COUNT);
        break;
    }
}
