# Hardware test checklist

Nothing in `feature/official-parity` has run on a keyboard yet. Work through this after flashing and
write down what you see; anything that does not match the "expect" is a bug to report.

Keys are named by matrix position (see `config/keypos_adv360.h`) because the keycap legends are
not recorded anywhere in the repo. **Mod** = position 7 (right half, top row, innermost key).
"Macro 1..4" are the inner-column keys below Keypad/Mod; Macro 4 is *assumed* to be position 39,
check the legend on your keyboard.

The LED strip is very dim by default (see "Brightness" below). Test in a dim room.

## 0. Before flashing

- [ ] The official firmware you currently run is still in `Adv360-Pro-ZMK/firmware/` (your way back).
- [ ] Files to flash, from `Adv360-Pro-ZMK-Module/firmware/`:
      `peek-27eda85-right-clique.uf2` and `peek-27eda85-left-clique.uf2`.
- [ ] Order: right half first (Mod + Macro 3 enters its bootloader), then left (Mod + Macro 1).
      If a half is unreachable, use its reset button.

## 1. Flash and first boot

- [ ] Both halves accept the file and the drive disappears by itself.
- [ ] After a few seconds both halves reconnect to each other (right half stops blinking red).
- [ ] The left half is still recognised by your computer, over USB and over Bluetooth, without
      re-pairing. (If it needs re-pairing, note that: `BLE_EXPERIMENTAL_FEATURES` is off, as in your
      official build.)
- [ ] Typing works on both halves; no stuck keys, no repeated keys.
- [ ] The halves still work after a power cycle (switch both off, then on).

## 2. Keymap regression (nothing should have changed)

- [ ] Base layer on both halves.
- [ ] Keypad layer (`&tog 1`, position 6), toggles on and off.
- [ ] Mod layer (hold Mod), layer 2 (`&mo 2`), layer 4 (`&mo 4`, position 66).
- [ ] Your macros, `HYPR`, `&mt LCTRL ESC`, homerow behaviour.
- [ ] Pointing / mouse behaviour, if you use any.
- [ ] ZMK Studio: Mod + position 28 (`&studio_unlock`), then connect Studio and see the keymap.

## 3. Layer colours (left LED 3, right LED 1)

Hold or toggle each layer and compare both halves:

| Layer | Expect (both halves) |
|---|---|
| 0 Base | off |
| 1 Keypad | white |
| 2 | blue |
| 3 Mod | green |
| 4 | red |

- [ ] Colours match the table and the two halves agree.
- [ ] The colour changes on the right half as fast as on the left (no visible lag).

## 4. Lock indicators

- [ ] Caps Lock: left LED 1 lights white, goes off again.
- [ ] Num Lock / Scroll Lock on the right half, if your OS/host reports them (macOS often does not).

## 5. Bluetooth profile LED (left LED 2)

Select a profile with Mod + `1`..`5` (profiles 1-5).

| Profile | Colour when connected |
|---|---|
| 1 | white |
| 2 | blue |
| 3 | red |
| 4 | green |
| 5 | none (always dark) |

- [ ] Each connected profile shows its colour, steady.
- [ ] Select a profile that was never paired: the LED **blinks fast** (about 150 ms phases).
- [ ] Turn Bluetooth off on the host (or take the host away) on a paired profile: the LED
      **blinks slowly** (about 0.7 s phases), and goes steady again when the host reconnects.
- [ ] Blink starts dark, and the rate does not change while you type or switch layers.

## 6. Right half link alert

- [ ] Switch the **left** half off while the right stays on: all three right LEDs **blink red slowly**.
- [ ] Switch the left half back on: red blinking stops by itself within a few seconds.
- [ ] (Optional) Flash `settings-reset.uf2` to the right half only: it should **blink red fast**
      (not bonded) until it is paired again. Re-flash the firmware afterwards.

## 7. Indicator on/off (`RGB_TOG` = Mod + Space) and battery key

