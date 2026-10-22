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

## DEV checkpoint instrumentation — PASS

Causal checkpoint: `sd-ep3-r4-b00`.

Confirmed on Android:

- SceneKey resolved to `02/02-2K-B00`;
- Route 4 / Scene 1 restored;
- checkpoint state dump emitted;
- first route resolution selected `t263 -> 02/02-2K-C00`;
- checkpoint helpers remain compile-time gated behind `KOTONOHA_DEV_CHECKPOINTS`.

Targeted routing-only checkpoint `sd-ep4-r15-e08-routing-only` is also present and covered by the standalone DEV checkpoint test.

## Windows full engine integration — PASS

A real interactive native Windows run with the complete physical ORS set loaded progressed through:

```text
EP1 -> EP2 -> EP3 -> EP4 -> EP5 -> EP6 -> Ending 16
```

Observed integration milestones include:

- delayed routing at physical `[Next]`;
- prompt propagation and feeling updates;
- same-episode and cross-route changes;
- episode-prefix transitions through all six episodes;
- routing-only dispatcher auto-resolution;
- native ending registration.

Ending 16 completed in the router through:

```text
ending=16
global REP05_SB_I01=1
callback ignored in normal-new-game router: callback_38
terminal transition t2357 ending=16 registrations=1
```

The current engine policy returns `SDL_APP_SUCCESS` on `NextKind::Terminal`, so process termination after `t2357` is expected from the present integration. Correct post-ending UI / `callback_38` behavior remains a separate pending engine task.

See `WINDOWS_ENGINE_INTEGRATION_20261004.md`.

## Routing-only no-ORS nodes — 2 / 2 Windows PASS

Repository-wide scene inventory audit proves exactly two RouteProc nodes have no physical ORS:

- `03/03-B2-A00` — Route 12 / Scene 0;
- `03/03-KB-E00` — Route 15 / Scene 31.

### Route 15 dispatcher — PASS

Observed in the full Windows engine:

```text
next t917 -> 03/03-KB-E00
routing-only node 03/03-KB-E00 (no ORS); resolving immediately
next t907 -> 03/03-KB-G00
```

The physical destination continued normally.

### Route 12 dispatcher — PASS

Targeted Windows smoke test used physical predecessor `03/03-SB-I03` (Route 18 / Scene 60) with `001=10`, `002=20`.

Observed:

```text
ROUTE=12
SCENE=0
next t1212 -> 03/03-B2-A00
routing-only node 03/03-B2-A00 (no ORS); resolving immediately
BS03B2A02=0
ROUTE=12
SCENE=2
next t629 -> 03/03-B2-A02
```

This validates the zero-Gameplay handling for both proven virtual dispatchers while preserving hard failure behavior for arbitrary missing physical destinations.

## Android ARM64 integration — revalidation pending

Previously confirmed on device:

- `00 -> 01`;
- `01 -> 02`;
- `02 -> 03` through `t322 -> 03/03-KB-A00`.

The earlier Android run exposed the no-ORS dispatcher problem before the fix existed. Current Android acceptance target is therefore to rebuild and confirm the same routing-only auto-resolution behavior already proven on Windows.

## Exhaustive executable verifier target

PASS target:

- reachable SceneKeys = 1,856;
- reachable transitions = 2,458;
- reachable endings = 22;
- unexpected unresolved reachable states = 0;
- invalid destinations = 0;
- six known dead transitions remain unreachable.

The exact 42,404,396 state count uses a historical projection. A future verifier must document its exact state-key projection before treating raw state-count equality as a proof condition.

## ORS-only natural execution — CLOSED

Final installed-edition classification:

- `01/01-00-OP2`: **NATURAL_EXECUTION = NO**;
- `05/05-9O-B00`: **NATURAL_EXECUTION = NO**;
- externally forced loading remains **POSSIBLE** for both and is explicitly outside the router model.

All known natural SceneKey producer domains have been closed. The physical ORS-only scripts must **not** be added as router nodes.

Inventory interpretation:

```text
Router SceneKeys:       1857
Physical ORS SceneKeys: 1857

Router-only:
  03/03-B2-A00
  03/03-KB-E00

ORS-only:
  01/01-00-OP2
  05/05-9O-B00
```

Semantics:

- **router-only** = logical RouteProc dispatcher nodes with no physical ORS;
- **ORS-only** = retained physical scripts that are not naturally selected by any audited installed-edition producer.

## Current open integration items

1. recover and implement correct post-ending / `callback_38` behavior instead of terminating the process directly;
2. revalidate the routing-only fix on Android ARM64;
3. finish/freeze executable verification and final certification artifacts before KTRF.
