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

## Routing-only no-ORS nodes

Repository-wide scene inventory audit: PASS. Exactly two RouteProc nodes have no physical ORS:

- `03/03-B2-A00` — Route 12 / Scene 0
- `03/03-KB-E00` — Route 15 / Scene 31

Android exposed the second node through `t917`. The audit also found exactly two physical ORS scripts absent from the normal-New-Game router: `01/01-00-OP2` and `05/05-9O-B00`. Those ORS-only scripts are a separate classification task and are not treated as routing-only nodes.

The engine integration has been updated to resolve only the two proven routing-only nodes immediately. This behavior still requires rebuild/device revalidation.


## ORS-only semantic closure

`01/01-00-OP2` is now classified as a genuine alternate opening script: it plays the Setsuna opening media while `01/01-00-OP1` plays the Sekai opening. The exact selector/loading mechanism remains unresolved and must be proven before final closure.

`05/05-9O-B00` remains semantically unresolved. The earlier apparent reference from `05/05-9O-A00` was a `PlayVoice` media-path collision and is not control-flow evidence.



## ORS-only RouteProc-table reachability — CLOSED; global execution — UNKNOWN

For the normal-New-Game routing scope:

- `01/01-00-OP2` is a physical Setsuna opening variant but has no normal-New-Game RouteProc node or control-flow reference. The separate Route-0 `PV/SETUNA-OP` mechanism is callback/mode-gated and outside normal New Game.
- `05/05-9O-B00` is retained physical content but cannot be reached through the recovered normal-New-Game graph. Route 36 is a one-node ending route containing only `05/05-9O-A00`.

Neither ORS-only asset should be inserted into the 1,857-node table-driven router. However, the installed EXE exposes a generic direct-launch path whose full caller/name provenance is not yet resolved, so global execution of either asset remains UNKNOWN.


### Installed-game deep audit

A read-only audit of the installed EXE, RouteProc, SysMenu and all 31 GPK indexes confirmed 55 tables / 1,857 SceneKeys with zero table divergence.

Acceptance status:

- `01/01-00-OP2`: **UNKNOWN globally**; absent from RouteProc tables, no proven alias from `PV/SETUNA-OP`.
- `05/05-9O-B00`: **UNKNOWN globally**; Route 36 produces only A00 and terminates, but direct-launch and later callback-state consumers are not exhaustively ruled out.

Next closure step: trace or statically close every name source reaching the EXE direct-launch/ORS-loader path.


### Save/SLog direct-launch surface

Status: **PARTIAL, materially narrowed**

- SLog grammar for all 22 current slot saves: **CONFIRMED**
- Save -> direct launch name transfer: **CONFIRMED**
- OP2 in existing saves: **NO-IN-EXISTING-SAVES**
- B00 in existing saves: **NO-IN-EXISTING-SAVES**
- initial/pending name sources: **PARTIAL**

Open proof obligations are now concentrated on:

1. complete writers/elements of `object+0x154` (initial-name source);
2. complete writers and restore effects for `ScriptObject+0x114`;
3. runtime acceptance/behavior only if static closure remains insufficient.

