/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Momentary effect select, as in the Kinesis ZMK fork: `&rgb_ug RGB_MEFS <effect>` switches to the
 * effect while the key is held and back to the previous one on release. Upstream's rgb.h has no
 * such command, so it gets a number outside upstream's range.
 */
#define RGB_MEFS_CMD 15
#define RGB_MEFS RGB_MEFS_CMD