- [ ] Mod + Space: indicators go dark, and the lock/layer/BT lights stop. Press again: they return,
      same colours.
- [ ] Mod + Macro 4 held, indicators **on**: all LEDs show the battery colour of their own half
      (green 80%+, yellow 50-79%, orange 20-49% (looks like red at default brightness), red below
      20%). Release: back to normal.
- [ ] Indicators **off** (Mod + Space first): hold Mod + Macro 4: battery colour shows, release: dark
      again. *(New in the module; the official firmware shows nothing here.)*
- [ ] Tap the key quickly several times: no stuck battery colour, always returns.
- [ ] While holding the battery key, press Mod + Space: nothing breaks, state ends consistent.

## 8. Both halves stay in sync

- [ ] Toggle the indicators off, then change layers a few times: both halves stay off.
- [ ] Toggle them on: both halves come on together.
- [ ] Power-cycle only the **right** half while the indicators are off on the left: note whether it
      comes up lit. Expected limitation: it catches up at the next layer change.
- [ ] Power-cycle only the **left** half: the right half's red blink starts and stops correctly.

## 9. Idle auto-off (left half is configured, as in official)

- [ ] Leave the keyboard untouched for about 30 s (ZMK idle timeout): the left LEDs go dark **and
      the right LEDs go dark too**.
- [ ] Press a key: both come back, with the same colours as before.
- [ ] If you had toggled them off before idling, they stay off after waking.

## 10. Backlight (per-key LEDs, separate system)

- [ ] Mod + Enter toggles the backlight, Mod + Down brighter, Mod + Up dimmer.
- [ ] Backlight state does not affect the indicator LEDs (and vice versa).

## 11. Bluetooth battery reporting (`BT_BAS=y`)

- [ ] Your computer shows the keyboard's battery level, if it supports it.
- [ ] No spontaneous wake-ups of the computer over a few hours (the README warns this can happen).
      If it does, set `CONFIG_BT_BAS=n` in the left defconfig.

## 12. Stability and power (use it for a day)

- [ ] No resets, hangs, or LED flicker, including after the keyboard sleeps and wakes.
- [ ] Right half battery life is not noticeably worse than with the official firmware, especially if
      it sits blinking red for a long time. (Not measured; this is a feel test.)
- [ ] Nothing odd in how the LEDs look after changing layers very fast.

## 13. Brightness (judging only, then tell me)

The status colours default to `CONFIG_ZMK_ADV360_STATUS_LEDS_BRIGHTNESS_PERMILLE=4` (1/250, the
official value), so orange and red look the same.

- [ ] Are the LEDs bright enough to read in your normal lighting? If not, try 100-200 in
      `boards/kinesis/adv360/adv360_{left,right}_nrf52840_zmk_defconfig`.

## 14. Underglow effects (needs a temporary keymap edit)

Your keymap only has `RGB_TOG`. To test effects, temporarily bind e.g. `&rgb_ug RGB_EFF`,
`RGB_HUI`, `RGB_BRI`, `RGB_SPI` and `&rgb_ug RGB_EFS 1` on free Mod-layer keys, rebuild and flash.

- [ ] `RGB_EFS 0..6`: solid, breathe, spectrum, swirl, status (default), battery, self-test.
- [ ] Animated effects (breathe/spectrum/swirl) stay smooth, on both halves, even while you type
      and change layers.
- [ ] Hue / saturation / brightness / speed steps change the colour as expected.
- [ ] The self-test sweeps, ends on all white, and stays white.
- [ ] Switching from an animated effect back to the status effect shows the indicators again.

## If something is wrong

- Roll back with the official `.uf2` files (right half first, then left).
- If the halves will not pair after a flash, flash `settings-reset.uf2` to both, then the firmware
  to both, then turn both off and on together.
- Report: which item, what you saw, which half, and the firmware file name (it contains the commit).
