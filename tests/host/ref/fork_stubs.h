/*
 * Host-side stand-ins for the Zephyr/ZMK symbols used by the fork's rgb_underglow.c snippets in
 * fork_kinesis.c. The tests drive these through the fork_* entry points at the bottom of that file.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <errno.h>
#include <stdlib.h>

#define IS_ENABLED(x) (x)
#define BIT(n) (1u << (n))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_WRN(...) ((void)0)

#ifndef ZMK_BLE_IS_CENTRAL
#error "compile with -DZMK_BLE_IS_CENTRAL=0 or =1"
#endif

#define CONFIG_ZMK_SPLIT_BLE 1

/*
 * Underglow settings. Config A is what the official firmware runs (ZMK's Kconfig.defaults plus
 * EFF_START=4); config B (-DUG_CONFIG_B) exercises the non-default min/max/step/start paths.
 */
#ifdef UG_CONFIG_B
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN 20
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX 80
#define CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP 7
#define CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP 13
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP 9
#define CONFIG_ZMK_RGB_UNDERGLOW_HUE_START 100
#define CONFIG_ZMK_RGB_UNDERGLOW_SAT_START 50
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_START 60
#define CONFIG_ZMK_RGB_UNDERGLOW_SPD_START 2
#define CONFIG_ZMK_RGB_UNDERGLOW_EFF_START 1
#define CONFIG_ZMK_RGB_UNDERGLOW_ON_START 0
#else
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN 0
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX 100
#define CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP 10
#define CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP 10
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP 10
#define CONFIG_ZMK_RGB_UNDERGLOW_HUE_START 0
#define CONFIG_ZMK_RGB_UNDERGLOW_SAT_START 100
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_START 100
#define CONFIG_ZMK_RGB_UNDERGLOW_SPD_START 3
#define CONFIG_ZMK_RGB_UNDERGLOW_EFF_START 4
#define CONFIG_ZMK_RGB_UNDERGLOW_ON_START 1
#endif
#define CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER 0

/* Zephyr timer / work-queue / LED strip stand-ins; the tests only care about the state they leave. */
struct device {
    int unused;
};
struct k_timer {
    int unused;
};
struct k_work {
    int unused;
};
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
static inline void k_timer_start(struct k_timer *t, int a, int b) {
    (void)t;
    (void)a;
    (void)b;
}
static inline void k_timer_stop(struct k_timer *t) { (void)t; }
static inline void *zmk_workqueue_lowprio_work_q(void) { return 0; }
static inline int k_work_submit_to_queue(void *q, struct k_work *w) {
    (void)q;
    (void)w;
    return 0;
}
static inline int led_strip_update_rgb(const struct device *d, void *px, int n) {
    (void)d;
    (void)px;
    (void)n;
    return 0;
}
/* Defaults from refil/zmk app/Kconfig; the user's board defconfigs do not override them. */
#define CONFIG_ZMK_RGB_UNDERGLOW_BRT_SCALE 1
#define CONFIG_ZMK_RGB_UNDERGLOW_MOD_COLOR 0xFFFFFF

#define STRIP_NUM_PIXELS 3

struct led_rgb {
    uint8_t r, g, b;
};

typedef uint8_t zmk_hid_indicators_t;

#define ZMK_LED_NUMLOCK_BIT BIT(0)
#define ZMK_LED_CAPSLOCK_BIT BIT(1)
#define ZMK_LED_SCROLLLOCK_BIT BIT(2)

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

/* app/include/zmk/rgb_underglow.h */
struct zmk_periph_led {
    uint8_t layer;
    zmk_hid_indicators_t indicators;
    uint8_t effect;
    bool on;
};

struct k_work_delayable {
    int unused;
};

/* State the fork code reads; set by the entry points. */
struct stub_world {
    uint8_t soc;
    uint8_t hid_indicators;
    uint8_t highest_layer;
    int ble_profile_index;
    bool ble_profile_open;
    bool ble_profile_connected;
    bool periph_bonded;
    bool periph_connected;
};
static struct stub_world world;

static inline uint8_t zmk_battery_state_of_charge(void) { return world.soc; }
static inline zmk_hid_indicators_t zmk_hid_indicators_get_current_profile(void) {
    return world.hid_indicators;
}
static inline uint8_t zmk_keymap_highest_layer_active(void) { return world.highest_layer; }
static inline int zmk_ble_active_profile_index(void) { return world.ble_profile_index; }
static inline bool zmk_ble_active_profile_is_open(void) { return world.ble_profile_open; }
static inline bool zmk_ble_active_profile_is_connected(void) { return world.ble_profile_connected; }
static inline bool zmk_split_bt_peripheral_is_bonded(void) { return world.periph_bonded; }
static inline bool zmk_split_bt_peripheral_is_connected(void) { return world.periph_connected; }
static inline int zmk_split_bt_update_led(struct zmk_periph_led *led) {
    (void)led;
    return 0;
}
