> **Superseded status note (2026-10-04):** this document records the intermediate deep-audit stage when global natural execution was still UNKNOWN. Subsequent save/SLog, replay, runtime, restore, and command-record producer closure resolved the installed-edition natural producer domains. Final status: `NATURAL_EXECUTION = NO` for both targets; see `ORS_ONLY_NATURAL_EXECUTION_CLOSURE_20261004.md`. Externally forced loading remains `POSSIBLE`.

# ORS-only Deep Audit Summary — 2026-10-04

This repository note summarizes the local read-only audit performed against the installed School Days HQ copy.

## Acceptance answers

| Target | Global executable path proven? | What is proven |
|---|---|---|
| `01/01-00-OP2` | **UNKNOWN** | Physical Setsuna-opening ORS exists; absent from all 55 RouteProc tables; no proven `PV/SETUNA-OP` alias. |
| `05/05-9O-B00` | **UNKNOWN** | Physical distinct ORS exists; absent from all 55 RouteProc tables; Route 36 emits A00 and terminates without producing B00. |

## Installed-binary findings

The audit reconstructed the installed RouteProc surface directly and matched 55 tables / 1,857 SceneKeys with zero entry divergence.

The EXE also exposes a generic direct-launch path that can pass a caller-provided script name to the ORS loader without obtaining that name from a RouteProc-table `Next`. Because all name origins reaching this path are not yet exhaustively resolved, absence from RouteProc is insufficient to prove global non-execution.

## Next proof obligation

Close every source of script names reaching the direct-launch/loader path, including replay, extras, saves/config/UI and special modes, or observe those surfaces with a non-invasive runtime trace.

No router node should be added for either target without positive executable evidence.
