/* Minimal host-side stand-ins for the Zephyr/ZMK symbols the reference code uses. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define IS_ENABLED(x) (x)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS
#define CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS 20
#endif

/* Which half is being built; the reference code selects on these at compile time. */
#ifdef TEST_SIDE_LEFT
#define CONFIG_BOARD_ADV360_LEFT 1
#define CONFIG_BOARD_ADV360_RIGHT 0
#elif defined(TEST_SIDE_RIGHT)
#define CONFIG_BOARD_ADV360_LEFT 0
#define CONFIG_BOARD_ADV360_RIGHT 1
#else
#define CONFIG_BOARD_ADV360_LEFT 0
#define CONFIG_BOARD_ADV360_RIGHT 0
#endif

struct led_rgb {
    uint8_t r, g, b;
};

typedef uint8_t zmk_hid_indicators_t;

/* dt-bindings/zmk/hid_indicators.h */
#define HID_INDICATOR_NUM_LOCK (1 << 0)
#define HID_INDICATOR_CAPS_LOCK (1 << 1)
#define HID_INDICATOR_SCROLL_LOCK (1 << 2)

static uint8_t zmk_battery_soc_stub;
static inline uint8_t zmk_battery_state_of_charge(void) { return zmk_battery_soc_stub; }
