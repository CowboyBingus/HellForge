# Custom stratagem selection categories

SDK 8 adds headings to the game's stratagem selection screen. The implementation is covered by the native synthetic
fixture and was checked in game on build 25480438 (2026-10-18: a heading with its stratagems in `selection_order`);
**controller navigation and saved loadouts still need an in-game check**.
The API example and limits are in [mods.md](mods.md#custom-selection-categories-sdk-8).

## Native path reviewed

The retained `F5FEE03DCFDB-20260929T172647Z-mission-host.hd2snap` game.dll image was inspected offline. Existing
research in `artifacts/stratagem-registry/BOUNDS.md` and the sibling HD2Runtime repository's
`research/stratagem-picker-F5FEE03DCFDB.json` identifies the picker. The relevant instructions:

| Address in game.dll | Role |
|---|---|
| `0x18D8710` | Selection-grid builder; filters available types before constructing candidates |
| `0x18D8BFD..0x18D8C15` | Candidate section from StratagemInfo `+0xB8`, type from `+0`, order from `+0x188` |
| `0x18D8C40` | Sorts 20-byte candidates using comparator `0x18D86E0` |
| `0x18D86E0` | Compares section, then order, then type, all unsigned |
| `0x18D43A0` | Appends an item key with section, enabled and blocked bytes; section changes start a row |
| `0x18D2E85` | Realize pass calls the stratagem heading renderer `0x18CEC00` with widget and first item key |
| `0x18CEC7F`, `0x18CECC7` | Header label at widget `+0x110`, count text at `+0x3C8` |
| `0x18CECF0..0x18CEDA9` | Counts selectable rows with catalogue entries and their available subset |

The sort-call wrapper changes only the candidates' section numbers and, for stratagems under a custom heading, their
order: `HfStratagem.selection_order` biased by `0x80000000` (the comparator is unsigned) replaces the order a copy
inherits from its base row's `+0x188`. The original comparator then groups and orders them, ties by type (call order).
Item keys, types, vanilla sections' order, disabled flags, selection handling and saved identities follow the existing
native path. The rows are not written: StratagemInfo `+0xB8` remains a vanilla category because other consumers use it
for HUD/ping presentation, and `+0x188` keeps the base's value.

Each registered category gets a session handle. UI section numbers start at 5 and are assigned by signed sort order,
then namespaced identity. The three native section arrays start at list `+0x9274C` (first row), `+0x927D0` (first card),
and `+0x92854` (section id); the next field begins at `+0x928D8`. Each holds **33** u32 entries. Reserving five vanilla
sections leaves **28 custom categories**. Registration refuses the 29th. Empty categories create no entries.

The header wrapper preserves the native renderer and supplies custom title text plus counts grouped by selection
category. Counts use the native catalogue lookup `0x1372290` and availability predicate `0x136FC20`, just as the
original renderer does. Vanilla counts exclude stratagems assigned to custom groups. Since other screens share the
renderer, the header thunk passes the list in RSI as a third argument; the wrapper only changes a list whose keys
and section boundaries match the selection builder's latest output.

Only two call sites are redirected, through nearby executable jump thunks. `kCategoryPins` checks the candidate
layout, sort call, header call, section-array writes, widget offsets and called helpers before either is changed.
The existing stratagem installation independently guards its registry and bounds. A category guard/allocation/write
failure logs a reason and leaves grouping on the vanilla path. If only the heading call was redirected, that wrapper
remains a pass-through. This feature does not increase the existing 48-new-stratagem or 150-picker-candidate limits.

## Validation

The engine's native test of the categories (run by every engine build) exercises
namespacing, shared definitions, conflicts, bad and late registrations, older struct sizes, deterministic ordering,
selection order within a heading (signed range, ties in call order, vanilla sections untouched),
mixed native categories in one group, unchanged keys/flags/HUD categories, custom and vanilla counts, reopening an
empty picker, shared-renderer isolation, maximum registration and rejection of mismatched native instructions.

In-game acceptance still required: install a mod with two custom headings and several stratagems (including mixed
offensive/defensive bases in one heading); inspect names/counts, scroll between headings, select with mouse and
controller, reopen each loadout slot, save/reload the loadout, and verify call-ins and ping/beam colours. Also test a
loadout with just one nonempty custom category and a run without category-using mods.

## Hellpod crash investigated, 2026-10-18

The first reported hellpod crash was a native loadout-preview recursion, before custom category candidates were
built. In `helldivers2.exe.40780.dmp`, the category cache count and candidate storage were zero. The stack contained
182 returns to `game.dll+0x18EA758`: the query at `0x18E9D50` repeatedly retried alternate preview 1 for slot 6, mode 0,
context 0. Those frames held resource `0x5C14F27759DD3BE0` (G-8 Immolation). The eventual access violation in the
loader's crash logger was its stack probe running after the native recursion had exhausted the stack.

`loadout_preview.c` guards this query against repeating an active `(alternate, slot, mode, context)` on the same
thread. A cycle returns the zeroed 76-byte result the native query already uses for a missing loadout row. Ordinary
queries and distinct nested queries continue through the original function. Entry, empty-result initialization and
recursive-call bytes are checked before hooking. The regression fixture reproduces the ordinary-to-alternate-to-same-
alternate cycle and checks termination, output bounds and preservation of distinct queries. Live validation remains
necessary; a missing preview part may remain empty instead of recursively retrying it.
