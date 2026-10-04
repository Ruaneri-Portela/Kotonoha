# Routing Validation Worklog

This is a chronological engineering journal. New validation milestones should append entries rather than rewriting history.

## 2026-10-03 — Full router baseline

- baseline commit: `a81ed02d0171be1e8bb13d9d4b17658e66aaac95`
- generated full normal-New-Game router integrated
- 22 known ending witnesses pass in standalone C++

## 2026-10-03 — Android build integration

- Windows -> Android ARM64 build restored locally
- full router compiled into Android build
- APK packaging initially failed because original School Days media exceeded ZIP32 4 GiB limits
- media was moved out of APK packaging
- Android app then built successfully

## 2026-10-03 — Android media access correction

- media was first copied under app-specific external storage by `adb push`
- shell-owned files were visible to adb but inaccessible to the app process
- media was moved into the app private files directory for the DEV test
- visual playback resumed

## 2026-10-03 — Real device routing evidence

- `00 -> 01` confirmed through `t25`
- Route 1 -> Route 2 confirmed inside episode prefix `01`
- `01 -> 02` confirmed through `t240`
- Route 4 continued through `t262` and `t393` to `02/02-2K-B00`

## 2026-10-03 — Process loss and checkpoint recovery

The Android process was accidentally left, losing the live router state. The saved trace was sufficient to reconstruct the causal state at `02/02-2K-B00`.

Recovered checkpoint name: `sd-ep3-r4-b00`.

## 2026-10-03 — Validation lab branch

Created dedicated branch:

`research/school-days-routing-validation-lab-20261003`

Added:

- compile-time DEV checkpoint gate
- `-k` checkpoint argument
- causal checkpoint restore
- deterministic `DumpState()`
- F6 manual state dump
- standalone checkpoint regression test
- dedicated routing-validation documentation

## 2026-10-03 — DEV checkpoint validated on Android

Real-device checkpoint restoration passed.

Observed:

```text
[KTN-DEV] checkpoint requested: sd-ep3-r4-b00
[KTN-ROUTER] DEV checkpoint restored: sd-ep3-r4-b00 scene=02/02-2K-B00
[KTN-DEV] checkpoint applied: sd-ep3-r4-b00 gameplay_index=195
```

The restored route then advanced through the expected first transition:

```text
ROUTE=4
SCENE=4
next t263 -> 02/02-2K-C00
```

Milestone A exit condition is therefore satisfied on the real Android integration.

## 2026-10-03 — Quick verifier runner added

Added `tools/schooldays-routing-verifier/run_quick.ps1`.

The runner compiles the real `SchoolDaysRouter.cpp` and executes the existing 22-ending witness suite through Visual Studio 2022. It requires 22 `PASS` lines before reporting success.

## Next entry

Run the quick verifier locally and record its result. In parallel, continue the Android path toward `02 -> 03 -> 04 -> 05 -> ending`, capturing state dumps at useful boundaries.

## 2026-10-03 — Android episode crossing 02 -> 03 confirmed

The resumed DEV-checkpoint session reached the next episode boundary on the real Android runtime.

Observed:

```text
ROUTE=15
SCENE=0
next t322 -> 03/03-KB-A00
```

This confirms executable integration across the episode-prefix transition `02 -> 03` and entry into Route 15.

A full state dump at this boundary is requested so the state can be preserved as the next causal DEV checkpoint candidate.


## 2026-10-03 — No-ORS dispatcher bug exposed by real device path

The Android path reached:

```text
BS03KBE00=39
ROUTE=15
SCENE=31
next t917 -> 03/03-KB-E00
destination not loaded: 03/03-KB-E00
```

The app closed because the engine returned failure for an unloaded destination.

Cross-check against the recovered scene mapping confirmed that `03/03-KB-E00` is intentionally one of exactly two RouteProc nodes with no physical ORS. The other is `03/03-B2-A00`.

These are routing-only dispatcher nodes and must be resolved immediately by RouteProc logic rather than passed to Gameplay.

The validation branch now exposes `IsCurrentSceneRoutingOnly()` and auto-resolves only these explicitly known virtual nodes. Ordinary missing destinations remain hard failures.

See `ROUTING_ONLY_NODES.md`.


## 2026-10-03 — Router-vs-ORS completeness audit added

A repository-wide scene inventory audit was added before claiming that the two currently known no-ORS dispatcher nodes are the complete set.

Tool:

`tools/schooldays-routing-verifier/audit_scene_inventory.py`

Runner:

`tools/schooldays-routing-verifier/run_scene_inventory_audit.ps1`

The audit compares all generated router SceneKeys against every physical `.ENG.ORS` under episodes `00..05`, reports both set differences, duplicates and malformed paths, and emits a JSON report.

No claim that the routing-only catalog is complete is accepted until this audit passes on the working checkout.

