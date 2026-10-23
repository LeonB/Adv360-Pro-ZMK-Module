/*
 * REFERENCE ORACLE - do not edit the extracted blocks.
 *
 * Indicator, battery and layer-colour logic of the Kinesis ZMK fork, extracted verbatim from
 *   refil/zmk  app/src/rgb_underglow.c  @ d93499bb877f2711989e6cbbe1feed9e9a792ca3
 *   (branch adv360-z3.5-2, the revision pinned by the official Adv360-Pro-ZMK west.yml)
 * Only the surrounding ZMK/Zephyr APIs are stubbed (fork_stubs.h). Used to check that the
 * module's pure renderer reproduces the official firmware's LED behaviour pixel for pixel.
 */
#include "fork_stubs.h"

static struct led_rgb pixels[STRIP_NUM_PIXELS];

struct {
    uint16_t animation_step;
} state;

static struct zmk_periph_led led_data;
static bool last_ble_state[2];
#if ZMK_BLE_IS_CENTRAL
static struct zmk_periph_led old_led_data;
#endif

/* ---- begin verbatim: LED_RGB scaling (rgb_underglow.c:159-166) ---- */
#define LED_RGB_SCALING_MULTIPLE (((float)CONFIG_ZMK_RGB_UNDERGLOW_BRT_SCALE) / 250.)

#define LED_RGB(hex)                                                                               \
    ((struct led_rgb){                                                                             \
        r : LED_RGB_SCALING_MULTIPLE * (((hex) & 0xFF0000) >> 16),                                 \
        g : LED_RGB_SCALING_MULTIPLE * (((hex) & 0x00FF00) >> 8),                                  \
        b : LED_RGB_SCALING_MULTIPLE * (((hex) & 0x0000FF) >> 0)                                   \
    })
/* ---- end verbatim ---- */

/* ---- begin verbatim: central send, colours, layer map, blink, kinesis effect (rgb_underglow.c:225-343) ---- */
#if ZMK_BLE_IS_CENTRAL
static struct k_work_delayable led_update_work;

static void zmk_rgb_underglow_central_send() {
    int err = zmk_split_bt_update_led(&led_data);
    if (err) {
        LOG_ERR("send failed (err %d)", err);
    }
}

#define NUM_BT_COLORS 4

static const struct led_rgb BT_COLORS[NUM_BT_COLORS] = {LED_RGB(0xFFFFFF), LED_RGB(0x0000FF),
                                                        LED_RGB(0xFF0000), LED_RGB(0x00FF00)};
#endif

static const struct led_rgb LAYER_COLORS[8] = {
    LED_RGB(0x000000), LED_RGB(0xFFFFFF), LED_RGB(0x0000FF), LED_RGB(0x00FF00),
    LED_RGB(0xFF0000), LED_RGB(0xFF00FF), LED_RGB(0x00FFFF), LED_RGB(0xFFFF00)};

// Formulas chosen so that for the first 8 layers both left and right modules show the same color,
// then as the layer number increases the right module color cycles through until "wrapping around",
// at which point the left module colour is advanced by one as well. We skip over the off/black
// state while we do this. (The right module also skips over the current left module color each
// loop, since those combinations correspond to the first 8 layers.)
static void zmk_led_layer_to_colors(uint8_t layer, uint8_t *left, uint8_t *right) {
    if (layer < 8) {
        *left = layer;
        *right = layer;
        return;
    }

    *left = 1 + ((layer - 8) / 6);
    *right = 1 + ((layer - 8) % 6);

    if (*left <= *right)
        *right += 1;
}

static bool zmk_kinesis_blink_step(uint8_t idx, uint8_t limit) {
    state.animation_step++;
    if (state.animation_step > limit) {
        last_ble_state[idx] = !last_ble_state[idx];
        state.animation_step = 0;
    }

    return !last_ble_state[idx];
}

static struct led_rgb zmk_get_indicator_color(zmk_hid_indicators_t bit) {
    return led_data.indicators & bit ? LED_RGB(CONFIG_ZMK_RGB_UNDERGLOW_MOD_COLOR)
                                     : LED_RGB(0x000000);
}

