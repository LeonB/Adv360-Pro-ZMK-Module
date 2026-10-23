# Host-side tests

Plain C, no Zephyr toolchain needed: `make -C tests/host test`.

| Test | Proves |
|---|---|
| `test_baseline_equiv` | The pure renderer (`src/adv360_status_render.c`) still produces exactly the pixels of the original driver (`ref/proffan_baseline.c`, verbatim from `41d16a2`), except for the deliberate parity changes listed in the test header. |
| `test_fork_differential` | The renderer matches the official firmware's LED code (`ref/fork_kinesis.c`, verbatim from `refil/zmk@d93499bb`) tick by tick: every layer, lock combination, BT profile / state, split-link state, and battery level. |

`ref/` holds reference oracles. Do not edit the extracted blocks; only the stub headers adapt them
to the host.

## What these tests do not cover

They test the colour logic only. Event wiring, the blink timer, ext-power handling and the split
sync run on the device and are covered by the firmware build plus the manual hardware checklist.
