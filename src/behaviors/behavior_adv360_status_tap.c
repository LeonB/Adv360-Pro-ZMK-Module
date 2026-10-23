/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT kinesis_behavior_adv360_status_tap

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/stp.h>

#include <zmk/rgb_underglow.h>
#include <zmk_keyboard_adv360/status_leds.h>
#include <zmk_keyboard_adv360/underglow.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Internal parameters, never written in a keymap; must match adv360_status_leds.c. */
#define ADV360_STP_SET_LAYER 2
#define ADV360_STP_SET_ON 3

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .value = STP_BAT,
        .display_name = "Battery Level",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
    {
        .value = STP_TOG,
        .display_name = "Toggle LEDs",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/*
 * STP_TOG toggles the LEDs on both halves. Each half would otherwise toggle its own state, so the
 * central turns it into an absolute "set on/off" before it is sent on.
 */
static int on_keymap_binding_convert_central_state_dependent_params(
    struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (binding->param1 == STP_TOG) {
        bool on;
        int err = zmk_rgb_underglow_get_state(&on);

        if (err) {
            return err;
        }

        binding->param1 = ADV360_STP_SET_ON;
        binding->param2 = !on;
    }

    return 0;
}

/*
 * As in the official firmware, STP_BAT is the battery effect while the key is held and the status
 * (kinesis) effect again on release.
 */
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case STP_BAT:
        return zmk_rgb_underglow_select_effect(ADV360_UG_EFFECT_BATTERY);
    case STP_TOG:
        return zmk_rgb_underglow_toggle();
    case ADV360_STP_SET_ON:
        return adv360_status_leds_set_on(binding->param2);
    case ADV360_STP_SET_LAYER:
        return adv360_status_leds_set_layer(binding->param2);
    default:
        LOG_ERR("Unknown Adv360 status command: %d", binding->param1);
        return -ENOTSUP;
    }
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case STP_BAT:
        return zmk_rgb_underglow_select_effect(ADV360_UG_EFFECT_KINESIS);
    case STP_TOG:
    case ADV360_STP_SET_ON:
        return ZMK_BEHAVIOR_OPAQUE;
    default:
        return -ENOTSUP;
    }
}

static const struct behavior_driver_api behavior_adv360_status_tap_driver_api = {
    .binding_convert_central_state_dependent_params =
        on_keymap_binding_convert_central_state_dependent_params,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_adv360_status_tap_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
