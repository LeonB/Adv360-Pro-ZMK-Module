/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Key chatter probe (debug aid, CONFIG_ZMK_ADV360_CHATTER_PROBE, off by default).
 *
 * Listens to the key position events of the left (central) half and records, per key, how soon a
 * press follows the previous release and how short a press is. Timestamps come from the cycle
 * counter (about 30 us), but the events themselves arrive at the matrix scan rate (1 ms while a key
 * is down), so the result is good to about 1 ms. Build with CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS=0 and
 * CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS=0 to see the raw contact bounce instead of the debounced keys.
 *
 * `&chatter` types the report as text (digits, letters, spaces) over the active HID endpoint and
 * starts a new measurement. Only keys with chatter or very short presses are listed.
 */

#define DT_DRV_COMPAT kinesis_behavior_adv360_chatter

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_ADV360_CHATTER_PROBE)

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>

#define POSITIONS 76
#define BUCKETS 6
#define SHORT_PULSE_US 10000

/* A press this soon after the previous release counts as chatter, bucketed by the gap. */
static const uint32_t bucket_limit_us[BUCKETS] = {2000, 4000, 8000, 16000, 32000, 64000};

struct key_stat {
    uint32_t presses;
    uint32_t chatter;      /* presses within the last bucket limit of the previous release */
    uint32_t short_pulses; /* presses released within SHORT_PULSE_US */
    uint32_t min_gap_us;   /* shortest release -> press gap seen */
    uint16_t bucket[BUCKETS];
};

struct key_edge {
    uint32_t last_press_us;
    uint32_t last_release_us;
    bool down;
    bool had_release;
};

static struct key_stat stats[POSITIONS];
static struct key_edge edges[POSITIONS];
static uint32_t total_presses;
static struct k_spinlock lock;

/* Left half (central) positions, see config/keypos_adv360.h. Right half events arrive over BLE. */
static bool left_half(uint32_t p) {
    return p <= 6 || (p >= 14 && p <= 20) || (p >= 28 && p <= 36) || (p >= 46 && p <= 52) ||
           (p >= 60 && p <= 67);
}

static void clear_stats(void) {
    memset(stats, 0, sizeof(stats));
    total_presses = 0;
}

static int chatter_probe_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || ev->position >= POSITIONS || !left_half(ev->position)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint32_t now = k_cyc_to_us_floor32(k_cycle_get_32());
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct key_stat *s = &stats[ev->position];
    struct key_edge *e = &edges[ev->position];

    if (ev->state) {
        s->presses++;
        total_presses++;

        if (e->had_release) {
            const uint32_t gap = now - e->last_release_us;

            if (gap < bucket_limit_us[BUCKETS - 1]) {
                s->chatter++;
                if (s->min_gap_us == 0 || gap < s->min_gap_us) {
                    s->min_gap_us = gap;
                }
                for (int i = 0; i < BUCKETS; i++) {
                    if (gap < bucket_limit_us[i]) {
                        s->bucket[i]++;
                        break;
                    }
                }
            }
        }

        e->last_press_us = now;
        e->down = true;
    } else {
        if (e->down && (now - e->last_press_us) < SHORT_PULSE_US) {
            s->short_pulses++;
        }

        e->last_release_us = now;
        e->had_release = true;
        e->down = false;
    }

    k_spin_unlock(&lock, key);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(adv360_chatter, chatter_probe_listener);
ZMK_SUBSCRIPTION(adv360_chatter, zmk_position_state_changed);

/* The report is typed one key every TYPE_STEP_MS (press, then release). */
#define TYPE_STEP_MS 10

static char report[1536];
static size_t report_len;
static size_t report_pos;
static bool key_is_down;
static bool typing;
static uint32_t current_code;
static struct k_work_delayable type_work;

static uint32_t char_to_code(char c) {
    if (c >= 'a' && c <= 'z') {
        return A + (c - 'a');
    }
    if (c >= '1' && c <= '9') {
        return N1 + (c - '1');
    }
    switch (c) {
    case '0':
        return N0;
    case '.':
        return DOT;
    case '\n':
        return ENTER;
    default:
        return SPACE;
    }
}

static void build_report(void) {
    size_t n = 0;

    k_spinlock_key_t key = k_spin_lock(&lock);

    n += snprintf(report + n, sizeof(report) - n, "chatter n%u\n", (unsigned)total_presses);

    for (int p = 0; p < POSITIONS; p++) {
        const struct key_stat *s = &stats[p];

        if (!left_half(p) || (s->chatter == 0 && s->short_pulses == 0)) {
            continue;
        }
        if (n + 80 >= sizeof(report)) {
            break;
        }

        const uint32_t m = s->min_gap_us / 100; /* tenths of a ms */

        n += snprintf(report + n, sizeof(report) - n, "p%d n%u c%u m%u.%u s%u b%u.%u.%u.%u.%u.%u\n",
                      p, (unsigned)s->presses, (unsigned)s->chatter, (unsigned)(m / 10),
                      (unsigned)(m % 10), (unsigned)s->short_pulses, s->bucket[0], s->bucket[1],
                      s->bucket[2], s->bucket[3], s->bucket[4], s->bucket[5]);
    }

    clear_stats();
    k_spin_unlock(&lock, key);

    report_len = n;
    report_pos = 0;
}

static void type_next(struct k_work *work) {
    if (report_pos >= report_len) {
        typing = false;
        return;
    }

    if (!key_is_down) {
        current_code = char_to_code(report[report_pos]);
        raise_zmk_keycode_state_changed_from_encoded(current_code, true, k_uptime_get());
        key_is_down = true;
    } else {
        raise_zmk_keycode_state_changed_from_encoded(current_code, false, k_uptime_get());
        key_is_down = false;
        report_pos++;
    }

    k_work_reschedule(&type_work, K_MSEC(TYPE_STEP_MS));
}

static int chatter_init(const struct device *dev) {
    k_work_init_delayable(&type_work, type_next);
    return 0;
}

static int chatter_report(void) {
    if (typing) {
        return 0;
    }

    build_report();
    typing = true;
    key_is_down = false;
    k_work_reschedule(&type_work, K_MSEC(TYPE_STEP_MS));
    return 0;
}

#else /* !CONFIG_ZMK_ADV360_CHATTER_PROBE */

static int chatter_init(const struct device *dev) { return 0; }
static int chatter_report(void) { return 0; }

#endif /* CONFIG_ZMK_ADV360_CHATTER_PROBE */

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    return chatter_report();
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_adv360_chatter_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

BEHAVIOR_DT_INST_DEFINE(0, chatter_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_adv360_chatter_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
