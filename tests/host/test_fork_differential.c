/*
 * Differential test: the module's renderer must match the official Kinesis firmware (the fork's
 * "kinesis" and battery effects, ref/fork_kinesis.c) pixel for pixel, tick by tick, for every
 * steady-state scenario.
 *
 * The fork keeps a shared blink counter, so the oracle is driven from a fresh reset for each
 * scenario with the state held constant, and the module is asked for the same tick number.
 */
#include <stdio.h>
#include <string.h>

#include <zmk_keyboard_adv360/status_render.h>

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

void fork_reset_left(void);
void fork_tick_left(const struct fork_world *, struct fork_rgb[3]);
void fork_battery_left(uint8_t, struct fork_rgb[3]);
void fork_reset_right(void);
void fork_tick_right(const struct fork_world *, struct fork_rgb[3]);
void fork_battery_right(uint8_t, struct fork_rgb[3]);

/* The official firmware's effective scale: CONFIG_ZMK_RGB_UNDERGLOW_BRT_SCALE (1) / 250. */
#define FORK_PERMILLE 4

#define TICKS 60 /* 3 s: several fast and slow blink periods */

static unsigned long cases, failures;

static void report(const char *what, const struct fork_world *w, unsigned tick,
                   const struct fork_rgb *want, const struct adv360_rgb *got) {
    if (failures++ >= 12) {
        return;
    }
    printf("MISMATCH %s tick=%u layer=%u ind=%u prof=%d open=%d conn=%d bonded=%d lconn=%d\n", what,
           tick, w->highest_layer, w->hid_indicators, w->ble_profile_index, w->ble_profile_open,
           w->ble_profile_connected, w->periph_bonded, w->periph_connected);
    for (int i = 0; i < 3; i++) {
        printf("   led%d fork=(%u,%u,%u) module=(%u,%u,%u)%s\n", i, want[i].r, want[i].g, want[i].b,
               got[i].r, got[i].g, got[i].b,
               memcmp(&want[i], &got[i], 3) ? "   <--" : "");
    }
}

static void check_side(bool left) {
    for (unsigned layer = 0; layer < 32; layer++)
        for (unsigned ind = 0; ind < 8; ind++)
            for (int state = 0; state < (left ? 10 : 3); state++) {
                struct fork_world w = {
                    .highest_layer = layer,
                    .hid_indicators = ind,
                };
                struct adv360_status_input in = {
                    .side = left ? ADV360_SIDE_LEFT : ADV360_SIDE_RIGHT,
                    .enabled = true,
                    .layer = layer,
                    .num_lock = ind & 1,
                    .caps_lock = ind & 2,
                    .scroll_lock = ind & 4,
                    .lock_color = 0xFFFFFF,
                    .brightness_permille = FORK_PERMILLE,
                };

                if (left) {
                    /* states 0-4: connected profile 0-4; 5: open (unpaired); 6-9: disconnected */
                    w.ble_profile_index = state < 5 ? state : (state == 5 ? 1 : state - 6);
                    w.ble_profile_open = state == 5;
                    w.ble_profile_connected = state < 5;
                    in.bt_profile = w.ble_profile_index;
                    in.bt_profile_open = w.ble_profile_open;
                    in.bt_profile_connected = w.ble_profile_connected;
                    fork_reset_left();
                } else {
                    /* 0: healthy; 1: not bonded; 2: bonded but not connected */
                    w.periph_bonded = state != 1;
                    w.periph_connected = state == 0;
                    in.link_bonded = w.periph_bonded;
                    in.link_connected = w.periph_connected;
                    fork_reset_right();
                }

                for (unsigned t = 1; t <= TICKS; t++) {
                    struct fork_rgb want[3];
                    struct adv360_rgb got[3];

                    (left ? fork_tick_left : fork_tick_right)(&w, want);
                    in.tick = t;
                    adv360_status_render(&in, got);
                    cases++;

                    if (memcmp(want, got, sizeof(want)) != 0) {
                        report(left ? "left" : "right", &w, t, want, got);
                    }
                }
            }
}

static void check_battery(void) {
    for (unsigned soc = 0; soc <= 100; soc++) {
        struct fork_rgb want[3];
        struct adv360_rgb got[3];
        struct adv360_status_input in = {
            .side = ADV360_SIDE_LEFT,
            .battery_preview = true,
            .battery_soc = soc,
            .brightness_permille = FORK_PERMILLE,
        };

        fork_battery_left(soc, want);
        adv360_status_render(&in, got);
        cases++;
        if (memcmp(want, got, sizeof(want)) != 0) {
            struct fork_world w = {.soc = soc};
            report("battery", &w, 0, want, got);
        }
    }
}

int main(void) {
    check_side(true);
    check_side(false);
    check_battery();
    printf("fork differential: %lu comparisons, %lu mismatches\n", cases, failures);
    return failures ? 1 : 0;
}
