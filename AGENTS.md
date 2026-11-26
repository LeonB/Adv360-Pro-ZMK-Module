# Agent notes

## Naming keys

When telling the user which key to press, name the **physical key** (as printed on the
keycap / where it sits on the board), not the ZMK binding or position number.
Write the layer key first, then the key to tap, e.g. `Mod + Right Alt`.

- Say "Mod + Right Alt", not "`&mo 3` + `&bt BT_CLR`" or "position 37".
- Say which half and where, if it is not obvious (e.g. "Mod: top row of the right half,
  left of `6`").
- The binding name may follow in parentheses when useful, e.g. `Mod + Right Alt (BT_CLR)`.
- Look up the physical key from the base layer at the same position
  (`config/keypos_adv360.h` has the position map).
