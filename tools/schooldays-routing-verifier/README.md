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
