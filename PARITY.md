# Parity with the official Kinesis firmware

Goal: make this ZMK module (built on upstream ZMK) behave like the official
[Adv360-Pro-ZMK](https://github.com/KinesisCorporation/Adv360-Pro-ZMK) firmware, which uses the
`refil/zmk` fork (`adv360-z3.5-2`). The reference for LED behaviour is the fork's
`app/src/rgb_underglow.c` at `d93499bb877f2711989e6cbbe1feed9e9a792ca3`.

## Status

### A. Status indicators

- [x] Left LED 2 shows the active Bluetooth profile (white / blue / red / green)
- [x] ... blinks fast when the profile is unpaired and slowly when it is not connected
- [x] Right half: all LEDs blink red when not bonded (fast) or not connected (slow) to the left half
- [x] Configurable lock indicator colour (`CONFIG_ZMK_ADV360_STATUS_LEDS_LOCK_COLOR`)

### B. Underglow functionality

- [x] Effects: solid, breathe, spectrum, swirl, battery, test (plus the status effect); select / cycle
      / `RGB_EFS` / momentary `RGB_MEFS` (`dt-bindings/zmk/rgb_mefs.h`)
- [x] Runtime hue, saturation, brightness, speed (`RGB_HUI` ... `RGB_SPD`, `RGB_COLOR_HSB`)
- [x] `RGB_ON` / `RGB_OFF` / `RGB_TOG`, switching the external LED power
- [x] Auto-off on idle (on for the left half, as official) and on USB disconnect (off, as official)
- [x] Start-up and limit settings: `CONFIG_ZMK_ADV360_STATUS_LEDS_{EFF,HUE,SAT,BRT,SPD}_START`,
      `_{HUE,SAT,BRT}_STEP`, `_BRT_MIN`, `_BRT_MAX`, `_ON_START`
- [x] Both halves stay in sync (global behaviors + a resync from the central, see below)
- [x] `&rgb_ug` and `&stp` bindings work without keymap changes: the Kinesis V3.0 keymap builds
      unchanged on this module for both halves

### Deliberate differences from the official firmware

- **Timer:** the fork redraws every 50 ms whenever the LEDs are on. This module runs the 50 ms timer
  only while an effect animates or an indicator blinks, and otherwise redraws on events.
- **Peripheral resync:** the fork pushes its LED state to the right half over a custom split message
  2.5 s after it connects. Upstream ZMK gives the central no event for a peripheral connecting, so
  the central re-sends the state 2.5 s after boot and 0.5 s after each layer change instead. Key
  commands always reach both halves directly. If only the right half restarts, it catches up at the
  next layer change.
- **Naming:** Kconfig options are `CONFIG_ZMK_ADV360_STATUS_LEDS_*`, not `ZMK_RGB_UNDERGLOW_*`
  (those symbols belong to upstream's underglow, which this module does not use). The fork's
  `BRT_SCALE` is `BRIGHTNESS_PERMILLE`.
- **`stp`:** the `&stp` node is declared in the board dtsi, as in the fork's global behaviors. A
  keymap that declares its own `stp` node must drop it. `STP_TOG` is a module addition (toggle).
- **Not carried over:** the fork's split LED message and its `zmk,underglow` chosen node.

## Findings

- **Brightness differs a lot.** The fork scales every indicator colour by
  `CONFIG_ZMK_RGB_UNDERGLOW_BRT_SCALE / 250` and the default scale is `1`, i.e. 4 per mille: a full
  `0xFF` channel is output as `1`. This module defaults to 20 % (`0xFF` -> `51`). The renderer takes
  per-mille so both can be reproduced exactly. The module now defaults to the official value:
  `CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS_PERMILLE=4` (replaces the old percent option, default 20).
- The fork persists no underglow state (`zmk_rgb_underglow_save_state()` returns 0), so there is
  nothing to port there.
- Board config differences vs the official defconfigs: this module omits `CONFIG_ZMK_USB` on the
  right half, enables `CONFIG_ZMK_BLE_EXPERIMENTAL_FEATURES`, and defaults `CONFIG_BT_BAS=n`.
- `ext_power_enable()` schedules a settings save on every call, so the driver now only calls it
  when the power state changes (the blink timer redraws every 50 ms).

## Tests

See [tests/host/README.md](tests/host/README.md). Nothing in this branch has been run on a keyboard yet.
