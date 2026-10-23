/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <dt-bindings/zmk/rgb.h>

/*
 * What the Kinesis fork's rgb.h has on top of upstream's, so keymaps written for the official
 * firmware build unchanged. stp.h includes this file, and every Adv360 keymap includes stp.h.
 *
 *   &rgb_ug RGB_EFS 4        select effect 4
 *   &rgb_ug RGB_MEFS 5       effect 5 only while the key is held (RFB_MEFS: the fork's spelling)
 */
#ifndef RGB_EFS
#define RGB_EFS RGB_EFS_CMD
#endif

/* Upstream's rgb.h has no momentary effect command, so it gets a number outside its range. */
#define RGB_MEFS_CMD 15
#define RGB_MEFS RGB_MEFS_CMD
#define RFB_MEFS RGB_MEFS_CMD
