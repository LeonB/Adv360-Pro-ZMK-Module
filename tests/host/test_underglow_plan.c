/*
 * Table tests for the pure decisions the driver makes: when to wake next (adv360_ug_plan_wake),
 * whether an effect is still animating, and whether the shared LED state is the boot default.
 */
#include <stdio.h>
#include <string.h>

#include <zmk_keyboard_adv360/underglow.h>

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
    return ug;
}

int main(void) {
    for (int effect = 0; effect < ADV360_UG_EFFECT_NUMBER; effect++) {
        const bool animated = effect == ADV360_UG_EFFECT_BREATHE || effect == ADV360_UG_EFFECT_SPECTRUM ||
                              effect == ADV360_UG_EFFECT_SWIRL || effect == ADV360_UG_EFFECT_TEST;

        for (int limit = -1; limit <= ADV360_BLINK_SLOW_LIMIT; limit += (limit < 0 ? 1 : 11)) {
            for (uint32_t elapsed = 0; elapsed < 1600; elapsed += 37) {
                struct adv360_ug off = make(effect, false);
                struct adv360_ug on = make(effect, true);
                const struct adv360_wake_plan p_off = adv360_ug_plan_wake(&off, limit, elapsed);
                const struct adv360_wake_plan p_on = adv360_ug_plan_wake(&on, limit, elapsed);

                /* LEDs off: never wake. */
                EXPECT(p_off.kind == ADV360_WAKE_NONE, "off e=%d l=%d", effect, limit);

                if (animated) {
                    /* Animated effects tick every 50 ms and ignore any blink state. */
                    EXPECT(p_on.kind == ADV360_WAKE_TICK && p_on.delay_ms == ADV360_BLINK_TICK_MS,
                           "animated e=%d l=%d", effect, limit);
                } else if (effect == ADV360_UG_EFFECT_KINESIS && limit >= 0) {
                    EXPECT(p_on.kind == ADV360_WAKE_BLINK_EDGE &&
                               p_on.delay_ms == adv360_blink_next_edge_ms(limit, elapsed),
                           "blink l=%d el=%u", limit, elapsed);
                } else {
                    /* Steady, or a blink state the effect does not show (battery, solid). */
                    EXPECT(p_on.kind == ADV360_WAKE_NONE, "steady e=%d l=%d", effect, limit);
                }
            }
        }
    }

    /* The self-test sweeps, then stops once it has reached its final all-white frame. */
    {
        struct adv360_ug ug = make(ADV360_UG_EFFECT_TEST, true);

        ug.animation_step = 0;
        EXPECT(adv360_ug_animating(&ug), "test starts animating");
        ug.animation_step = ADV360_UG_HUE_MAX * 3;
        EXPECT(adv360_ug_animating(&ug), "test still animating at 3 hue turns");
        ug.animation_step = ADV360_UG_HUE_MAX * 3 + 1;
        EXPECT(!adv360_ug_animating(&ug), "test done past 3 hue turns");
        EXPECT(adv360_ug_plan_wake(&ug, -1, 0).kind == ADV360_WAKE_NONE, "no wake once test done");

        /* Breathing never finishes, whatever its step. */
        ug = make(ADV360_UG_EFFECT_BREATHE, true);
        ug.animation_step = 5000;
        EXPECT(adv360_ug_animating(&ug), "breathe keeps animating");
    }

    /* is_default: effect, colour and on/off, but not speed. */
    {
        struct adv360_ug ug;

        adv360_ug_init(&ug, &cfg);
        EXPECT(adv360_ug_is_default(&ug), "fresh state is default");
        ug.speed = 5;
        EXPECT(adv360_ug_is_default(&ug), "speed does not count");
        adv360_ug_off(&ug);
        EXPECT(!adv360_ug_is_default(&ug), "off differs");
        adv360_ug_on(&ug);
        EXPECT(adv360_ug_is_default(&ug), "on again is default");
        adv360_ug_select_effect(&ug, ADV360_UG_EFFECT_SOLID);
        EXPECT(!adv360_ug_is_default(&ug), "other effect differs");
        adv360_ug_select_effect(&ug, cfg.eff_start);
        adv360_ug_change_hue(&ug, 1);
        EXPECT(!adv360_ug_is_default(&ug), "hue differs");
        adv360_ug_change_hue(&ug, -1);
        EXPECT(adv360_ug_is_default(&ug), "hue back is default");
        adv360_ug_change_sat(&ug, -1);
        EXPECT(!adv360_ug_is_default(&ug), "saturation differs");
        adv360_ug_change_sat(&ug, 1);
        adv360_ug_change_brt(&ug, -1);
        EXPECT(!adv360_ug_is_default(&ug), "brightness differs");
    }

    printf("underglow plan: %lu checks, %lu failures\n", checks, failures);
    return failures ? 1 : 0;
}
