# Implementation plan — native WinUI 3 launcher for Visual Studio

**English (US)** | [Português (Brasil)](WINUI_NATIVE_APP_PLAN.pt-BR.md)

Status: **in implementation** — Phase 1 (bridge) and the Phase 2 skeleton are
landed; the WinUI XAML layer still requires the Windows App SDK toolchain.
This plan turns the WinUI feasibility
study into actionable steps. Related reading:
[Visual Studio targets](VISUAL_STUDIO_TARGETS.md),
[Visual Studio guide](../buildsystems/visualstudio/README.md),
[engine settings](ENGINE_SETTINGS.md), [asset overlays](ASSET_OVERLAYS.md).

## 1. Goal and non-goals

Build a native WinUI 3 application for Windows that acts as a **game library and
launch hub** for the Kotonoha ports, following the same role as the Android
native app and the macOS/iOS native launcher.

It must:

- manage a **game library** (multiple roots: folders, ISO/BIN);
- follow the Kotonoha visual identity, adapted to **Fluent/WinUI**;
- expose **per-game** overlay, translation, route file and forced subtitles;
- let the user choose **startup options**: window/monitor, renderer backend,
  upscaler, volume, fonts and colours;
- build inside `buildsystems/visualstudio/` for **x64 and ARM64** alongside the
  existing 19 projects (plus the new `KotonohaLauncherCore` static library).

Non-goals for the first iteration:

- hosting SDL3 inside `SwapChainPanel` (see §3);
- changing `KOTONOHA_SDL_CALLBACK_APP` or any game port's `main`;
- changing the legacy `game-params.txt` field layout;
- redistributing commercial game assets.

## 2. Architecture decision (ADR)

**WinUI is a presenter. The engine stays in separate processes.**

```
WinUI app
 ├─ KotonohaLauncherScanRootsJson(roots)      -> game library
 ├─ KotonohaLauncherLoadGameParamsJson(...)   -> per-game params
 ├─ KotonohaLauncherSaveGameParams(...)       -> per-game params
 ├─ KotonohaLauncherSaveProfileEngineSettingsJson(...) -> settings.json
 └─ CreateProcessW("<port>.exe --no-launcher ...")
                                              -> SDL_AppInit -> Engine
```

Rationale: it mirrors Android (`LauncherActivity.java` passes `kotonoha_args`
to the game activity), preserves the SDL callback invariant, and reuses all
detection/validation logic already in the C ABI bridge. `SwapChainPanel` would
require `CreateSwapChainForComposition` + `ISwapChainPanelNative`, a separate
`DesktopWindowXamlSource` thread, and would conflict with SDL's own HWND and
XAML input handling.

## 3. Prerequisites (user / CI)

- Visual Studio with **Desktop development with C++**, **Windows SDK**, and the
  **C++/WinUI 3 (Windows App SDK)** component; Windows App SDK redistributable
  or self-contained deployment.
- Developer Mode + test certificate whose `Publisher` matches the MSIX `Identity`
  for sideloading, or Store signing for release.
- Keep the existing vcpkg trees for `x64-windows` and `arm64-windows`.
- `git submodule update --init --recursive` (SDL3, SDL3_ttf, imgui).

## 4. Phases

### Phase 0 — Baseline (no code)

Record current green state: `python3 tools/ValidateBuildMetadata.py`,
`generate_visualstudio.py --check`, MSBuild Debug/x64 build of `KotonohaExample`
and `DokiDoki`. Do not proceed until these pass.

### Phase 1 — Missing C ABI bridge function (C++ only)

Goal: let the UI write **partial** engine settings without duplicating the
`settings.json` schema or `EngineSettings::validate()`.

- Add to `launcher/include/KotonohaLauncher/LauncherBridge.h`:
  `KotonohaLauncherSaveProfileEngineSettingsJson(const char* profileId, const char* partialJson)`
  returning a JSON status string (`ok` / `error`).
- Implement in `launcher/src/LauncherBridge.cpp`: resolve the profile directory
  via `Kotonoha::documentsDirectory()`, call `EngineSettings::load(path, defaults)`,
  merge only the supplied fields, run `validate()`, write with
  `Kotonoha::writeTextFileAtomically`. Reuse `resolvedOverlayPath()` behaviour.
