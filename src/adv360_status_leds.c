/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Device glue for the three Adv360 LEDs: runs the pure underglow engine (adv360_underglow_engine.c)
 * and renderer (adv360_status_render.c) from a timer and ZMK events, owns the LED strip and its
 * external power, and implements ZMK's zmk_rgb_underglow_*() API that the `&rgb_ug` behavior uses.
 *
 * The official Kinesis firmware polls every 50 ms while the LEDs are on. This driver only runs a
 * 50 ms timer while an effect animates or an indicator blinks, and otherwise redraws on events.
 */

#define DT_DRV_COMPAT kinesis_adv360_status_leds

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/ext_power.h>
#include <dt-bindings/zmk/hid_indicators.h>
#include <dt-bindings/zmk/rgb.h>
#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators_types.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#define ADV360_STATUS_HAS_CENTRAL_STATE 1
#else
#define ADV360_STATUS_HAS_CENTRAL_STATE 0
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#define ADV360_STATUS_HAS_LINK_STATE 1
#else
#define ADV360_STATUS_HAS_LINK_STATE 0
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#define ADV360_STATUS_CAN_SYNC 1
#else
#define ADV360_STATUS_CAN_SYNC 0
#endif

#include <zmk_keyboard_adv360/status_leds.h>
#include <zmk_keyboard_adv360/status_render.h>
#include <zmk_keyboard_adv360/underglow.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define STATUS_NODE DT_DRV_INST(0)
#define STATUS_STRIP_NODE DT_PHANDLE(STATUS_NODE, led_strip)
#define STATUS_LED_COUNT DT_PROP(STATUS_STRIP_NODE, chain_length)

BUILD_ASSERT(STATUS_LED_COUNT >= ADV360_STATUS_LED_COUNT,
             "Adv360 status LED strip must have at least three pixels");

/* Behavior parameter used to pick the layer; must match behavior_adv360_status_tap.c. */
#define ADV360_STP_SET_LAYER 2
#define ADV360_STP_SET_ON 3

#define ADV360_BLINK_TICK_MS 50
#define ADV360_SYNC_DELAY_MS 2500
#define ADV360_SYNC_DEBOUNCE_MS 500

static const struct device *const led_strip = DEVICE_DT_GET(STATUS_STRIP_NODE);

static const struct adv360_ug_config ug_cfg = {
    .hue_start = CONFIG_ZMK_ADV360_STATUS_LEDS_HUE_START,
    .sat_start = CONFIG_ZMK_ADV360_STATUS_LEDS_SAT_START,
    .brt_start = CONFIG_ZMK_ADV360_STATUS_LEDS_BRT_START,
    .spd_start = CONFIG_ZMK_ADV360_STATUS_LEDS_SPD_START,
    .eff_start = CONFIG_ZMK_ADV360_STATUS_LEDS_EFF_START,
    .on_start = IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_ON_START),
    .hue_step = CONFIG_ZMK_ADV360_STATUS_LEDS_HUE_STEP,
    .sat_step = CONFIG_ZMK_ADV360_STATUS_LEDS_SAT_STEP,
    .brt_step = CONFIG_ZMK_ADV360_STATUS_LEDS_BRT_STEP,
    .brt_min = CONFIG_ZMK_ADV360_STATUS_LEDS_BRT_MIN,
    .brt_max = CONFIG_ZMK_ADV360_STATUS_LEDS_BRT_MAX,
};

static struct adv360_ug ug;
static struct led_rgb pixels[STATUS_LED_COUNT];

static zmk_hid_indicators_t hid_indicators;
static uint8_t active_layer;

/* Left half: Bluetooth state of the active host profile. */
static uint8_t bt_profile;
static bool bt_profile_open;
static bool bt_profile_connected = true;

/* Right half: link to the left half. Assume healthy until the first refresh says otherwise. */
static bool link_bonded = true;
static bool link_connected = true;

/* 50 ms animation ticks while an indicator blinks; 0 whenever nothing blinks. */
static uint32_t blink_tick;

