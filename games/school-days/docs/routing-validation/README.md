# School Days HQ Routing Validation Lab

This directory is the dedicated engineering record for validating the recovered School Days HQ normal-New-Game router in Kotonoha.

The goal is not merely to make one playthrough work. The goal is to close the routing map with reproducible evidence at four layers: recovered model correctness, C++ router correctness, engine integration correctness, and Android/device execution correctness.

## Baseline

- Router baseline branch: `research/school-days-full-router-20261003`
- Baseline commit: `a81ed02d0171be1e8bb13d9d4b17658e66aaac95`
- Validation branch: `research/school-days-routing-validation-lab-20261003`
- 55 route tables
- 1,857 SceneKeys
- 1,856 / 1,857 SceneKeys causally reachable from reset
- 2,458 reachable forward transitions
- 22 / 22 native endings causally reachable
- 42,404,396 projected reachable states in the formal exploration
- 6 known dead forward branches

Known unreachable normal SceneKey: `04/04-C0-A05`.

## Real-device evidence reached so far

- full router startup at `00/00-00-A00`
- real prompt choices reaching `AcceptChoice()`
- feeling mutations
- episode transition `00 -> 01` through `t25`
- same-episode change from Route 1 to Route 2
- episode transition `01 -> 02` through `t240`
- continued execution inside Route 4 through `02/02-2K-B00`

This device evidence validates integration. It does not replace exhaustive logical verification.

## DEV instrumentation

The lab adds development-only helpers behind `KOTONOHA_DEV_CHECKPOINTS`.

Current checkpoint: `sd-ep3-r4-b00` -> `02/02-2K-B00`.

The checkpoint restores causal router state instead of only forcing the Gameplay index.

State dump hotkey: `F6`.

## Closure sequence

1. DEV checkpoint + deterministic `DumpState()`.
2. Quick 22-ending witness regression.
3. Exhaustive C++ router verifier.
4. One complete Android path from New Game to ending.
5. Targeted device smoke tests for distinct routing mechanisms.
6. Generate the final verified graph from executable transitions.
7. Freeze the final validation report.
8. Only then use the certified router as the oracle for KTRF / `.ktroute`.

Every routing claim in this directory should identify whether it is model evidence, standalone C++ evidence, engine integration evidence, or real-device evidence.


## ORS-only inventory closure

The router/physical-script count mismatch is now fully classified for the installed edition.

- Router-only logical dispatchers: `03/03-B2-A00`, `03/03-KB-E00`.
- Physical ORS not naturally selected: `01/01-00-OP2`, `05/05-9O-B00`.
- `NATURAL_EXECUTION = NO` for both ORS-only scripts after closing startup, RouteProc, replay/SysMenu, save/SLog, restore, pending-name, exit, command-record, and direct-launch producer domains.
- Generic externally forced loading remains possible and is outside the natural routing model.

The next Android task is to rebuild and revalidate the zero-Gameplay handling for the two router-only dispatchers, beginning with the previously observed Route 15 incident at `t917 -> 03/03-KB-E00`.
