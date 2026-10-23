# SPDX-License-Identifier: MIT

zephyr_include_directories(include)

target_sources_ifdef(CONFIG_ZMK_ADV360_STATUS_LEDS app PRIVATE
  src/adv360_status_leds.c
  src/adv360_status_render.c
  src/adv360_underglow_engine.c
  src/behaviors/behavior_adv360_rgb_underglow.c
  src/behaviors/behavior_adv360_status_tap.c
)
