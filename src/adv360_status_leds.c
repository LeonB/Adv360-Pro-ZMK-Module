/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Device glue for the three Adv360 LEDs: runs the pure underglow engine (adv360_underglow_engine.c)
 * and renderer (adv360_status_render.c) from a timer and ZMK events, owns the LED strip and its
 * external power, and implements ZMK's zmk_rgb_underglow_*() API that the `&rgb_ug` behavior uses.
 *
 * The official Kinesis firmware polls every 50 ms while the LEDs are on. This driver redraws on
 * events, runs a 50 ms timer only while an animated effect is selected, and wakes once per blink
 * phase (not per tick) while an indicator blinks. Frames the strip already shows are not resent.
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

/* When the current blinking started (k_uptime_get() ms); the blink phase is derived from it. */
static int64_t blink_start_ms;
static bool blink_active;

/*
 * Everything that touches `ug` (and the blink counter) runs under this lock: forwarded `&rgb_ug`
 * and `&stp` commands arrive on the Bluetooth thread on the right half, while the tick runs on the
 * system work queue. The mutex is recursive, so API functions may call each other.
 */
K_MUTEX_DEFINE(adv360_ug_lock);

/* False until adv360_status_leds_init() has set up `ug`; events and commands before that are ignored. */
static bool ready;

static void tick_handler(struct k_work *work);
static void redraw_handler(struct k_work *work);

/* The 50 ms timer: advances animations and blinking. Only scheduled while something needs it. */
static K_WORK_DELAYABLE_DEFINE(tick_work, tick_handler);
/* Redraws the current frame after an event, without advancing time. */
static K_WORK_DEFINE(redraw_work, redraw_handler);
#if ADV360_STATUS_CAN_SYNC
static void sync_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sync_work, sync_work_handler);
#endif

static inline void ug_lock(void) { k_mutex_lock(&adv360_ug_lock, K_FOREVER); }
static inline void ug_unlock(void) { k_mutex_unlock(&adv360_ug_lock); }

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

/* What the strip currently shows, so an identical frame is not sent again. */
static struct adv360_rgb last_frame[ADV360_STATUS_LED_COUNT];
static bool last_frame_valid;

