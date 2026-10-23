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
