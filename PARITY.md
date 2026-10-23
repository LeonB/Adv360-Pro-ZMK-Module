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

### B. Underglow functionality (not started)

- [ ] Effects: solid, breathe, spectrum, swirl, battery, test; select / cycle / `EFS` / `MEFS`
- [ ] Runtime hue, saturation, brightness, speed (`RGB_HUI` ... `RGB_SPD`, `RGB_COLOR_HSB`)
- [ ] `RGB_ON` / `RGB_OFF` / `RGB_TOG` that also switch external LED power
- [ ] Auto-off on idle and on USB disconnect
- [ ] Start-up and limit settings (`HUE/SAT/BRT/SPD/EFF/ON_START`, `BRT_MIN/MAX/SCALE`)
- [ ] Central-to-peripheral sync of LED state
- [ ] Keep `&rgb_ug` bindings working without keymap changes

## Findings

- **Brightness differs a lot.** The fork scales every indicator colour by
  `CONFIG_ZMK_RGB_UNDERGLOW_BRT_SCALE / 250` and the default scale is `1`, i.e. 4 per mille: a full
  `0xFF` channel is output as `1`. This module defaults to 20 % (`0xFF` -> `51`). The renderer takes
  per-mille so both can be reproduced exactly; the module's default has intentionally not been
  changed.
- The fork persists no underglow state (`zmk_rgb_underglow_save_state()` returns 0), so there is
  nothing to port there.
- Board config differences vs the official defconfigs: this module omits `CONFIG_ZMK_USB` on the
  right half, enables `CONFIG_ZMK_BLE_EXPERIMENTAL_FEATURES`, and defaults `CONFIG_BT_BAS=n`.
- `ext_power_enable()` schedules a settings save on every call, so the driver now only calls it
  when the power state changes (the blink timer redraws every 50 ms).

## Tests

See [tests/host/README.md](tests/host/README.md).
