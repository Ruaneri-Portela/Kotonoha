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


## 2026-10-03 — Router-vs-ORS completeness audit PASS

The repository-wide scene inventory audit passed on the working checkout.

Observed:

```text
Router SceneKeys:       1857
Physical ORS SceneKeys: 1857

Router-only:
  03/03-B2-A00 (route=12 scene=0 outgoing=2)
  03/03-KB-E00 (route=15 scene=31 outgoing=2)

ORS-only:
  01/01-00-OP2
  05/05-9O-B00

RESULT: PASS
```

Additional checks also passed:

- router SceneKeys unique;
- ORS SceneKeys unique;
- no malformed/rejected ORS paths;
- the router-only set is exactly the expected pair;
- both router-only nodes have outgoing transitions.

This closes the completeness question for no-ORS RouteProc nodes: there are exactly two in the audited normal-New-Game routing model.

The two ORS-only scripts are now tracked separately for later classification because they are physical scripts not represented as normal-New-Game router nodes.


## 2026-10-03 — Opening variant inventory classified

The complete physical `*-OP*.ENG.ORS` inventory was inspected.

Results:

- 33 physical opening ORS files;
- 32 RouteProc-visible `OP1` nodes;
- exactly one physical `OP2`: `01/01-00-OP2`;
- 17 Sekai opening scripts, 14 Kotonoha opening scripts, 2 Setsuna opening scripts.

`01/01-00-OP1` plays `SDHQ_SEKAI` + `SDV01`.
`01/01-00-OP2` plays `SDHQ_SETSUNA` + `SDV03`.

The Setsuna opening also appears in the ordinary RouteProc-visible node `03/03-KA-OP1`, so `OP2` is not synonymous with the Setsuna media variant. The remaining question is how/if the `01-00` alternate opening script is selected outside the recovered 55-table router.

A previous `05-9O-A00 -> 05-9O-B00` reference was also corrected: it was a `PlayVoice` media-path basename collision, not script control flow.

See `OPENING_VARIANT_AUDIT_20261003.md`.


## 2026-10-03 — ORS-only classifier v2 and 9O-B00 overlap test

The corrected classifier separated media-path basename collisions from control-flow references.

Both ORS-only scripts have zero literal control-flow references.

`05/05-9O-B00` is mentioned once from `05/05-9O-A00`, but only inside a `PlayVoice` media path.

A voice-path overlap test found 46 unique B00 voice paths, of which only 1 is also used by A00. This rejects the hypothesis that B00 is merely a duplicate scene whose content was merged into A00.

Further closure now requires proving the selector/loader mechanisms for the alternate `01-00-OP2` opening and the distinct `05-9O-B00` script.



## 2026-10-03 — ORS-only normal-New-Game ambiguity closed

Disassembly and generated-router evidence were combined to resolve the two physical ORS scripts absent from the 55 recovered tables.

`01/01-00-OP2` is a real Setsuna opening script, but the DLL's `PV/SETUNA-OP` handling belongs to the special Route-0 callback/mode path and is not the normal-New-Game episode opening graph.

`05/05-9O-B00` is distinct retained content, but Route 36 contains only `05/05-9O-A00`. Its sole incoming transition (`t1850`) registers Ending 4 and enters Route 36 / Scene 0; `05/05-9O-A00` then terminates through `t1936` / callback_38. No normal-New-Game path can enter B00.

The ORS-only discrepancy is therefore closed for the normal-New-Game router scope without inventing nodes.



## 2026-10-04 — Installed-game ORS-only deep audit: 83/83 checks PASS, global status UNKNOWN

A read-only audit was completed against the installed School Days HQ executable, RouteProc, SysMenu and all installed GPK indexes.

Key result:

- 55 RouteProc tables / 1,857 SceneKeys were reconstructed directly from the installed DLL with zero entry divergence.
- `01/01-00-OP2` and `05/05-9O-B00` are confirmed absent from those tables.
- The EXE has a generic direct-launch path that can feed caller-provided script names into the ORS loader without a table-driven `Next`.
- Complete provenance of all names reaching that path is not yet closed.
- Consequently, the correct acceptance answer for both targets is **UNKNOWN globally**, not NO.
- The previous documentation wording that implied full reachability closure was narrowed to RouteProc-table closure only.

The next decisive phase is either full static dataflow over all direct-launch callers/name sources or a non-invasive runtime trace over the loader/direct-launch/RouteProc/callback surfaces.


## 2026-10-04 — SLog format and save -> launcher chain confirmed

A focused audit decoded all 22 present `SaveFile*.DAT` SLog slot saves to the final byte under the observed grammar.

The first type-1 name is read as a length-prefixed XOR-transformed UTF-16LE string and flows unchanged through:

`0x00433BA9 -> 0x0042A760 -> 0x0042A7AC -> 0x00430D20`.

Across the current save set:

- 22 type-1 launch names decoded;
- 490 type-3 and 876 type-4 records decoded;
- 1,388 structural SceneKey names total;
- zero OP2/B00 target matches;
- `GlobalFlag.DAT` parsed completely with zero OP2/B00 matches.

The save surface is therefore closed for the **existing save files**, but not globally for all possible future or restored states. Remaining static closure work is focused on `object+0x154` and `ScriptObject+0x114`.

Validation reported 146/146 PASS with no original file modification.

