/*
 * Battery peek (`&stp STP_BAT`): the one place the module deliberately improves on the official
 * firmware. Where the official behavior already works (LEDs on, status effect) the result must be
 * identical, which is checked against the fork's own select-effect function; everywhere else the
 * state machine must restore exactly what was there, whatever the order of presses and releases.
 */
#include <stdio.h>

#include <zmk_keyboard_adv360/underglow.h>

struct fork_state_view {
    uint16_t h;
    uint8_t s, b, speed, effect;
    uint16_t step;
    bool on;
};
void fork_left_reset(void);
void fork_left_state(struct fork_state_view *);
int fork_left_cmd(int, int, int, int);
enum { C_SELECT = 3 };

static unsigned long checks, failures;

#define EXPECT(cond, ...)                                                                          \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            if (failures++ < 10) {                                                                 \
                printf("FAIL line %d: ", __LINE__);                                                \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

static const struct adv360_ug_config cfg = {
    .hue_start = 0, .sat_start = 100, .brt_start = 100, .spd_start = 3, .eff_start = 4,
    .on_start = true, .hue_step = 10, .sat_step = 10, .brt_step = 10, .brt_min = 0, .brt_max = 100,
};

static struct adv360_ug make(int effect, bool on) {
    struct adv360_ug ug;

    adv360_ug_init(&ug, &cfg);
    ug.effect = effect;
    ug.on = on;
    ug.animation_step = 0;
    return ug;
}

int main(void) {
    /* Parity: LEDs on + status effect, the case where the official behavior works. */
    {
        struct adv360_ug ug = make(ADV360_UG_EFFECT_KINESIS, true);
        struct adv360_ug_peek peek = {0};
        struct fork_state_view f;

        fork_left_reset();
        fork_left_cmd(C_SELECT, 5, 0, 0);
        adv360_ug_battery_peek_begin(&ug, &peek);
        fork_left_state(&f);
        EXPECT(f.effect == ug.effect && f.on == ug.on && f.step == ug.animation_step,
               "pressed: fork eff=%d on=%d, module eff=%d on=%d", f.effect, f.on, ug.effect, ug.on);

        fork_left_cmd(C_SELECT, 4, 0, 0);
        adv360_ug_battery_peek_end(&ug, &peek);
        fork_left_state(&f);
        EXPECT(f.effect == ug.effect && f.on == ug.on && f.step == ug.animation_step &&
                   f.h == ug.color.h && f.s == ug.color.s && f.b == ug.color.b,
               "released: fork eff=%d on=%d, module eff=%d on=%d", f.effect, f.on, ug.effect, ug.on);
    }

    for (int effect = 0; effect < ADV360_UG_EFFECT_NUMBER; effect++) {
        for (int on = 0; on < 2; on++) {
            struct adv360_ug ug = make(effect, on);
            struct adv360_ug_peek peek = {0};
            const struct adv360_ug before = ug;

            /* Plain press and release: battery shown (even if off), then exactly restored. */
            adv360_ug_battery_peek_begin(&ug, &peek);
            EXPECT(ug.effect == ADV360_UG_EFFECT_BATTERY && ug.on, "begin e=%d on=%d", effect, on);
            adv360_ug_battery_peek_end(&ug, &peek);
            EXPECT(ug.effect == before.effect && ug.on == before.on && ug.color.h == before.color.h &&
                       ug.color.s == before.color.s && ug.color.b == before.color.b &&
                       ug.speed == before.speed,
                   "restore e=%d on=%d -> e=%d on=%d", effect, on, ug.effect, ug.on);

            /* A second press while held must not overwrite what the first one saved. */
            ug = before;
            peek = (struct adv360_ug_peek){0};
            adv360_ug_battery_peek_begin(&ug, &peek);
            adv360_ug_battery_peek_begin(&ug, &peek);
            adv360_ug_battery_peek_end(&ug, &peek);
            EXPECT(ug.effect == before.effect && ug.on == before.on, "double press e=%d on=%d", effect, on);

            /* A release with no press is ignored, and so is a second release. */
            ug = before;
            peek = (struct adv360_ug_peek){0};
            adv360_ug_battery_peek_end(&ug, &peek);
            EXPECT(ug.effect == before.effect && ug.on == before.on, "stray release e=%d on=%d", effect, on);
            adv360_ug_battery_peek_begin(&ug, &peek);
            adv360_ug_battery_peek_end(&ug, &peek);
            adv360_ug_battery_peek_end(&ug, &peek);
            EXPECT(ug.effect == before.effect && ug.on == before.on, "double release e=%d on=%d", effect, on);

            /* The effect is changed during the hold: that change wins. */
            for (int other = 0; other < ADV360_UG_EFFECT_NUMBER; other++) {
                if (other == ADV360_UG_EFFECT_BATTERY) {
                    continue;
                }
                ug = before;
                peek = (struct adv360_ug_peek){0};
                adv360_ug_battery_peek_begin(&ug, &peek);
                adv360_ug_select_effect(&ug, other);
                adv360_ug_battery_peek_end(&ug, &peek);
                EXPECT(ug.effect == other, "changed during hold e=%d -> %d got %d", effect, other, ug.effect);
            }

            /* LEDs toggled off during the hold: they stay off and the effect is restored. */
            ug = before;
            peek = (struct adv360_ug_peek){0};
            adv360_ug_battery_peek_begin(&ug, &peek);
            adv360_ug_off(&ug);
            adv360_ug_battery_peek_end(&ug, &peek);
            EXPECT(!ug.on && ug.effect == before.effect, "toggled off during hold e=%d on=%d", effect, on);
        }
    }

    /* The peek renders the battery colours even though the LEDs were off before. */
    {
        struct adv360_ug ug = make(ADV360_UG_EFFECT_KINESIS, false);
        struct adv360_ug_peek peek = {0};
        struct adv360_rgb out[3];
        const struct adv360_status_input ctx = {.side = ADV360_SIDE_LEFT, .battery_soc = 90,
                                                .brightness_permille = 1000};

        adv360_ug_battery_peek_begin(&ug, &peek);
        adv360_ug_tick(&ug, &ctx, out);
        EXPECT(ug.on && out[0].g == 255 && out[0].r == 0 && out[1].g == 255 && out[2].g == 255,
               "battery 90%% from off should be green: %u,%u,%u", out[0].r, out[0].g, out[0].b);
    }

    printf("battery peek: %lu checks, %lu failures\n", checks, failures);
    return failures ? 1 : 0;
}
