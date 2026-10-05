# School Days KTRF Adapter v0.1 — Gate 5 Engine Cutover

Gate 5 is the runtime cutover point from the legacy School Days routing path to the native KTRF stack.

## Runtime split

Generic/legacy launch remains unchanged:

```text
SDL_AppIterate
  -> Kotonoha::Main
```

Explicit School Days KTRF launch uses:

```text
-K <route.ktnroute> <ors-root>

SDL_AppIterate
  -> SchoolDaysKtrfAppController
     -> SchoolDaysKtrfGameplayBridge
        -> SchoolDaysKtrfSession
           -> native KTRF router
        -> resolved physical .ENG.ORS
     -> Gameplay
```

The two paths are mutually exclusive per frame.

## Why the cutover is in Main.cpp

The SDL callback layer is the narrowest safe point that already owns the engine lifetime and calls `Kotonoha::Main()`.

Keeping the cutover there means:

- `src/Kotonoha.cpp` remains untouched;
- the generic ORS loader remains intact;
- `SchoolDaysRouter + sceneIndex` never execute in KTRF mode;
- rollback is simply launching without `-K`.

## Gameplay ownership

The KTRF controller creates `Gameplay(resolved_ors_path, gameContext)` on demand.

Destruction is routed through:

```text
Kotonoha::DeleteGameplay()
```

instead of raw `delete`, so process-pool tasks associated with the old gameplay are removed before the object is destroyed.

The controller is destroyed before the Kotonoha engine during `SDL_AppQuit`, keeping that cleanup path valid.

## Choices and handoffs

Gameplay prompt results are committed through the KTRF bridge.

`callback_38` and terminal transitions remain pending handoffs and keep the current Gameplay alive. Development builds can acknowledge non-terminal handoffs with F5.

Terminal/title behavior remains intentionally outside this routing gate.

## Launch

```powershell
Kotonoha.exe -K build\ktrf\school-days-hq.ktnroute D:\Dev\Kotonoha\assets
```

Do not combine `-K` with `-l`.

## Gate

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_adapter_gate5_v0_1.ps1
```

The gate reruns Gate 4 and audits that:

- `-K` is explicit;
- KTRF and legacy main loops are exclusive;
- KTRF mode rejects `-l` preload mode;
- Gameplay destruction uses `DeleteGameplay()`;
- the KTRF controller has no dependency on `SchoolDaysRouter` or `sceneIndex`;
- the legacy `Kotonoha::Main()` path still exists.

Expected final banner:

```text
=========================================================
 SCHOOL DAYS KTRF ADAPTER v0.1 / GATE 5: PASS
=========================================================
```

After Gate 5 passes, routing integration is considered closed. The next milestone is desktop end-to-end runtime smoke followed by Android boot/integration.
