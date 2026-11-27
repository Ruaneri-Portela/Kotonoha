# Kotonoha Manager (Avalonia)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

A desktop game manager built with Avalonia 12 / .NET 10. It follows the
interface patterns of the Android and iOS/macOS launchers: a searchable game
library, a detail pane, per-game parameters, global settings and native
in-process launching through the same C ABI.

## UI map

| Surface | Android / iOS equivalent |
|---|---|
| Header: brand, search, Rescan, + Folder, + ISO/BIN, + ZIP, Settings | `LauncherActivity` toolbar / `KotonohaLauncherView` header |
| Library cards: title, engine badge, FOLDER/DISC badge, OVERLAY badge, Play | `item_game_card.xml` / `KotonohaGameItem` |
| Detail pane: path, save directory, Play / Parameters… | iOS/macOS split detail pane |
| Parameters dialog: overlays, localization, School Days route, saves, graphics | `dialog_game_parameters.xml` / `KotonohaGameParametersSheet` |
| Settings dialog: library roots, isolated saves, graphics defaults | `dialog_graphics_settings.xml` / `MacSettingsSheet` |

The visual theme is **WinUI 3 / Fluent2** (Avalonia 12) with dark Windows 11
tokens centralized in `Styles/Theme.axaml` (surface `#2C2C2C`, card `#323232`,
accent `#0F6CBD`, text `#FFFFFF`, muted `#9E9E9E`, radius 8). Renderer, upscaler
and preset are `ComboBox` selectors; the scale factor is a `Slider` (1–4×).
The application icon comes from `design/generated/generic/icon.ico`.
Language can be switched at runtime between **Português** and **English** via
the header selector; all UI strings come from `Services/Localization.cs`.

## Native bridge

`KotonohaLauncher.cs` is linked from `launcher/bridge/dotnet/`, so the same
C ABI facade used by the other frontends is compiled into this app.
`NativeLibraryResolver` locates `KotonohaLauncherCore.dll` (or `.dylib`/`.so`)
from `KOTONOHA_NATIVE_DIR`, the Visual Studio output
`buildsystems/visualstudio/out/x64/Release/`, or a CMake build output.
No native library is committed to the repository.

`KotonohaLauncherRunDll` is the Windows equivalent of a fork: the game starts
in a `rundll32.exe` host on its own main thread (SDL requirement). The manager
receives the process PID, captures stdout/stderr into the detail pane, keeps the
game card green while the process is alive and shows a transient notification
when it exits. Per-game paths (overlay, translation, subtitle style, documents,
route, scripts) use native file/folder pickers.

## Build and run

```sh
dotnet build launcher/desktop/KotonohaManager/KotonohaManager.csproj -c Release
```

For VS Code, `Properties/launchSettings.json` defines two profiles with
`KOTONOHA_NATIVE_DIR` pointing to the Visual Studio native output
(`buildsystems/visualstudio/out/x64/<Configuration>`), which contains
`KotonohaLauncherCore.dll` and the vcpkg runtime DLLs. The local
`.vscode/launch.json` only selects a profile via `launchSettingsProfile`.
The app sets its working directory to the dotnet output, where `assets/` is
copied.

Set `KOTONOHA_NATIVE_DIR` to a directory containing `KotonohaLauncherCore`
and its runtime dependencies before starting the app:

```powershell
$env:KOTONOHA_NATIVE_DIR = 'E:\Kotonoha\buildsystems\visualstudio\out\x64\Release'
dotnet run --project launcher\desktop\KotonohaManager\KotonohaManager.csproj -c Release
```

On first run the library root defaults to `Documents/Kotonoha/games`
(`MainWindowViewModel.DefaultGamesRoot`). Library roots, save isolation and
graphics defaults are persisted as JSON under
`%APPDATA%/Kotonoha/KotonohaManager/settings.json` — outside the read-only
virtual filesystem.

ZIP import is path-traversal safe and enforces the same 10,000-entry / 64 GiB
limits as the Android Storage Access Framework importer.

## Verification

```sh
dotnet build launcher/desktop/KotonohaManager/KotonohaManager.csproj -c Release --no-restore
```

The scan loads a complete, deduplicated native catalog in the background and
updates the library on the UI thread. Existing running game cards are retained.
The default depth is 0 (root and direct children).
