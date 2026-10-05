# School Days KTRF Desktop v0.1 — Gate 6

Gate 6 is the first full-desktop end-to-end smoke after the KTRF engine cutover.

It does not add routing semantics. It proves that the production executable can be configured, built, launched in `-K` mode, create the first real `Gameplay` from the KTRF-selected ORS, and exit normally.

## Runtime path under test

```text
Kotonoha.exe
  -> -K <school-days-hq.ktnroute> <ors-root>
  -> SchoolDaysKtrfAppController
  -> SchoolDaysKtrfGameplayBridge
  -> SchoolDaysKtrfSession
  -> native KTRF router
  -> resolved .ENG.ORS
  -> Gameplay
  -> Event
  -> Kotonoha_OrsParser
```

The legacy `SchoolDaysRouter + sceneIndex` path is not used when `-K` mode is active.

## Run

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_desktop_gate6_v0_1.ps1
```

By default the runner uses:

```text
ORS root: <repo>\assets
configuration: Release
build: <repo>\build\ktrf\school-days-desktop-gate6-v0.1
```

If vcpkg is not discoverable automatically:

```powershell
powershell -ExecutionPolicy Bypass -File `
  .\tools\ktrf\run_school_days_ktrf_desktop_gate6_v0_1.ps1 `
  -VcpkgRoot "C:\path\to\vcpkg"
```

A different physical ORS root can be supplied with `-AssetsRoot`.

## What the runner does

1. Re-runs Gate 5 unless `-SkipGate5` is supplied.
2. Verifies the production `.ktnroute` exists.
3. Configures the full root CMake project.
4. Builds the real `Kotonoha` desktop target.
5. Launches the real executable with `-K`.
6. Passes the ORS root separately from the media prefix.
7. Adds a trailing path separator to `-p`, because ORS media paths are concatenated directly to that prefix.
8. Preserves `assets/styles.skot` when present.
9. Captures native output to `build\ktrf\school-days-desktop-gate6.log`.
10. Requires a normal process exit.

## Manual smoke checklist

Before closing the window, confirm:

- the game window opens without an immediate crash;
- New Game starts from `00/00-00-A00`;
- image/audio/video paths are not obviously malformed;
- the timeline advances normally;
- if practical, let at least one physical scene transition occur;
- close the window normally.

Gate 6 deliberately keeps those rendering/media observations manual. The routing and transition semantics were already mechanically proven by Gates 1–5 and the native differential suite.

## PASS banner

```text
=========================================================
 SCHOOL DAYS KTRF DESKTOP v0.1 / GATE 6: PASS
=========================================================
```

A PASS means the full desktop executable built, entered KTRF mode, and returned a successful process exit after the manual smoke session.

## Next stage

After Gate 6 passes, desktop routing integration is considered closed. The next stage is the Android boot path:

```text
Android launcher
  -> copied/extracted KTRF + School Days assets
  -> SDL arguments
  -> -K runtime mode
  -> arm64-v8a Kotonoha
  -> first ORS Gameplay
```
