# School Days KTRF Adapter v0.1 — Gate 4: Gameplay Bridge

Gate 4 inserts the engine-facing ownership layer between the already-certified
`SchoolDaysKtrfSession` and `Gameplay`.

## Scope

The new `SchoolDaysKtrfGameplayBridge`:

- owns one `SchoolDaysKtrfSession`;
- owns exactly one active `Gameplay*` through an injected factory;
- creates the New Game `Gameplay` from the ORS path resolved by KTRF;
- preserves deferred-choice blocking;
- advances KTRF and swaps to the resolved ORS scene;
- creates the next Gameplay before destroying the previous one;
- preserves `callback_38` as a pending episode handoff;
- preserves terminal transitions as a pending acknowledgement;
- releases every owned Gameplay on `Close()`.

The production factory is `MakeSchoolDaysRealGameplayFactory()`. It calls:

```cpp
new Gameplay(orsPath, gameContext)
```

and deletes the Gameplay when the bridge replaces or closes it.

The gate uses a fake Gameplay factory so it can validate ownership and routing
without creating a multimedia window/audio stack. This isolates routing/session
integration from SDL/FFmpeg rendering while exercising the real `.ktnroute` and
physical ORS asset resolver.

## Gate assertions

The harness requires:

- New Game starts at `00/00-00-A00`;
- exactly one Gameplay is live after open;
- at least 8 real transitions and 8 Gameplay scene swaps;
- at least one real School Days choice;
- an unresolved choice blocks before commit and performs no Gameplay swap;
- every normal scene advance creates one next Gameplay and destroys one old Gameplay;
- callback_38, when encountered, does not swap before acknowledgement;
- terminal acknowledgement does not destroy the active Gameplay;
- `Close()` leaves zero live Gameplays and equal create/destroy totals.

## Run

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate4_v0_1.ps1
```

Optional asset root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate4_v0_1.ps1 `
  -AssetsRoot "D:\path\to\assets"
```

## Deliberate boundary

Gate 4 validates the real Gameplay ownership bridge, but does not yet replace
`Kotonoha::Main()`'s legacy `SchoolDaysRouter + sceneIndex` branch. The next gate
is the small host-loop cutover: `Kotonoha::Main()` delegates School Days scene
advancement to this already-tested bridge, while generic ORS loading remains
unchanged.