static struct k_work_delayable tick_work;
#if ADV360_STATUS_CAN_SYNC
static struct k_work_delayable sync_work;
#endif

/*
 * ext_power_enable()/disable() schedule a settings save every time they are called, so only call
 * them when the state really changes (the blink timer redraws every 50 ms).
 */
static int8_t ext_power_is_on = -1; /* -1 until the first call: the restored state is unknown */

static int set_ext_power(bool on) {
    const struct device *ext_power = device_get_binding("EXT_POWER");

    if (ext_power == NULL) {
        return 0;
    }

    return on ? ext_power_enable(ext_power) : ext_power_disable(ext_power);
}

static void flush_pixels(const struct adv360_rgb *out, bool power_on) {
    int err;

    if (power_on && ext_power_is_on != 1) {
        err = set_ext_power(true);
        if (err) {
            LOG_WRN("Failed to enable Adv360 status LED power: %d", err);
        } else {
            ext_power_is_on = 1;
        }
    }

    memset(pixels, 0, sizeof(pixels));
    for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
        pixels[i] = (struct led_rgb){.r = out[i].r, .g = out[i].g, .b = out[i].b};
    }

    err = led_strip_update_rgb(led_strip, pixels, STATUS_LED_COUNT);
    if (err) {
        LOG_ERR("Failed to update Adv360 status LEDs: %d", err);
        return;
    }

    if (!power_on && ext_power_is_on != 0) {
        err = set_ext_power(false);
        if (err) {
            LOG_WRN("Failed to disable Adv360 status LED power: %d", err);
        } else {
            ext_power_is_on = 0;
        }
    }
}

static void refresh_connection_state(void) {
#if ADV360_STATUS_HAS_CENTRAL_STATE && IS_ENABLED(CONFIG_BOARD_ADV360_LEFT)
    bt_profile = zmk_ble_active_profile_index();
    bt_profile_open = zmk_ble_active_profile_is_open();
    bt_profile_connected = zmk_ble_active_profile_is_connected();
#endif
#if ADV360_STATUS_HAS_LINK_STATE
    link_bonded = zmk_split_bt_peripheral_is_bonded();
    link_connected = zmk_split_bt_peripheral_is_connected();
#endif
}

/* Does the current Bluetooth / split-link state make an LED blink? */
static bool blink_needed(void) {
#if ADV360_STATUS_HAS_CENTRAL_STATE && IS_ENABLED(CONFIG_BOARD_ADV360_LEFT)
    return bt_profile_open || !bt_profile_connected;
#elif ADV360_STATUS_HAS_LINK_STATE
    return !link_bonded || !link_connected;
#else
    return false;
#endif
}

static void tick_handler(struct k_work *work) {
    struct adv360_rgb out[ADV360_STATUS_LED_COUNT] = {0};
    bool blinking = false;

    refresh_connection_state();

    if (ug.on) {
        blinking = ug.effect == ADV360_UG_EFFECT_KINESIS && blink_needed();
        blink_tick = blinking ? blink_tick + 1 : 0;

        const struct adv360_status_input ctx = {
#if IS_ENABLED(CONFIG_BOARD_ADV360_LEFT)
            .side = ADV360_SIDE_LEFT,
#elif IS_ENABLED(CONFIG_BOARD_ADV360_RIGHT)
            .side = ADV360_SIDE_RIGHT,
#else
            .side = ADV360_SIDE_OTHER,
#endif
            .layer = active_layer,
            .caps_lock = hid_indicators & HID_INDICATOR_CAPS_LOCK,
            .num_lock = hid_indicators & HID_INDICATOR_NUM_LOCK,
            .scroll_lock = hid_indicators & HID_INDICATOR_SCROLL_LOCK,
            .battery_soc = zmk_battery_state_of_charge(),
            .lock_color = CONFIG_ZMK_ADV360_STATUS_LEDS_LOCK_COLOR,
            .bt_profile = bt_profile,
            .bt_profile_open = bt_profile_open,
            .bt_profile_connected = bt_profile_connected,
            .link_bonded = link_bonded,
            .link_connected = link_connected,
            .tick = blink_tick,
            .brightness_permille = CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS_PERMILLE,
        };

        adv360_ug_tick(&ug, &ctx, out);
    } else {
        blink_tick = 0;
    }

    flush_pixels(out, ug.on);

    if (ug.on && (adv360_ug_effect_animates(ug.effect) || blinking)) {
        k_work_reschedule(&tick_work, K_MSEC(ADV360_BLINK_TICK_MS));
    }
}

