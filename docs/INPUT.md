# Input & Controls Reference

**English (US)** | [Português (Brasil)](INPUT.pt-BR.md)

Kotonoha provides a unified cross-device input pipeline across all 15 game ports, the desktop launcher, the unified `KotonohaLauncher`, and the Dear ImGui debug overlay. Every port supports **Mouse**, **Touch**, **Keyboard**, and **Gamepad** simultaneously with hotplug support.

---

## 1. Shared Input Architecture

All input normalization lives in `lib/`:

| Component | Header | Responsibility |
|---|---|---|
| `SDLSession` / `GamepadManager` | `Kotonoha/core/SDL.hpp`, `Kotonoha/core/InputMapper.hpp` | Initializes `SDL_INIT_GAMEPAD`, opens connected controllers, and tracks `SDL_EVENT_GAMEPAD_ADDED` / `SDL_EVENT_GAMEPAD_REMOVED` hotplug with RAII `GamepadPtr` handles. |
| `NormalizeControllerEvent` / `MapControllerEvent` | `Kotonoha/events/ControllerEvent.hpp` | Applies `settings.swapControllerAB` at the engine boundary and maps gamepad buttons/triggers to context-aware keyboard actions (`Menu`, `Dialogue`, `Backlog`). |
| `ControllerNavigation` | `Kotonoha/events/ControllerEvent.hpp` | Converts left-stick axes (`SDL_GAMEPAD_AXIS_LEFTX`, `SDL_GAMEPAD_AXIS_LEFTY`) to virtual D-pad events with hysteresis deadzone (`16000` press / `10000` release) and safe release on focus loss or unplug. |
| `NavRepeater` | `Kotonoha/core/InputMapper.hpp` | Generates directional repeat events (`380 ms` initial delay, `95 ms` repeat interval) for held D-pad and left-stick directions inside `Engine::Iterate()`. |
| `InputMapper` | `Kotonoha/core/InputMapper.hpp` | Maps mouse, keyboard, and gamepad events into semantic `InputAction` values (`Confirm`, `Cancel`, `Menu`, `Backlog`, `AutoToggle`, `SkipHold`, `SkipToggle`, `QuickSave`, `QuickLoad`, `NavUp/Down/Left/Right`, `PageUp/Down`) and provides `InputMapper::touchToLogical()` coordinate conversion. |
| `InputActions` & `TouchPointer` | `Kotonoha/events/InputActions.hpp` | Translates `SDL_EVENT_FINGER_DOWN/MOTION/UP/CANCELED` into logical coordinates, single-tap confirm/click, two-finger tap back, left-edge swipe back, long-press (`550 ms`) menu/back, swipe-up backlog, and interior swipe-right skip toggle. |
| `ImGuiConfigFlags_NavEnableGamepad` | `lib/src/core/Launcher.cpp`, `lib/src/debug/DebugOverlay.cpp` | Enables full keyboard and gamepad navigation across the desktop/unified launcher and the Dear ImGui debug overlay. |

---

## 2. Standard Control Scheme

| Action (`InputAction`) | Mouse | Touch | Keyboard | Gamepad (Default / `swapControllerAB`) |
|---|---|---|---|---|
| **Confirm / Advance** (`Confirm`) | Left click | Single tap | `Enter` / `Space` / `KP_Enter` | East (`B`/Circle) — or South (`A`/Cross) when `swapControllerAB` is enabled |
| **Cancel / Back** (`Cancel`) | Right click | Two-finger tap, left-edge swipe right, or long-press (`550 ms`) | `Esc` / `Backspace` / `AC_Back` | South (`A`/Cross) in menus, or North (`Y`/Triangle) |
| **Pause / In-Game Menu** (`Menu`) | Right click (port-specific) or mobile menu button | Two-finger tap or long-press (`550 ms`) | `Esc` | `Start` |
| **Backlog / History** (`Backlog`) | Wheel up | Swipe up | `PageUp` / `B` | `LB` / `Back` (Select) |
| **Auto Mode** (`AutoToggle`) | Toolbar button | Toolbar tap | `A` | `RB` |
| **Hold Skip** (`SkipHold`) | — | — | `LCtrl` / `RCtrl` | `LT` / `RT` (analog triggers), or South in dialogue |
| **Toggle Skip** (`SkipToggle`) | Toolbar button | Swipe right (interior) | `S` | West (`X`/Square) |
| **Navigate Menus / Choices** (`NavUp/Down/Left/Right`) | Hover + Left click | Tap / Drag | Arrow keys (`Up`/`Down`/`Left`/`Right`) | D-pad or Left Stick (with deadzone & repeat) |
| **Quick Save / Quick Load** (`QuickSave` / `QuickLoad`) | Menu | Menu | `F5` / `F6` (or `F9`) | Via In-Game Menu |
| **Fullscreen Toggle** | — | — | `F11` / `Alt+Enter` | — |

---

## 3. Port & Launcher Support Matrix

All 15 game ports and the unified launcher run through `Engine` / `Launcher` and support all four input modalities:

| Target | Mouse | Touch | Keyboard | Gamepad | Notes |
|---|---|---|---|---|---|
| `KotonohaLauncher` (Desktop & Unified) | Yes | Yes (`TouchPointer`) | Yes (`ImGuiConfigFlags_NavEnableKeyboard`) | Yes (`SDL_INIT_GAMEPAD`, `GamepadManager`, `ImGuiConfigFlags_NavEnableGamepad`) | Full hotplug + ImGui gamepad navigation |
| `example` (`DemoGame`) | Yes | Yes | Yes | Yes | Uses `MapControllerEvent` + shared VN components |
| `amagami` (`AmagamiRuntime`) | Yes | Yes | Yes | Yes | Native `VitaInput` + `MapControllerEvent` + backlog/map navigation |
| `zero-no-tsukaima` (`ZeroRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + frontend/textbox/backlog support |
| `snow-radish-vacation` (`SnowRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + custom scenes & backlog |
| `summer-radish-vacation` (`SummerRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` for Summer, LostM, Imouto, and MEO |
| `meo` (`SummerRuntime` MEO mode) | Yes | Yes | Yes | Yes | Title, scene menu, overlay, and gameplay mapped via `MapControllerEvent` |
| `doki-doki-literature-club` (`DdlcRuntime`) | Yes | Yes | Yes | Yes | Native touch/wheel/keyboard + `MapControllerEvent` |
| `katawa-shoujo` (`DdlcRuntime`) | Yes | Yes | Yes | Yes | Shares `Ddlc::GameRuntime` input pipeline |
| `school-days` (`SchoolDays::Runtime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + `SchoolDaysInputPolicy` + native touch |
| `reallive` (`RealLiveRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + `RealLiveInput` + `TitleInput` + `RealLiveSelection` |
| `kirikiri` (`KirikiriRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + VNX+ overlays + Ctrl/S/A/PageUp/AC_Back |
| `luca-system` (`LucaSystemRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + title/save grid/gallery navigation |
| `pmail` (`PmailRuntime` + `LargePonPonRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` / `InputMapper::toKeyEvent` + full Title/Choice/Map/CG arrow & gamepad nav |
| `n2system` (`N2Runtime`) | Yes | Yes | Yes | Yes | `InputMapper::toKeyEvent` + `capturesInput()` modals + wheel/PageUp backlog + RMB/AC_Back + Ctrl skip |
| `ponscripter` (`PonscripterRuntime`) | Yes | Yes | Yes | Yes | `MapControllerEvent` + PageUp/B backlog + AC_Back/Esc right-click + Ctrl/S/A |
