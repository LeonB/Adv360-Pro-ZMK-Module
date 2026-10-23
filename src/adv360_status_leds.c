/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT kinesis_adv360_status_leds

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/ext_power.h>
#include <dt-bindings/zmk/hid_indicators.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators_types.h>

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
#define ADV360_STATUS_CAN_SYNC_LAYER 1
#else
#define ADV360_STATUS_CAN_SYNC_LAYER 0
#endif

#include <zmk_keyboard_adv360/status_leds.h>
#include <zmk_keyboard_adv360/status_render.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define STATUS_NODE DT_DRV_INST(0)
#define STATUS_STRIP_NODE DT_PHANDLE(STATUS_NODE, led_strip)
#define STATUS_LED_COUNT DT_PROP(STATUS_STRIP_NODE, chain_length)

BUILD_ASSERT(STATUS_LED_COUNT >= 3, "Adv360 status LED strip must have at least three pixels");

static const struct device *const led_strip = DEVICE_DT_GET(STATUS_STRIP_NODE);
static struct led_rgb pixels[STATUS_LED_COUNT];

static bool status_enabled = IS_ENABLED(CONFIG_ZMK_ADV360_STATUS_LEDS_ON_START);
static bool battery_preview;
static zmk_hid_indicators_t hid_indicators;
static uint8_t active_layer;

/* Left half: Bluetooth state of the active host profile. */
static uint8_t bt_profile;
static bool bt_profile_open;
static bool bt_profile_connected = true;

/* Right half: link to the left half. Assume healthy until the first refresh says otherwise. */
static bool link_bonded = true;
static bool link_connected = true;

/* 50 ms animation ticks while an LED blinks; 0 whenever nothing blinks. */
static uint32_t blink_tick;
#define ADV360_BLINK_TICK_MS 50

static struct k_work_delayable update_work;

#define ADV360_STP_SET_LAYER 2

static void clear_pixels(void) { memset(pixels, 0, sizeof(pixels)); }

static int set_ext_power(bool on) {
    const struct device *ext_power = device_get_binding("EXT_POWER");

    if (ext_power == NULL) {
        return 0;
    }

    return on ? ext_power_enable(ext_power) : ext_power_disable(ext_power);
}

/*
 * ext_power_enable()/disable() schedule a settings save every time they are called, so only call
 * them when the state really changes (the blink timer redraws every 50 ms).
 */
static int8_t ext_power_is_on = -1; /* -1 until the first call: the restored state is unknown */

static int flush_pixels(bool power_on) {
    struct led_rgb out[STATUS_LED_COUNT];
    int err;

    if (power_on && ext_power_is_on != 1) {
        err = set_ext_power(true);
        if (err) {
            LOG_WRN("Failed to enable Adv360 status LED power: %d", err);
        } else {
            ext_power_is_on = 1;
        }
    }

    memcpy(out, pixels, sizeof(out));
    err = led_strip_update_rgb(led_strip, out, ARRAY_SIZE(out));
    if (err) {
        LOG_ERR("Failed to update Adv360 status LEDs: %d", err);
        return err;
    }

    if (!power_on && ext_power_is_on != 0) {
        err = set_ext_power(false);
        if (err) {
            LOG_WRN("Failed to disable Adv360 status LED power: %d", err);
        } else {
            ext_power_is_on = 0;
        }
    }

    return 0;
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

static void render_status(void) {
    const struct adv360_status_input in = {
#if IS_ENABLED(CONFIG_BOARD_ADV360_LEFT)
        .side = ADV360_SIDE_LEFT,
#elif IS_ENABLED(CONFIG_BOARD_ADV360_RIGHT)
        .side = ADV360_SIDE_RIGHT,
#else
        .side = ADV360_SIDE_OTHER,
#endif
        .enabled = status_enabled,
        .battery_preview = battery_preview,
        .layer = active_layer,
        .caps_lock = hid_indicators & HID_INDICATOR_CAPS_LOCK,
        .num_lock = hid_indicators & HID_INDICATOR_NUM_LOCK,
        .scroll_lock = hid_indicators & HID_INDICATOR_SCROLL_LOCK,
        .battery_soc = battery_preview ? zmk_battery_state_of_charge() : 0,
        .lock_color = CONFIG_ZMK_ADV360_STATUS_LEDS_LOCK_COLOR,
        .bt_profile = bt_profile,
        .bt_profile_open = bt_profile_open,
        .bt_profile_connected = bt_profile_connected,
        .link_bonded = link_bonded,
        .link_connected = link_connected,
        .tick = blink_tick,
        .brightness_permille = CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS_PERMILLE,
    };
    struct adv360_rgb out[ADV360_STATUS_LED_COUNT];

    adv360_status_render(&in, out);

    clear_pixels();
    for (int i = 0; i < ARRAY_SIZE(pixels) && i < ADV360_STATUS_LED_COUNT; i++) {
        pixels[i] = (struct led_rgb){.r = out[i].r, .g = out[i].g, .b = out[i].b};
    }
}

static void update_work_handler(struct k_work *work) {
    refresh_connection_state();

    const bool blinking = status_enabled && !battery_preview && blink_needed();

    blink_tick = blinking ? blink_tick + 1 : 0;
    render_status();
    flush_pixels(status_enabled || battery_preview);

    if (blinking) {
        k_work_reschedule(&update_work, K_MSEC(ADV360_BLINK_TICK_MS));
    }
}

static void schedule_update(void) { k_work_reschedule(&update_work, K_NO_WAIT); }

#if ADV360_STATUS_CAN_SYNC_LAYER
static void sync_layer_to_peripherals(void) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = "stp",
        .param1 = ADV360_STP_SET_LAYER,
        .param2 = active_layer,
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
#else
static void sync_layer_to_peripherals(void) {}
#endif

int adv360_status_leds_show_battery(bool show) {
    battery_preview = show;
    schedule_update();
    return 0;
}

int adv360_status_leds_toggle(void) {
    status_enabled = !status_enabled;
    battery_preview = false;
    schedule_update();
    return 0;
}

int adv360_status_leds_set_layer(uint8_t layer) {
    active_layer = layer;
    schedule_update();
    return 0;
}

static int adv360_status_leds_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *hid_ev = as_zmk_hid_indicators_changed(eh);

    if (hid_ev != NULL) {
        hid_indicators = hid_ev->indicators;
        schedule_update();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_battery_state_changed(eh) != NULL && battery_preview) {
        schedule_update();
        return ZMK_EV_EVENT_BUBBLE;
    }

#if ADV360_STATUS_HAS_CENTRAL_STATE
    if (as_zmk_ble_active_profile_changed(eh) != NULL) {
        schedule_update();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh) != NULL) {
        active_layer = zmk_keymap_highest_layer_active();
        schedule_update();
        sync_layer_to_peripherals();
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

    k_work_init_delayable(&update_work, update_work_handler);

#if ADV360_STATUS_HAS_CENTRAL_STATE
    hid_indicators = zmk_hid_indicators_get_current_profile();
    active_layer = zmk_keymap_highest_layer_active();
#endif

    schedule_update();
    sync_layer_to_peripherals();
    return 0;
}

SYS_INIT(adv360_status_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