static void schedule_update(void) { k_work_reschedule(&tick_work, K_NO_WAIT); }

#if ADV360_STATUS_CAN_SYNC
static void invoke_on_peripherals(const char *behavior, uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = behavior,
        .param1 = param1,
        .param2 = param2,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };

    for (int i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
        zmk_split_central_invoke_behavior(i, &binding, event, true);
    }
}

static void sync_layer_to_peripherals(void) {
    invoke_on_peripherals("stp", ADV360_STP_SET_LAYER, active_layer);
}

/*
 * Commands reach both halves through the global `&rgb_ug` / `&stp` behaviors, so the halves only
 * drift apart if one of them restarts or reconnects. The central has no event for that, so it
 * re-sends the shared LED state after boot and (debounced) after layer changes.
 *
 * Selecting an effect or switching on restarts the animation on the receiving half, so after boot
 * only effects that do not animate are re-sent: an animated effect would visibly restart on the
 * right half at every layer change.
 */
static bool full_sync_done;

static void sync_work_handler(struct k_work *work) {
    const bool resend_state = !full_sync_done || !adv360_ug_effect_animates(ug.effect);

    full_sync_done = true;
    sync_layer_to_peripherals();

    if (resend_state) {
        invoke_on_peripherals("rgb_ug", RGB_EFS_CMD, ug.effect);
        invoke_on_peripherals("rgb_ug", RGB_COLOR_HSB_CMD,
                              RGB_COLOR_HSB_VAL(ug.color.h, ug.color.s, ug.color.b));
        invoke_on_peripherals("rgb_ug", ug.on ? RGB_ON_CMD : RGB_OFF_CMD, 0);
    }
}
#else
static void sync_layer_to_peripherals(void) {}
#endif

/* ---- Public API: ZMK's zmk_rgb_underglow_*() (see zmk/rgb_underglow.h), on the Adv360 engine ---- */

int zmk_rgb_underglow_get_state(bool *on_off) {
    *on_off = ug.on;
    return 0;
}

int zmk_rgb_underglow_on(void) {
    adv360_ug_on(&ug);
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_off(void) {
    adv360_ug_off(&ug);
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_toggle(void) { return ug.on ? zmk_rgb_underglow_off() : zmk_rgb_underglow_on(); }

int zmk_rgb_underglow_calc_effect(int direction) { return adv360_ug_calc_effect(&ug, direction); }

int zmk_rgb_underglow_select_effect(int effect) {
    int err = adv360_ug_select_effect(&ug, effect);

    if (err == 0) {
        schedule_update();
    }
    return err;
}

int zmk_rgb_underglow_cycle_effect(int direction) {
    return zmk_rgb_underglow_select_effect(zmk_rgb_underglow_calc_effect(direction));
}

int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    int err = adv360_ug_set_hsb(&ug, (struct adv360_ug_hsb){.h = color.h, .s = color.s, .b = color.b});

    if (err == 0) {
        schedule_update();
    }
    return err;
}

#define ADV360_HSB_TO_ZMK(c) ((struct zmk_led_hsb){.h = (c).h, .s = (c).s, .b = (c).b})

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction) {
    return ADV360_HSB_TO_ZMK(adv360_ug_calc_hue(&ug, direction));
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction) {
    return ADV360_HSB_TO_ZMK(adv360_ug_calc_sat(&ug, direction));
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction) {
    return ADV360_HSB_TO_ZMK(adv360_ug_calc_brt(&ug, direction));
}

