# Ideas (not implemented)

## Enter the bootloader from the Mac (1200 baud touch)

Today a half enters the UF2 bootloader with Mod + Macro 1 (left) / Mod + Macro 3 (right), or the
physical reset button. A host cannot press those keys, so the firmware needs a trigger the Mac can
send.

**Why it looks practical:** the left half is built with ZMK Studio's USB serial interface
(`studio-rpc-usb-uart`); the build config has the USB CDC ACM port and `CONFIG_UART_LINE_CTRL`
enabled, so the Mac already sees a `/dev/cu.usbmodem*` device. The right half has no USB data
connection, so it has no port of its own.

**Approach**
- Left half: watch the baud rate on that port; when it is opened at 1200 baud, reboot into the UF2
  bootloader (the same reboot ZMK's `&bootloader` behavior does).
- Right half: a second magic rate (e.g. 2400); the left half forwards a `bootloader` behavior
  invocation to the right half over the split link (the module already invokes behaviors on the
  peripheral). Only works while the right half is connected.
- Mac side: `stty -f /dev/cu.usbmodemXXXX 1200`, or `serial.Serial(port, 1200).close()` in Python;
  plus a `bin/flash.sh` that does the touch, waits for the `ADV360PRO` drive and copies the `.uf2`.
- Probably a Kconfig option, default on.

**Caveats**
- Only works while the current firmware runs; a hung or broken half still needs the reset button.
- Studio shares the port; only a 1200/2400 baud open triggers this. Untested whether Studio's client
  ever opens it at those rates (unlikely).
- Anything on the Mac that can open the port can reboot the keyboard into the bootloader.
- The reboot glue runs on the device; a host test can only cover the baud-to-action decision.

**Size:** roughly 60 lines in the module, the flash script, and a small host test for the decision.

## The right half sleeps while you type on the left (ZMK limitation)

**What you see.** After ~30 s without a key press *on the right half*, its backlight goes off, even
while you keep typing on the left. After a full idle, a key on the left brings back the left
backlight and (through this module) the indicator LEDs on both halves, but not the right backlight;
that only returns when a key on the right is pressed. A key on the right does wake the left.

**Why (verified in ZMK at `cb615aee`).**
- Every half runs its own idle timer (`app/src/activity.c`). It only counts position and sensor
  events from *that half*.
- A peripheral forwards its key presses to the central, so the central's timer sees both halves.
  The central never forwards its key presses to the peripheral, so the peripheral's timer never
  sees them.
- ZMK's backlight auto-off (`backlight.c`) saves its own `prev_state` privately and restores it
  when `zmk_activity_get_state()` becomes ACTIVE, i.e. only on that half's own activity. There is no
  public call to trigger that restore from outside, and raising a fake `zmk_activity_state_changed`
  does not work (the backlight reads the state, not the event). A fake key or sensor event on the
  peripheral would be forwarded to the central as real input, so that is not an option either.
- The official Kinesis firmware uses the same ZMK, so it behaves the same.

**Upstream status (zmkfirmware/zmk).**
- Issue #2408 "Synced sleep between central/peripherals" (open since 2024-08): the same complaint
  ("key presses on a peripheral keep the central awake but not the reverse"). No maintainer comment,
  no label, no assignee. Commenters want it: one says there is no reason not to sync, another has a
  gaming setup (central on USB, peripheral unplugged) where the peripheral sleeps mid-session.
- PR #2459 "sync central & peripherals last activity timing" (by angweekiat, 2024-09): the central
  broadcasts how long it has been inactive over a new GATT characteristic, either periodically or on
  key/sensor events with a minimum interval, and the peripheral adjusts its own last-activity time.
  It also syncs on (re)connect and wakes a peripheral that is idle when the central comes online.
  **It was never reviewed** (no reviews, no inline comments, no assignee; labels `core`, `split`)
  and was closed by the stale bot on 2025-08-07 after 10 months without activity. It was not
  rejected. In Nov 2025 a user wrote that they use the event-based sync and it solves their problem;
  in Feb 2026 someone asked for it to be picked up.
- Open design question from the thread: sync the inactivity *time* (author and one commenter prefer
  it, so a central on USB does not keep a battery peripheral awake forever) or the *state*.
- #1317 is a broader (2022) discussion about peripheral power management; also open, no outcome.

**Options for this module (none done)**
1. Leave it as is (same as official).
2. When the left half wakes from idle, send its backlight state to the right half
   (`&bl BL_ON`/`BL_OFF`, only if the left backlight is on after ZMK restored it). Needs a short
   delay because the order of ZMK's restore and ours is not defined. Fixes "after full idle" only.
3. Option 2 plus a rate-limited "I am active" message from the left while it is used (e.g. at most
   once per 10 s), the same idea as PR #2459 but done in this module's own driver, so the right half
   stays awake while you type on the left. Small extra Bluetooth traffic; deviates most from ZMK.
4. Pick up PR #2459 upstream (rebase onto current ZMK) instead of working around it here.

Decision: try it as is for a few days first.