- Add a native test covering: partial merge, invalid range rejection, named
  overlay resolution, and profile isolation (`SplitSavesByGame`).
- Wire the new export into `lib/tests`/launcher test target if the bridge test
  harness is shared; otherwise add a small dedicated test executable.

Acceptance: the new function is callable from C, round-trips a partial JSON,
and invalid values are rejected by the existing validator (not by the UI).

### Phase 2 — WinUI project skeleton inside the VS solution

- Add `buildsystems/visualstudio/WinUI/` with an **artisanal** `.vcxproj` and
  MSIX packaging files (`Package.appxmanifest`, certificate template).
  Override `SubSystem` to `Windows`; C++/WinRT is C++17-compatible, matching
  `<LanguageStandard>stdcpp17</LanguageStandard>`.
- Register it in `buildsystems/visualstudio/ProjectManifest.json` under
  `native_projects` (NOT `targets`) with `platforms: ["x64", "ARM64"]`. The
  generator already maps unsupported Win32 solution configs to `ActiveCfg`
  without `Build.0` and never rewrites imported projects.
- Keep `generate_visualstudio.py --check` and `ValidateBuildMetadata.py` green;
  add assertions for the new imported project and the two supported platforms.
- Verify MSIX unpackaged bootstrap (`MddBootstrapInitialize`) or packaged
  deployment path; declare `rescap:runFullTrust` for arbitrary folder/ISO reads.

Acceptance: `Kotonoha.sln` lists the WinUI project; x64 and ARM64 build; Win32
is skipped rather than failing.

### Phase 3 — State model and persistence

Create C++/WinRT view models mirroring the already-proven macOS models
(`MacLauncherStore`, `MacGameItem`, `MacGameParams`) instead of inventing new
schemas:

```
LauncherState
 ├─ LibraryRoots, Games, IsScanning, StatusMessage
 ├─ GlobalLaunchOptions (renderer/upscaler/volume/fonts/colours defaults)
 └─ ThemeState (AppTheme, AccentColor, FontFamily, UiScale)
GameItem
 ├─ fields from KotonohaLauncherScanRootsJson
 └─ Params: overlay, translation, subtitleStyle, ktrf/scripts, startScenario
ProfileSettingsState
 └─ fields from EngineSettings/settings.json (window, display, audio, text)
```

Persist in `%APPDATA%\Kotonoha\me.hirameki.kotonoha\`:

| File | Owner | Content |
|---|---|---|
| `library-roots.json` | WinUI | multiple library roots |
| `launcher-options.json` | WinUI | global launch options + theme |
| `game-params.txt` | **bridge only** | `GameLaunchParameters` |
| `settings.json` | **bridge only** | `EngineSettings` for hub and per-profile |

Use `FolderPicker` + `Windows.Storage.AccessCache` for durable folder access.
Never write `game-params.txt` or `settings.json` directly from C#.

Acceptance: roots, per-game params and global options survive restart; engine
settings written by the UI are accepted without manual edits.

### Phase 4 — UI implementation (Fluent, adapted to Kotonoha)

- **Library page**: `NavigationView`, multi-root add/remove, scan progress
  (background `Task.Run` + `DispatcherQueue.TryEnqueue`), game grid using
  title/engine badge from `DiscoveredGameEntry`.
- **Game detail page**: overlay, translation, `--subtitle-style`, and
  Ktrf/route fields. Show route validation only for `IsSchoolDays`, using
  `KotonohaLauncherValidateRoute` and `KotonohaLauncherDefaultSchoolDaysRoute`.
  Forced subtitles toggle writes `EngineSettings::enableForcedSubtitles`, not
  `GameLaunchParameters`.
- **Launch options**: renderer (`direct3d11`, `direct3d12`, `opengl`,
  `software`), upscaler/preset/factor from `KotonohaLauncherUpscalePresetsJson`
  (no hardcoded list), volume, fonts, text colours, window/monitor.
- **Theme**: `ThemeDictionaries` for `Default`/`Light`/`HighContrast` using the
  canonical palette (`#0D142E`, `#182449`, `#2C3E9A`, `#6A5BE7`, `#C86BD9`,
  `#00D6FF`, `#FF8FBD`, `#B9B5D4`, `#39315E`), Mica/Acrylic, rounded corners,
  `Segoe UI Variable`. Do not copy Apple's `glassEffect`.

