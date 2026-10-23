# Routing Closure Roadmap

## A — DEV recovery instrumentation — COMPLETE

Deliverables: compile-time DEV gate, `-k`, causal checkpoint `sd-ep3-r4-b00`, `DumpState()`, F6 dump, standalone checkpoint test, documentation.

Exit condition satisfied on Android: checkpoint restored `02/02-2K-B00` and the router continued through expected transition `t263 -> 02/02-2K-C00`.

## B — quick regression suite

Formalize the existing 22-ending witness test as the fast regression command.

Exit condition: 22/22 witnesses pass with all expected transition IDs.

## C — exhaustive executable verification

Deliverable: `SchoolDaysRoutingVerifier --full`.

Exit condition: 22 endings, 1,856 reachable SceneKeys, 2,458 expected reachable transitions, no unexpected unresolved state, and all six known dead transitions still unreachable.

## D — engine/platform integration

### Windows — PASS for normal New Game

Native Windows validation with the full physical ORS set is now complete for one real interactive normal-New-Game path:

- EP1 -> EP2 -> EP3 -> EP4 -> EP5 -> EP6;
- real choices / feeling changes / cross-route state changes;
- `03/03-KB-E00` routing-only dispatcher: PASS;
- `03/03-B2-A00` routing-only dispatcher: PASS;
- native Ending 16 reached and registered through `t2357`.

The two proven routing-only nodes are therefore **2/2 PASS** in the full Windows engine integration.

See `WINDOWS_ENGINE_INTEGRATION_20261004.md`.

### Android ARM64 — PENDING REVALIDATION

The original Android incident exposed `03/03-KB-E00` before the routing-only fix existed. The current fix still needs a device rebuild/revalidation when the Android test device is available.

Minimum Android PASS target:

```text
next t917 -> 03/03-KB-E00
routing-only node 03/03-KB-E00 (no ORS); resolving immediately
next t907 -> 03/03-KB-G00
```

or `t908 -> 03/03-KB-F00` when `001 > 002`.

## E — post-ending / callback integration — ACTIVE

The normal-New-Game route graph correctly reaches terminal endings, but the engine currently handles `NextKind::Terminal` by returning `SDL_APP_SUCCESS`, which closes Kotonoha.

Next task:

1. recover the observable semantics of `callback_38` / post-ending handling;
2. separate terminal graph semantics from process termination;
3. implement the correct post-ending engine state transition;
4. validate with a targeted ending checkpoint and a real ending presentation.

Do not change the already-certified narrative transition graph merely to solve post-ending UI behavior.

## F — targeted mechanism smoke tests

Use checkpoints for timeout routing, same-episode route changes, convergences, BS/history predicates, 99x flags, global/callback behavior where reachable, and different ending families.

## G — certified graph generation

Generate full scene graph, episode map, route-table map, decision map, ending map, dead-branch overlay and machine-readable graph directly from verifier results.

## H — final report

Create `SCHOOLDAYS_ROUTING_VALIDATION_FINAL.md` with router commit, verifier commit, toolchain, exact baselines, witness result, exhaustive result, Windows integration result, Android result/status, known exclusions and hashes of generated reports.

## I — KTRF

Only after the final report is frozen: specify KTRF v0.001, compile the recovered model to `.ktroute`, implement the read-only runtime, run differential verification, require zero divergences, then consider migration from generated `.inc` data.
