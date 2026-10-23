/*
 * REFERENCE ORACLE - do not edit.
 *
 * Render logic of src/adv360_status_leds.c exactly as it was in ProfFan/Adv360-Pro-ZMK-Module
 * at 41d16a2 ("Clean up README"), i.e. before the parity work. Only the Zephyr/ZMK types are
 * stubbed (tests/host/ref/zephyr_stubs.h). Used to prove the refactored renderer is
 * behaviour-preserving and to list every deliberate behaviour change.
 */
#include "zephyr_stubs.h"

static struct led_rgb pixels[3];
static bool status_enabled;
static bool battery_preview;
static zmk_hid_indicators_t hid_indicators;
static uint8_t active_layer;
#define STATUS_LED_COUNT 3

static struct led_rgb scale_rgb(uint32_t hex) {
    const uint8_t brightness = CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS;

    return (struct led_rgb){
        .r = (uint8_t)((((hex >> 16) & 0xFF) * brightness) / 100),
        .g = (uint8_t)((((hex >> 8) & 0xFF) * brightness) / 100),
        .b = (uint8_t)((((hex >> 0) & 0xFF) * brightness) / 100),
    };
}

static const uint32_t layer_colors[] = {
    0x000000, 0xFFFFFF, 0x0000FF, 0x00FF00, 0xFF0000, 0xFF00FF, 0x00FFFF, 0xFFFF00,
};

static void layer_to_color_indices(uint8_t layer, uint8_t *left, uint8_t *right) {
    if (layer < ARRAY_SIZE(layer_colors)) {
        *left = layer;
        *right = layer;
        return;
    }

    *left = 1 + ((layer - ARRAY_SIZE(layer_colors)) / 6);
    *right = 1 + ((layer - ARRAY_SIZE(layer_colors)) % 6);

    if (*left <= *right) {
        *right += 1;
    }

    *left %= ARRAY_SIZE(layer_colors);
    *right %= ARRAY_SIZE(layer_colors);
}

static struct led_rgb battery_color(void) {
    const uint8_t soc = zmk_battery_state_of_charge();

    if (soc >= 80) {
        return scale_rgb(0x00FF00);
    } else if (soc >= 50) {
        return scale_rgb(0xFFFF00);
    } else if (soc >= 20) {
        return scale_rgb(0xFF8C00);
    } else {
        return scale_rgb(0xFF0000);
    }
}

static void clear_pixels(void) { memset(pixels, 0, sizeof(pixels)); }
static void render_status(void) {
    uint8_t left_color;
    uint8_t right_color;

    clear_pixels();

    if (!status_enabled && !battery_preview) {
        return;
    }

    if (battery_preview) {
        struct led_rgb color = battery_color();
        for (int i = 0; i < ARRAY_SIZE(pixels); i++) {
            pixels[i] = color;
        }
        return;
    }

    layer_to_color_indices(active_layer, &left_color, &right_color);

#if IS_ENABLED(CONFIG_BOARD_ADV360_LEFT)
    pixels[0] = (hid_indicators & HID_INDICATOR_CAPS_LOCK) ? scale_rgb(0xFFFFFF)
                                                           : scale_rgb(0x000000);
    pixels[1] = scale_rgb(0x000000);
    pixels[2] = scale_rgb(layer_colors[left_color]);
#elif IS_ENABLED(CONFIG_BOARD_ADV360_RIGHT)
    pixels[0] = scale_rgb(layer_colors[right_color]);
    pixels[1] = (hid_indicators & HID_INDICATOR_SCROLL_LOCK) ? scale_rgb(0xFFFFFF)
                                                             : scale_rgb(0x000000);
    pixels[2] = (hid_indicators & HID_INDICATOR_NUM_LOCK) ? scale_rgb(0xFFFFFF)
                                                          : scale_rgb(0x000000);
#else
    pixels[0] = scale_rgb(layer_colors[left_color]);
#endif
}

/* Test entry point: same inputs the driver keeps in its statics. */
void BASELINE_ENTRY(bool enabled, bool preview, uint8_t layer, zmk_hid_indicators_t indicators,
                    uint8_t soc, struct led_rgb out[3]) {
    status_enabled = enabled;
    battery_preview = preview;
    active_layer = layer;
    hid_indicators = indicators;
    zmk_battery_soc_stub = soc;
    render_status();
    for (int i = 0; i < 3; i++) {
        out[i] = pixels[i];
    }
}
