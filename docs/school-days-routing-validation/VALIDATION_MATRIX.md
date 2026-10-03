# Validation Matrix

## Model baseline

| Metric | Baseline |
|---|---:|
| Route tables | 55 |
| SceneKeys | 1,857 |
| Causally reachable SceneKeys | 1,856 |
| Reachable forward transitions | 2,458 |
| Native endings reachable | 22 / 22 |
| Projected reachable states | 42,404,396 |

Known unreachable SceneKey: `04/04-C0-A05`.

Known dead forward transitions: `t0971`, `t1243`, `t1248`, `t1494`, `t1824`, `t1825`.

## Generated C++ router

Baseline commit: `a81ed02d0171be1e8bb13d9d4b17658e66aaac95`.

Existing witness suite: `tests/SchoolDaysFullRouterTest.cpp`.

PASS requires all 22 ending witnesses and every expected transition ID to match.

## DEV checkpoint instrumentation — PASS on Android

Current checkpoint: `sd-ep3-r4-b00`.

Confirmed:

- SceneKey resolved to `02/02-2K-B00`
- Route 4 / Scene 1 restored
- checkpoint state dump emitted
- first route resolution selected `t263 -> 02/02-2K-C00`
- checkpoint helpers remain compile-time gated behind `KOTONOHA_DEV_CHECKPOINTS`

## Engine integration

Already observed: delayed routing at `Next`, prompt propagation, feeling updates, route changes, `00 -> 01`, `01 -> 02`, and destination SceneKey lookup.

Confirmed on device: `02 -> 03` through `t322 -> 03/03-KB-A00` with `ROUTE=15`, `SCENE=0`. Still desired for one full end-to-end device path: `03 -> 04`, `04 -> 05`, terminal ending.

## Exhaustive executable verifier target

PASS target:

- reachable SceneKeys = 1,856
- reachable transitions = 2,458
- reachable endings = 22
- unexpected unresolved reachable states = 0
- invalid destinations = 0
- six known dead transitions remain unreachable

The exact 42,404,396 state count uses a historical projection. A future verifier must document its exact state-key projection before treating raw state-count equality as a proof condition.

## Final certified graph

The final graph should be generated from verified executable reachability, not hand-edited.

Recommended semantics: reachable transitions green, dead branches red, structural unreachable nodes gray, choice nodes blue, terminal endings gold.
