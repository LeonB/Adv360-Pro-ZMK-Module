/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * The Kinesis V3.0 keymap references a custom status-tap behavior from the
 * Adv360 fork. The module implements it on top of its own underglow engine:
 * STP_BAT shows the battery effect while held, STP_TOG toggles the LEDs.
 */
#define STP_BAT 0
#define STP_TOG 1

/*
 * The fork's stp.h also pulls in the pointing macros and (with rgb.h) the RGB_EFS / RGB_MEFS
 * helpers, and official keymaps rely on that.
 */
#include <dt-bindings/zmk/pointing.h>
#include <dt-bindings/zmk/rgb_mefs.h>
