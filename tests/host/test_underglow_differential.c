/*
 * Differential test of the underglow engine (src/adv360_underglow_engine.c) against the Kinesis
 * fork's own functions (ref/fork_underglow.c).
 *
 * Reproducible random sequences of commands (on / off / toggle / select / cycle / set colour /
 * hue / saturation / brightness / speed, including invalid arguments) and 50 ms ticks run through
 * both implementations. After every step the return code, the full state (colour, speed, effect,
 * animation step, on) and, for ticks, all three pixels must be identical.
 *
 * Bluetooth / split-link state is held healthy here (nothing blinks): blinking depends on a counter
 * that the fork shares with the effects, and is covered by test_fork_differential.
 */
#include <stdio.h>
#include <string.h>

#include <zmk_keyboard_adv360/underglow.h>

struct fork_rgb {
    uint8_t r, g, b;
};

struct fork_world {
    uint8_t soc;
    uint8_t hid_indicators;
    uint8_t highest_layer;
    int ble_profile_index;
    bool ble_profile_open;
    bool ble_profile_connected;
    bool periph_bonded;
    bool periph_connected;
};

struct fork_state_view {
    uint16_t h;
    uint8_t s, b, speed, effect;
    uint16_t step;
    bool on;
};

enum { C_ON, C_OFF, C_TOGGLE, C_SELECT, C_CYCLE, C_SET_HSB, C_HUE, C_SAT, C_BRT, C_SPD };

#define DECLARE_FORK(p)                                                                            \
    void p##reset(void);                                                                           \
    void p##tick(const struct fork_world *, struct fork_rgb[3]);                                   \
    void p##state(struct fork_state_view *);                                                       \
    int p##cmd(int, int, int, int)
DECLARE_FORK(fork_left_);
DECLARE_FORK(fork_right_);
DECLARE_FORK(fork_cfgb_);

struct variant {
    const char *name;
    enum adv360_side side;
    const struct adv360_ug_config *cfg;
    void (*reset)(void);
    void (*tick)(const struct fork_world *, struct fork_rgb[3]);
    void (*state)(struct fork_state_view *);
    int (*cmd)(int, int, int, int);
};

/* ZMK's Kconfig.defaults plus EFF_START=4: what the official firmware runs. */
static const struct adv360_ug_config cfg_a = {
    .hue_start = 0, .sat_start = 100, .brt_start = 100, .spd_start = 3, .eff_start = 4,
    .on_start = true, .hue_step = 10, .sat_step = 10, .brt_step = 10, .brt_min = 0, .brt_max = 100,
};
/* Non-default limits, steps and start values (matches -DUG_CONFIG_B in ref/fork_stubs.h). */
static const struct adv360_ug_config cfg_b = {
    .hue_start = 100, .sat_start = 50, .brt_start = 60, .spd_start = 2, .eff_start = 1,
    .on_start = false, .hue_step = 7, .sat_step = 13, .brt_step = 9, .brt_min = 20, .brt_max = 80,
};

static const struct variant variants[] = {
    {"central/A", ADV360_SIDE_LEFT, &cfg_a, fork_left_reset, fork_left_tick, fork_left_state, fork_left_cmd},
    {"peripheral/A", ADV360_SIDE_RIGHT, &cfg_a, fork_right_reset, fork_right_tick, fork_right_state, fork_right_cmd},
    {"central/B", ADV360_SIDE_LEFT, &cfg_b, fork_cfgb_reset, fork_cfgb_tick, fork_cfgb_state, fork_cfgb_cmd},
};

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static unsigned long steps, failures, ticks_per_effect[ADV360_UG_EFFECT_NUMBER];

static void fail(const struct variant *v, int seq, int step, const char *what) {
    if (failures++ < 10) {
        printf("MISMATCH %s seq=%d step=%d: %s\n", v->name, seq, step, what);
    }
}

