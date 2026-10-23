/*
 * The driver sleeps until adv360_blink_next_edge_ms() instead of waking every tick. That is only
 * correct if, for every blinking state and every moment in time:
 *   - the frame stays identical until the reported edge (no change is slept through), and
 *   - the frame changes at the edge (no needless wake) wherever the blink is visible.
 * Steady states (limit -1) must never change at all.
 */
#include <stdio.h>
#include <string.h>

#include <zmk_keyboard_adv360/status_render.h>

static unsigned long checks, failures;

static void fail(const char *what, int side, int a, int b, unsigned t) {
    if (failures++ < 10) {
        printf("FAIL %s side=%d a=%d b=%d t=%ums\n", what, side, a, b, t);
    }
}

static void frame(const struct adv360_status_input *base, uint32_t t_ms, struct adv360_rgb out[3]) {
    struct adv360_status_input in = *base;

    in.tick = t_ms / ADV360_BLINK_TICK_MS;
    adv360_status_render(&in, out);
}

static void check_state(const struct adv360_status_input *in, int side, int a, int b, bool visible) {
    const int limit = adv360_status_blink_limit(in);
    struct adv360_rgb first[3], now[3], next[3];

    frame(in, 0, first);

    if (limit < 0) {
        for (uint32_t t = 0; t < 3000; t += 7) {
            frame(in, t, now);
            checks++;
            if (memcmp(first, now, sizeof(first)) != 0) {
                fail("steady state changed", side, a, b, t);
            }
        }
        return;
    }

    for (uint32_t t = 0; t < 4000; t++) {
        const uint32_t edge = adv360_blink_next_edge_ms(limit, t);

        frame(in, t, now);
        checks++;

        if (edge < 1 || edge > (uint32_t)(limit + 1) * ADV360_BLINK_TICK_MS) {
            fail("edge out of range", side, a, b, t);
            continue;
        }
        if (edge > 1) { /* nothing may change before the edge */
            frame(in, t + edge - 1, next);
            if (memcmp(now, next, sizeof(now)) != 0) {
                fail("change before reported edge", side, a, b, t);
            }
        }
        if (visible) { /* and it must change at the edge */
            frame(in, t + edge, next);
            if (memcmp(now, next, sizeof(now)) == 0) {
                fail("no change at reported edge", side, a, b, t);
            }
        }
    }
}

int main(void) {
    /* Left half: every profile x {connected, unpaired, disconnected}. Profile 4 has no colour, so
     * its blink is not visible (the LED is dark in both phases). */
    for (int profile = 0; profile < 5; profile++) {
        for (int mode = 0; mode < 3; mode++) {
            struct adv360_status_input in = {
                .side = ADV360_SIDE_LEFT, .enabled = true, .layer = 1, .lock_color = 0xFFFFFF,
                .brightness_permille = 1000, .bt_profile = profile,
                .bt_profile_connected = mode == 0, .bt_profile_open = mode == 1,
                .link_bonded = true, .link_connected = true,
            };
            check_state(&in, 0, profile, mode, mode != 0 && profile < 4);
        }
    }

    /* Right half: bonded x connected. Any link problem blinks red. */
    for (int bonded = 0; bonded < 2; bonded++) {
        for (int connected = 0; connected < 2; connected++) {
            struct adv360_status_input in = {
                .side = ADV360_SIDE_RIGHT, .enabled = true, .layer = 1, .lock_color = 0xFFFFFF,
                .brightness_permille = 1000, .link_bonded = bonded, .link_connected = connected,
                .bt_profile_connected = true,
            };
            check_state(&in, 1, bonded, connected, !bonded || !connected);
        }
    }

    /* Exact boundaries: a fresh blink waits a full phase; one ms before the edge waits 1 ms. */
    for (int limit = ADV360_BLINK_FAST_LIMIT; limit <= ADV360_BLINK_SLOW_LIMIT; limit += 11) {
        const uint32_t phase = (limit + 1) * ADV360_BLINK_TICK_MS;

        checks += 3;
        if (adv360_blink_next_edge_ms(limit, 0) != phase) fail("elapsed 0", 2, limit, 0, 0);
        if (adv360_blink_next_edge_ms(limit, phase - 1) != 1) fail("one before edge", 2, limit, 0, 0);
        if (adv360_blink_next_edge_ms(limit, phase) != phase) fail("on the edge", 2, limit, 0, 0);
    }

    printf("blink schedule: %lu checks, %lu failures\n", checks, failures);
    return failures ? 1 : 0;
}
