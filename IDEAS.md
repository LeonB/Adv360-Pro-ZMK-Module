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