static bool states_equal(const struct adv360_ug *ug, const struct fork_state_view *f) {
    return ug->color.h == f->h && ug->color.s == f->s && ug->color.b == f->b &&
           ug->speed == f->speed && ug->effect == f->effect &&
           ug->animation_step == f->step && ug->on == f->on;
}

static void run_sequence(const struct variant *v, int seq) {
    struct adv360_ug ug;
    struct fork_state_view fs;

    v->reset();
    adv360_ug_init(&ug, v->cfg);
    v->state(&fs);
    if (!states_equal(&ug, &fs)) {
        fail(v, seq, -1, "initial state differs");
    }

    for (int step = 0; step < 150; step++) {
        const unsigned pick = rnd() % 100;
        int want_rc = 0, got_rc = 0;
        char what[96] = "";

        if (pick < 45) {
            struct fork_world w = {
                .soc = rnd() % 101,
                .hid_indicators = rnd() % 8,
                .highest_layer = rnd() % 32,
                .ble_profile_index = rnd() % 5,
                .ble_profile_connected = true,
                .periph_bonded = true,
                .periph_connected = true,
            };
            struct fork_rgb want[3];
            struct adv360_rgb got[3];
            const struct adv360_status_input ctx = {
                .side = v->side,
                .layer = w.highest_layer,
                .num_lock = w.hid_indicators & 1,
                .caps_lock = w.hid_indicators & 2,
                .scroll_lock = w.hid_indicators & 4,
                .battery_soc = w.soc,
                .lock_color = 0xFFFFFF,
                .bt_profile = w.ble_profile_index,
                .bt_profile_connected = true,
                .link_bonded = true,
                .link_connected = true,
                .brightness_permille = 4,
            };

            v->tick(&w, want);
            adv360_ug_tick(&ug, &ctx, got);
            ticks_per_effect[ug.effect < ADV360_UG_EFFECT_NUMBER ? ug.effect : 0]++;
            steps++;
            if (memcmp(want, got, sizeof(want)) != 0) {
                snprintf(what, sizeof(what), "tick pixels differ (effect %d)", ug.effect);
                fail(v, seq, step, what);
            }
        } else {
            int cmd, a = 0, b = 0, d = 0;

            switch (pick % 10) {
            case 0: cmd = C_ON; break;
            case 1: cmd = C_OFF; break;
            case 2: cmd = C_TOGGLE; break;
            case 3: cmd = C_SELECT; a = (int)(rnd() % 10) - 1; break; /* -1..8: includes invalid */
            case 4: cmd = C_CYCLE; a = (rnd() & 1) ? 1 : -1; break;
            case 5:
                cmd = C_SET_HSB;
                a = rnd() % 380; /* up to 379: values over 360 must be rejected */
                b = rnd() % 105;
                d = rnd() % 105;
                break;
            case 6: cmd = C_HUE; a = (rnd() & 1) ? 1 : -1; break;
            case 7: cmd = (rnd() & 1) ? C_SAT : C_BRT; a = (rnd() & 1) ? 1 : -1; break;
            case 8: cmd = C_SPD; a = (rnd() & 1) ? 1 : -1; break;
            default: cmd = C_SELECT; a = rnd() % 7; break;
            }

            want_rc = v->cmd(cmd, a, b, d);

            switch (cmd) {
            case C_ON: adv360_ug_on(&ug); break;
            case C_OFF: adv360_ug_off(&ug); break;
            case C_TOGGLE:
                if (ug.on) {
                    adv360_ug_off(&ug);
                } else {
                    adv360_ug_on(&ug);
                }
                break;
            case C_SELECT: got_rc = adv360_ug_select_effect(&ug, a); break;
            case C_CYCLE: got_rc = adv360_ug_cycle_effect(&ug, a); break;
            case C_SET_HSB:
                got_rc = adv360_ug_set_hsb(&ug, (struct adv360_ug_hsb){.h = a, .s = b, .b = d});
                break;
            case C_HUE: adv360_ug_change_hue(&ug, a); break;
            case C_SAT: adv360_ug_change_sat(&ug, a); break;
            case C_BRT: adv360_ug_change_brt(&ug, a); break;
            case C_SPD: adv360_ug_change_spd(&ug, a); break;
            }
            steps++;

            /* Only the commands that can fail have a meaningful return code to compare. */
            if ((cmd == C_SELECT || cmd == C_CYCLE || cmd == C_SET_HSB) && want_rc != got_rc) {
                snprintf(what, sizeof(what), "cmd %d(%d,%d,%d) rc fork=%d module=%d", cmd, a, b, d,
                         want_rc, got_rc);
                fail(v, seq, step, what);
            }
        }

        v->state(&fs);
        if (!states_equal(&ug, &fs)) {
            snprintf(what, sizeof(what),
                     "state fork={h%u s%u b%u spd%u eff%u step%u on%d} module={h%u s%u b%u spd%u eff%u step%u on%d}",
                     fs.h, fs.s, fs.b, fs.speed, fs.effect, fs.step, fs.on, ug.color.h, ug.color.s,
                     ug.color.b, ug.speed, ug.effect, ug.animation_step, ug.on);
            fail(v, seq, step, what);
        }
    }
}