static void flush_pixels(const struct adv360_rgb *out, bool power_on) {
    int err;

    if (power_on && ext_power_is_on != 1) {
        err = set_ext_power(true);
        if (err) {
            LOG_WRN("Failed to enable Adv360 status LED power: %d", err);
        } else {
            ext_power_is_on = 1;
            last_frame_valid = false; /* a freshly powered strip shows nothing */
        }
    }

    /* An unpowered strip shows nothing, so there is nothing to send; and an unchanged frame is
     * already on it. */
    if (ext_power_is_on != 0 && (!last_frame_valid || memcmp(out, last_frame, sizeof(last_frame)))) {
        memset(pixels, 0, sizeof(pixels));
        for (int i = 0; i < ADV360_STATUS_LED_COUNT; i++) {
            pixels[i] = (struct led_rgb){.r = out[i].r, .g = out[i].g, .b = out[i].b};
        }

        err = led_strip_update_rgb(led_strip, pixels, STATUS_LED_COUNT);
        if (err) {
            LOG_ERR("Failed to update Adv360 status LEDs: %d", err);
            last_frame_valid = false;
        } else {
            memcpy(last_frame, out, sizeof(last_frame));
            last_frame_valid = true;
        }
    }

    /* Still switch the power off when the update failed, so the rail is not left on. */
    if (!power_on && ext_power_is_on != 0) {
        err = set_ext_power(false);
        if (err) {
            LOG_WRN("Failed to disable Adv360 status LED power: %d", err);
        } else {
            ext_power_is_on = 0;
            last_frame_valid = false;
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

/*
 * Draw one frame. With `advance` (the 50 ms timer) animated effects move on one step; without it
 * (an event such as a layer change) the current frame is redrawn from a copy, so events never speed
 * up animations. Blink phase comes from the clock, not from a counter. Then schedule the next wake:
 * one tick for animated effects, the next blink edge for blinking, otherwise none. Caller holds
 * ug_lock.
 */
static void render_frame(bool advance) {
    struct adv360_rgb out[ADV360_STATUS_LED_COUNT] = {0};
    int blink_limit = -1;
    uint32_t blink_elapsed_ms = 0;

    refresh_connection_state();

    if (ug.on) {
        struct adv360_status_input ctx = {
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
            .brightness_permille = CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS_PERMILLE,
        };

        if (ug.effect == ADV360_UG_EFFECT_KINESIS) {
            blink_limit = adv360_status_blink_limit(&ctx);
        }

        if (blink_limit < 0) {
            blink_active = false;
        } else {
            const int64_t now = k_uptime_get();

            if (!blink_active) {
                blink_active = true;
                blink_start_ms = now;
            }
            blink_elapsed_ms = (uint32_t)(now - blink_start_ms);
            ctx.tick = blink_elapsed_ms / ADV360_BLINK_TICK_MS;
        }

        if (advance) {
            adv360_ug_tick(&ug, &ctx, out);
        } else {
            struct adv360_ug preview = ug;

            adv360_ug_tick(&preview, &ctx, out);
        }
    } else {
        blink_active = false;
    }

    flush_pixels(out, ug.on);

    if (ug.on && adv360_ug_effect_animates(ug.effect)) {
        /* No-op if the timer is already pending, so redraws do not push an animation out. */
        k_work_schedule(&tick_work, K_MSEC(ADV360_BLINK_TICK_MS));
    } else if (ug.on && blink_limit >= 0) {
        /* The delay is computed from the clock, so replacing a pending wake is always correct. */
        k_work_reschedule(&tick_work, K_MSEC(adv360_blink_next_edge_ms(blink_limit, blink_elapsed_ms)));
    } else {
        k_work_cancel_delayable(&tick_work);
    }
}

static void tick_handler(struct k_work *work) {
    ug_lock();
    render_frame(true);
    ug_unlock();
}

static void redraw_handler(struct k_work *work) {
    ug_lock();
    render_frame(false);
    ug_unlock();
}

static void schedule_update(void) {
    if (ready) {
        k_work_submit(&redraw_work);
    }
}

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
    struct adv360_ug snapshot;
    bool resend_state;

    if (!ready) {
        return;
    }

    ug_lock();
    snapshot = ug;
    ug_unlock();

    resend_state = !full_sync_done || !adv360_ug_effect_animates(snapshot.effect);
    full_sync_done = true;
    sync_layer_to_peripherals();

    if (resend_state) {
        invoke_on_peripherals("rgb_ug", RGB_EFS_CMD, snapshot.effect);
        invoke_on_peripherals("rgb_ug", RGB_COLOR_HSB_CMD,
                              RGB_COLOR_HSB_VAL(snapshot.color.h, snapshot.color.s, snapshot.color.b));
        invoke_on_peripherals("rgb_ug", snapshot.on ? RGB_ON_CMD : RGB_OFF_CMD, 0);
    }
}
#else
static void sync_layer_to_peripherals(void) {}
#endif

/* ---- Public API: ZMK's zmk_rgb_underglow_*() (see zmk/rgb_underglow.h), on the Adv360 engine ---- */

int zmk_rgb_underglow_get_state(bool *on_off) {
    if (!ready) {
        return -ENODEV;
    }

    ug_lock();
    *on_off = ug.on;
    ug_unlock();
    return 0;
}

int zmk_rgb_underglow_on(void) {
    if (!ready) {
        return -ENODEV;
    }

    ug_lock();
    adv360_ug_on(&ug);
    ug_unlock();
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_off(void) {
    if (!ready) {
        return -ENODEV;
    }

    ug_lock();
    adv360_ug_off(&ug);
    ug_unlock();
    schedule_update();
    return 0;
}

int zmk_rgb_underglow_toggle(void) {
    bool on;

    if (zmk_rgb_underglow_get_state(&on) != 0) {
        return -ENODEV;
    }

    return on ? zmk_rgb_underglow_off() : zmk_rgb_underglow_on();
}

int zmk_rgb_underglow_calc_effect(int direction) {
    int effect = 0;

    if (ready) {
        ug_lock();
        effect = adv360_ug_calc_effect(&ug, direction);
        ug_unlock();
    }
    return effect;
}

int zmk_rgb_underglow_select_effect(int effect) {
    int err;

    if (!ready) {
        return -ENODEV;
    }

    ug_lock();
    err = adv360_ug_select_effect(&ug, effect);
    ug_unlock();

    if (err == 0) {
        schedule_update();
    }
    return err;
}

int zmk_rgb_underglow_cycle_effect(int direction) {
    int err;

    if (!ready) {
        return -ENODEV;
    }

    /* Calculate and select under one lock so the cycle cannot race another command. */
    ug_lock();
    err = adv360_ug_cycle_effect(&ug, direction);
    ug_unlock();

    if (err == 0) {
        schedule_update();
    }
    return err;
}

int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    int err;

    if (!ready) {
        return -ENODEV;
    }

    ug_lock();
    err = adv360_ug_set_hsb(&ug, (struct adv360_ug_hsb){.h = color.h, .s = color.s, .b = color.b});
    ug_unlock();

    if (err == 0) {
        schedule_update();
    }
    return err;
}

#define ADV360_HSB_TO_ZMK(c) ((struct zmk_led_hsb){.h = (c).h, .s = (c).s, .b = (c).b})

#define ADV360_CALC_HSB(fn, direction)                                                             \
    ({                                                                                             \
        struct zmk_led_hsb result = {0};                                                           \
        if (ready) {                                                                               \
            ug_lock();                                                                             \
            result = ADV360_HSB_TO_ZMK(fn(&ug, direction));                                        \
            ug_unlock();                                                                           \
        }                                                                                          \
        result;                                                                                    \
    })

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction) {
    return ADV360_CALC_HSB(adv360_ug_calc_hue, direction);
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction) {
    return ADV360_CALC_HSB(adv360_ug_calc_sat, direction);
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction) {
    return ADV360_CALC_HSB(adv360_ug_calc_brt, direction);
}

#define ADV360_CHANGE(fn, direction)                                                               \
    do {                                                                                           \
        if (!ready) {                                                                              \
            return -ENODEV;                                                                        \
        }                                                                                          \
        ug_lock();                                                                                 \
        fn(&ug, direction);                                                                        \
        ug_unlock();                                                                               \
        schedule_update();                                                                         \
        return 0;                                                                                  \
    } while (0)

int zmk_rgb_underglow_change_hue(int direction) { ADV360_CHANGE(adv360_ug_change_hue, direction); }
int zmk_rgb_underglow_change_sat(int direction) { ADV360_CHANGE(adv360_ug_change_sat, direction); }
int zmk_rgb_underglow_change_brt(int direction) { ADV360_CHANGE(adv360_ug_change_brt, direction); }
int zmk_rgb_underglow_change_spd(int direction) { ADV360_CHANGE(adv360_ug_change_spd, direction); }

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
    bool turn_on;

    /* A wake event while awake, or a sleep event while asleep, is a no-op. */
    if (target_wake_state == is_awake) {
        return 0;
    }
    is_awake = target_wake_state;

    if (is_awake) {
        turn_on = state_before_sleeping;
    } else {
        zmk_rgb_underglow_get_state(&state_before_sleeping);
        turn_on = false;
    }

    /* The official firmware sends the new on/off state to the right half too. */
#if ADV360_STATUS_CAN_SYNC
    invoke_on_peripherals("rgb_ug", turn_on ? RGB_ON_CMD : RGB_OFF_CMD, 0);
#endif
    return turn_on ? zmk_rgb_underglow_on() : zmk_rgb_underglow_off();
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
        if (ready) {
            bool battery_shown;

            ug_lock();
            battery_shown = ug.on && ug.effect == ADV360_UG_EFFECT_BATTERY;
            ug_unlock();

            if (battery_shown) {
                schedule_update();
            }
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

#if ADV360_STATUS_HAS_CENTRAL_STATE
    hid_indicators = zmk_hid_indicators_get_current_profile();
    active_layer = zmk_keymap_highest_layer_active();
#endif

    ready = true;

    schedule_update();
    sync_layer_to_peripherals();
#if ADV360_STATUS_CAN_SYNC
    k_work_reschedule(&sync_work, K_MSEC(ADV360_SYNC_DELAY_MS));
#endif
    return 0;
}

SYS_INIT(adv360_status_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