int zmk_rgb_underglow_change_hue(int direction) {
    adv360_ug_change_hue(&ug, direction);
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_change_sat(int direction) {
    adv360_ug_change_sat(&ug, direction);
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_change_brt(int direction) {
    adv360_ug_change_brt(&ug, direction);
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_change_spd(int direction) {
    adv360_ug_change_spd(&ug, direction);
    return 0;
}

/* ---- Hooks for the `stp` behavior ---- */

int adv360_status_leds_set_layer(uint8_t layer) {
    active_layer = layer;
    schedule_update();
    return 0;
}

int adv360_status_leds_set_on(bool on) { return on ? zmk_rgb_underglow_on() : zmk_rgb_underglow_off(); }

/* ---- Auto-off ---- */

#if IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_IDLE) ||                                     \
    IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_USB)
static int auto_state(bool target_wake_state) {
    static bool is_awake = true;
    static bool state_before_sleeping;

    /* A wake event while awake, or a sleep event while asleep, is a no-op. */
    if (target_wake_state == is_awake) {
        return 0;
    }
    is_awake = target_wake_state;

    if (is_awake) {
        return state_before_sleeping ? zmk_rgb_underglow_on() : zmk_rgb_underglow_off();
    }

    state_before_sleeping = ug.on;
    return zmk_rgb_underglow_off();
}
#endif

static int adv360_status_leds_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *hid_ev = as_zmk_hid_indicators_changed(eh);

    if (hid_ev != NULL) {
        hid_indicators = hid_ev->indicators;
        schedule_update();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_battery_state_changed(eh) != NULL) {
        if (ug.on && ug.effect == ADV360_UG_EFFECT_BATTERY) {
            schedule_update();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_IDLE)
    if (as_zmk_activity_state_changed(eh) != NULL) {
        auto_state(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_USB)
    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        auto_state(zmk_usb_is_powered());
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

#if ADV360_STATUS_HAS_CENTRAL_STATE
    if (as_zmk_ble_active_profile_changed(eh) != NULL) {
        schedule_update();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh) != NULL) {
        active_layer = zmk_keymap_highest_layer_active();
        schedule_update();
        sync_layer_to_peripherals();
#if ADV360_STATUS_CAN_SYNC
        k_work_reschedule(&sync_work, K_MSEC(ADV360_SYNC_DEBOUNCE_MS));
#endif
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

#if ADV360_STATUS_HAS_LINK_STATE
    if (as_zmk_split_peripheral_status_changed(eh) != NULL) {
        schedule_update();
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(adv360_status_leds, adv360_status_leds_listener);
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_hid_indicators_changed);
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_IDLE)
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_activity_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_AUTO_OFF_USB)
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_usb_conn_state_changed);
#endif
#if ADV360_STATUS_HAS_CENTRAL_STATE
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_ble_active_profile_changed);
#endif
#if ADV360_STATUS_HAS_LINK_STATE
ZMK_SUBSCRIPTION(adv360_status_leds, zmk_split_peripheral_status_changed);
#endif

static int adv360_status_leds_init(void) {
    if (!device_is_ready(led_strip)) {
        LOG_ERR("Adv360 status LED strip device is not ready");
        return -ENODEV;
    }

    adv360_ug_init(&ug, &ug_cfg);
    k_work_init_delayable(&tick_work, tick_handler);
#if ADV360_STATUS_CAN_SYNC
    k_work_init_delayable(&sync_work, sync_work_handler);
#endif

#if ADV360_STATUS_HAS_CENTRAL_STATE
    hid_indicators = zmk_hid_indicators_get_current_profile();
    active_layer = zmk_keymap_highest_layer_active();
#endif

    schedule_update();
    sync_layer_to_peripherals();
#if ADV360_STATUS_CAN_SYNC
    k_work_reschedule(&sync_work, K_MSEC(ADV360_SYNC_DELAY_MS));
#endif
    return 0;
}

SYS_INIT(adv360_status_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