static void zmk_rgb_underglow_effect_kinesis() {
#if ZMK_BLE_IS_CENTRAL
    // update state and propagate to peripheral if necessary
    old_led_data.layer = led_data.layer;
    old_led_data.indicators = led_data.indicators;
    led_data.indicators = zmk_hid_indicators_get_current_profile();
    led_data.layer = zmk_keymap_highest_layer_active();

    if (old_led_data.layer != led_data.layer || old_led_data.indicators != led_data.indicators) {
        zmk_rgb_underglow_central_send();
    }
#endif

    bool bt_blinking = false;

    uint8_t layer_color_left, layer_color_right;
    zmk_led_layer_to_colors(led_data.layer, &layer_color_left, &layer_color_right);

#if ZMK_BLE_IS_CENTRAL
    // leds for central (left) side

    // set first led to caps lock state
    pixels[0] = zmk_get_indicator_color(ZMK_LED_CAPSLOCK_BIT);

    // set second led to bluetooth state, blinking quickly if bluetooth not paired,
    // and slowly if not connected
    int bt_idx = zmk_ble_active_profile_index();
    if (zmk_ble_active_profile_is_open()) {
        bt_blinking = zmk_kinesis_blink_step(0, 2);
    } else if (!zmk_ble_active_profile_is_connected()) {
        bt_blinking = zmk_kinesis_blink_step(1, 13);
    }
    pixels[1] = (bt_idx < NUM_BT_COLORS && !bt_blinking) ? BT_COLORS[bt_idx] : LED_RGB(0x000000);

    // set third led to layer state
    pixels[2] = LAYER_COLORS[layer_color_left];
#else
    // leds for peripheral (right) side

    // set first and second leds to num lock and scroll lock state, respectively
    pixels[2] = zmk_get_indicator_color(ZMK_LED_NUMLOCK_BIT);
    pixels[1] = zmk_get_indicator_color(ZMK_LED_SCROLLLOCK_BIT);

    // set third led to layer state
    pixels[0] = LAYER_COLORS[layer_color_right];

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    bool bt_alert = false;
    if (!zmk_split_bt_peripheral_is_bonded()) {
        bt_alert = true;
        bt_blinking = zmk_kinesis_blink_step(0, 2);
    } else if (!zmk_split_bt_peripheral_is_connected()) {
        bt_alert = true;
        bt_blinking = zmk_kinesis_blink_step(1, 13);
    }

    if (bt_alert) {
        // override all leds to blinking red due to bluetooth problem
        struct led_rgb color = bt_blinking ? LED_RGB(0x000000) : LED_RGB(0xFF0000);
        for (int i = 0; i < STRIP_NUM_PIXELS; i++)
            pixels[i] = color;
    }
#endif
#endif
}
/* ---- end verbatim ---- */

/* ---- begin verbatim: battery effect (rgb_underglow.c:398-416) ---- */
#define NUM_BATTERY_LEVELS 3

static const uint8_t BATTERY_LEVELS[NUM_BATTERY_LEVELS] = {80, 50, 20};

static const struct led_rgb BATTERY_COLORS[NUM_BATTERY_LEVELS + 1] = {
    LED_RGB(0x00FF00), LED_RGB(0xFFFF00), LED_RGB(0xFF8C00), LED_RGB(0xFF0000)};

static void zmk_rgb_underglow_effect_battery() {
    uint8_t soc = zmk_battery_state_of_charge();

    int color = 0;
    for (; color < NUM_BATTERY_LEVELS && soc < BATTERY_LEVELS[color]; color++)
        ;

    struct led_rgb rgb = BATTERY_COLORS[color];
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = rgb;
    }
}
/* ---- end verbatim ---- */

/* ------------------------------------------------------------------------------------------ */
/* Test entry points                                                                          */

void FORK_RESET(void) {
    memset(&state, 0, sizeof(state));
    memset(&led_data, 0, sizeof(led_data));
    memset(last_ble_state, 0, sizeof(last_ble_state));
#if ZMK_BLE_IS_CENTRAL
    memset(&old_led_data, 0, sizeof(old_led_data));
#endif
    memset(pixels, 0, sizeof(pixels));
    memset(&world, 0, sizeof(world));
}

/* One 50 ms underglow tick of the "kinesis" effect (the effect the official firmware starts in). */
void FORK_KINESIS_TICK(const struct stub_world *w, struct led_rgb out[3]) {
    world = *w;
#if !ZMK_BLE_IS_CENTRAL
    /* The peripheral gets layer + indicators pushed from the central instead of reading them. */
    led_data.layer = w->highest_layer;
    led_data.indicators = w->hid_indicators;
#endif
    zmk_rgb_underglow_effect_kinesis();
    memcpy(out, pixels, sizeof(pixels));
}

void FORK_BATTERY(uint8_t soc, struct led_rgb out[3]) {
    world.soc = soc;
    zmk_rgb_underglow_effect_battery();
    memcpy(out, pixels, sizeof(pixels));
}
