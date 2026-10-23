/*
 * Characterization test: the pure renderer must still produce exactly the same pixels as the
 * original driver code (ref/proffan_baseline.c) for every combination of inputs, except for the
 * deliberate parity changes listed below (see PARITY.md):
 *
 *   - left LED 2 (Bluetooth profile) was always off in the original; it is now ignored here and
 *     covered by test_fork_differential instead.
 *
 * Everything else, including the right half with a healthy link and the default white lock
 * colour, must be unchanged.
 */
#include <stdio.h>
#include <string.h>

#include <zmk_keyboard_adv360/status_render.h>

struct ref_rgb {
    uint8_t r, g, b;
};

void baseline_left(bool, bool, uint8_t, uint8_t, uint8_t, struct ref_rgb[3]);
void baseline_right(bool, bool, uint8_t, uint8_t, uint8_t, struct ref_rgb[3]);
void baseline_other(bool, bool, uint8_t, uint8_t, uint8_t, struct ref_rgb[3]);

int main(void) {
    static const uint8_t socs[] = {0, 1, 19, 20, 21, 49, 50, 51, 79, 80, 81, 100};
    static const struct {
        enum adv360_side side;
        void (*ref)(bool, bool, uint8_t, uint8_t, uint8_t, struct ref_rgb[3]);
        const char *name;
    } sides[] = {
        {ADV360_SIDE_LEFT, baseline_left, "left"},
        {ADV360_SIDE_RIGHT, baseline_right, "right"},
        {ADV360_SIDE_OTHER, baseline_other, "other"},
    };

    unsigned long cases = 0, failures = 0;

    for (unsigned s = 0; s < 3; s++)
        for (int enabled = 0; enabled < 2; enabled++)
            for (int preview = 0; preview < 2; preview++)
                for (unsigned layer = 0; layer < 64; layer++)
                    for (unsigned ind = 0; ind < 8; ind++)
                        for (unsigned k = 0; k < sizeof(socs); k++) {
                            struct ref_rgb want[3];
                            struct adv360_rgb got[3];
                            struct adv360_status_input in = {
                                .side = sides[s].side,
                                .enabled = enabled,
                                .battery_preview = preview,
                                .layer = layer,
                                .num_lock = ind & 1,
                                .caps_lock = ind & 2,
                                .scroll_lock = ind & 4,
                                .battery_soc = socs[k],
                                .brightness_permille = 200, /* 20% */
                                .lock_color = 0xFFFFFF,
                                .bt_profile = 0,
                                .bt_profile_connected = true,
                                .link_bonded = true,
                                .link_connected = true,
                                .tick = 1,
                            };

                            sides[s].ref(enabled, preview, layer, ind, socs[k], want);
                            adv360_status_render(&in, got);
                            cases++;

                            if (sides[s].side == ADV360_SIDE_LEFT) {
                                got[1] = (struct adv360_rgb){want[1].r, want[1].g, want[1].b};
                            }

                            if (memcmp(want, got, sizeof(want)) != 0) {
                                if (failures++ < 10) {
                                    printf("MISMATCH side=%s en=%d prev=%d layer=%u ind=%u soc=%u\n",
                                           sides[s].name, enabled, preview, layer, ind, socs[k]);
                                }
                            }
                        }

    printf("baseline equivalence: %lu cases, %lu mismatches\n", cases, failures);
    return failures ? 1 : 0;
}