/*
 * Long deterministic runs: every effect at every speed, uninterrupted, so that breathe wraps
 * around, spectrum / swirl cycle several times and the self-test sweep reaches its final all-white
 * stage (which random sequences re-select away from too early to ever see).
 */
static void run_long(const struct variant *v) {
    for (int effect = 0; effect < ADV360_UG_EFFECT_NUMBER; effect++) {
        for (int speed = 1; speed <= ADV360_UG_SPD_MAX; speed++) {
            struct adv360_ug ug;
            struct fork_state_view fs;
            const struct fork_world w = {.soc = 55, .hid_indicators = 3, .highest_layer = 2,
                                         .ble_profile_connected = true, .periph_bonded = true,
                                         .periph_connected = true};
            const struct adv360_status_input ctx = {
                .side = v->side, .layer = 2, .num_lock = 1, .caps_lock = 1, .battery_soc = 55,
                .lock_color = 0xFFFFFF, .bt_profile_connected = true, .link_bonded = true,
                .link_connected = true, .brightness_permille = 4,
            };

            v->reset();
            adv360_ug_init(&ug, v->cfg);
            v->cmd(C_SELECT, effect, 0, 0);
            adv360_ug_select_effect(&ug, effect);
            while (ug.speed < speed) {
                v->cmd(C_SPD, 1, 0, 0);
                adv360_ug_change_spd(&ug, 1);
            }
            while (ug.speed > speed) {
                v->cmd(C_SPD, -1, 0, 0);
                adv360_ug_change_spd(&ug, -1);
            }

            for (int t = 0; t < 700; t++) {
                struct fork_rgb want[3];
                struct adv360_rgb got[3];

                v->tick(&w, want);
                adv360_ug_tick(&ug, &ctx, got);
                ticks_per_effect[effect]++;
                steps++;
                v->state(&fs);
                if (memcmp(want, got, sizeof(want)) != 0 || !states_equal(&ug, &fs)) {
                    char what[64];
                    snprintf(what, sizeof(what), "long run effect=%d speed=%d tick=%d", effect, speed, t);
                    fail(v, -2, t, what);
                }
            }
        }
    }
}

int main(void) {
    rng_state = 0x2545F491u;

    for (unsigned vi = 0; vi < sizeof(variants) / sizeof(variants[0]); vi++) {
        run_long(&variants[vi]);
    }

    for (unsigned vi = 0; vi < sizeof(variants) / sizeof(variants[0]); vi++) {
        for (int seq = 0; seq < 2000; seq++) {
            run_sequence(&variants[vi], seq);
        }
    }

    printf("ticks per effect:");
    for (int e = 0; e < ADV360_UG_EFFECT_NUMBER; e++) {
        printf(" %d=%lu", e, ticks_per_effect[e]);
    }
    printf("\nunderglow differential: %lu steps, %lu mismatches\n", steps, failures);
    return failures ? 1 : 0;
}
