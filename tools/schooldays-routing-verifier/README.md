# School Days Routing Verifier

This directory contains standalone verification helpers for the recovered School Days HQ router.

## Quick mode

`run_quick.ps1` compiles and runs the existing 22-ending witness suite directly against:

- `tests/SchoolDaysFullRouterTest.cpp`
- `src/SchoolDaysRouter.cpp`

It uses the Visual Studio 2022 C++ toolchain discovered through `vswhere.exe`.

Run from PowerShell:

```powershell
cd D:\Dev\Kotonoha
powershell -ExecutionPolicy Bypass -File .\tools\schooldays-routing-verifier\run_quick.ps1
```

Success condition:

```text
QUICK VERIFIER PASS: 22/22 ending witnesses
```

The test validates the expected transition ID at every witness step, not only the final ending.

## Full mode

The exhaustive state-space verifier is the next milestone and is intentionally not claimed as complete yet.

Its required graph-level baseline is documented under:

`docs/school-days-routing-validation/MODEL_CHECKING_PLAN.md`

## Scene inventory audit

Before treating any missing Gameplay destination as a routing-only node, run:

```powershell
cd D:\Dev\Kotonoha
powershell -ExecutionPolicy Bypass -File .\tools\schooldays-routing-verifier\run_scene_inventory_audit.ps1
```

The audit compares every generated router SceneKey against every physical `.ENG.ORS` under `assets/00` through `assets/05`.

It reports:

- router-only nodes;
- ORS-only scripts;
- duplicate SceneKeys;
- rejected/malformed ORS paths;
- per-episode counts;
- RouteProc route/scene coordinates for router-only nodes.

The current formal baseline expects 1,857 router SceneKeys, 1,857 physical ORS SceneKeys, exactly two currently known router-only SceneKeys, and exactly two ORS-only SceneKeys.

A `PASS` is required before we promote the currently known pair into a formally complete routing-only catalog.

JSON output:

`build/routing-verifier/scene-inventory-audit.json`
