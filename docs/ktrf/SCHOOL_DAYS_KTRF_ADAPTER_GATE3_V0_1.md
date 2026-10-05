# School Days KTRF Adapter v0.1 — Gate 3 Session Bridge

Gate 3 adds the engine-facing C++ ownership layer above the certified native KTRF stack.

`SchoolDaysKtrfSession` owns `Kotonoha_KtrfDocument`, binds the School Days adapter, stores the ORS assets root, resets New Game, commits deferred choices, advances through routing-only dispatchers, probes every returned `.ENG.ORS`, and collects hooks/endings produced by each step.

The generic KTRF reader/runtime/router are unchanged.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate3_v0_1.ps1
```

Optional assets root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate3_v0_1.ps1 `
  -AssetsRoot "D:\path\to\assets"
```

The harness regenerates the frozen IR and production `.ktnroute`, opens the New Game ORS, walks up to 64 real routing transitions, verifies that an uncommitted deferred choice blocks routing, commits a legal choice, and requires multiple physical ORS resolutions.

Success ends with:

```text
SCHOOL DAYS KTRF SESSION GATE 3 PASS
SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 3: PASS
```

After this gate passes, the application loop can bind `SchoolDaysKtrfSession::ResetNewGame/Advance` directly to `Gameplay(path) -> Event -> Kotonoha_OrsParser` without changing routing semantics.