Acceptance: full flow reachable by keyboard and touch; HighContrast readable;
all strings localised or at least passed through the existing translation path.

### Phase 5 — Launch integration

- `CreateProcessW` with Unicode args: `--no-launcher`, `--library`,
  `--config <profile>\settings.json`, plus overlay/translation/ktrf/scripts/
  renderer/upscaler flags as needed.
- Set `KOTONOHA_USER_DATA_DIR` in the child environment block (matching the
  launcher's `_wputenv_s` usage in `launcher/src/main.cpp`).
- Treat `settings.json` as engine-owned while a game runs; UI edits happen
  before launch.
- Keep an optional in-process hub variant behind IPC (named pipe) only if
  parity with macOS is required later.

Acceptance: launching a detected game starts the correct `.exe` with the chosen
options; saves land in the expected profile directory; closing the game leaves
the UI responsive.

### Phase 6 — Validation and documentation

- Extend `tools/ValidateBuildMetadata.py` with the imported WinUI project
  checks; keep the existing CMake/Xcode/VS outputs unchanged.
- Add manual smoke checklist to `docs/BUILD_VALIDATION.md` (and `.pt-BR.md`).
- Update `docs/VISUAL_STUDIO_TARGETS.md` (+ `.pt-BR.md`) and the VS README pair
  with the WinUI target and its x64/ARM64-only support.
- Run `python3 tools/ValidateBuildMetadata.py` and the native tests on both
  architectures before merging.

## 5. Files touched (planned)

| Path | Change |
|---|---|
| `launcher/include/KotonohaLauncher/LauncherBridge.h` | new `SaveProfileEngineSettingsJson` export |
| `launcher/src/LauncherBridge.cpp` | implementation + validation reuse |
| `buildsystems/visualstudio/WinUI/**` | new artisanal project, XAML, MSIX |
| `buildsystems/visualstudio/ProjectManifest.json` | `native_projects` entry (x64/ARM64) |
| `buildsystems/visualstudio/Scripts/generate_visualstudio.py` | guard imported project (already supports it) |
| `tools/ValidateBuildMetadata.py` | assertions for WinUI project |
| `docs/WINUI_NATIVE_APP_PLAN.md` / `.pt-BR.md` | this plan |
| `docs/VISUAL_STUDIO_TARGETS.md` / `.pt-BR.md` | matrix update |
| `docs/BUILD_VALIDATION.md` / `.pt-BR.md` | smoke checklist |
| VS README pair | link to new target |

## 6. Risks and mitigations

| Risk | Mitigation |
|---|---|
| MSIX virtualises `%APPDATA%` | verify `documentsDirectory()` in packaged mode; `runFullTrust`; picker + access cache |
| WinAppSDK runtime init | packaged Deployment API or `MddBootstrapInitialize` for unpackaged; prefer self-contained |
| x86 is second-class for WinAppSDK | support x64/ARM64 first; never fail the whole solution on Win32 |
| SDL HWND vs XAML input | keep engine out-of-process (ADR), do not use `SwapChainPanel` |
| Concurrency on `settings.json` | UI edits before launch; engine file authoritative while running |
| Theme drift between platforms | share canonical palette, adapt materials per platform |
| Duplicate validation rules | all writes go through the C ABI bridge |

## 7. Definition of done

- WinUI project builds in x64 and ARM64; Win32 is skipped cleanly.
- Library multi-root, per-game overlay/translation/route/forced-subtitles and
  startup options are editable and persisted.
- Launching a game from the UI uses the existing bridge + CLI contract; no
  changes to `KOTONOHA_SDL_CALLBACK_APP` or game ports.
- `ValidateBuildMetadata.py` and `generate_visualstudio.py --check` pass, and
  the smoke checklist is executed on x64 and ARM64.
- Documentation pairs are updated together.
