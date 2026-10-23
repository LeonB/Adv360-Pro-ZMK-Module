/*
 * Host-side stand-ins for the Zephyr/ZMK symbols used by the fork's rgb_underglow.c snippets in
 * fork_kinesis.c. The tests drive these through the fork_* entry points at the bottom of that file.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define IS_ENABLED(x) (x)
#define BIT(n) (1u << (n))
#define LOG_ERR(...) ((void)0)

#ifndef ZMK_BLE_IS_CENTRAL
#error "compile with -DZMK_BLE_IS_CENTRAL=0 or =1"
#endif

#define CONFIG_ZMK_SPLIT_BLE 1
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
