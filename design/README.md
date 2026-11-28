# Kotonoha Engine - Brand & Visual Style Guide v2.0

**English (US)** | [Português (Brasil)](README.pt-BR.md)

This README and `tools/generate_brand_assets.py` are the permanent reference for the Kotonoha visual identity. Future changes must preserve the canonical K monogram coordinates and legibility; preview mockups are not build inputs.

## Invariant K Monogram

White geometric three-stroke K with rounded stroke caps:
- Vertical stem: `(0.375, 0.310)` to `(0.375, 0.690)`
- Upper diagonal: `(0.615, 0.310)` to `(0.385, 0.510)`
- Lower diagonal: `(0.395, 0.500)` to `(0.635, 0.690)`
- Stroke radius: `0.041` on a normalized `1:1` square.

The K monogram always occupies the primary focal zone; sakura blossoms appear only as secondary accents. Its geometry remains identical across WebApp, Windows, Linux, Android, iOS, and macOS even when background materials vary.

## Official Visual Tokens

| Token | Hex | Role |
| --- | --- | --- |
| SplashBlack | `#000000` | Universal startup splash background |
| Navy | `#0D142E` | Deep background |
| Raised | `#182449` | Cards and elevated surfaces |
| Indigo | `#2C3E9A` | Interactive surfaces |
| Violet | `#6A5BE7` | Primary accent, focus, and selection |
| Magenta | `#C86BD9` | Secondary accent and hover |
| Sakura | `#FF8FBD` | Brand ornament accents |
| Cyan | `#00D6FF` | Alternate focus ring and subtle highlight |
| White | `#FFFFFF` | Primary text and monogram |
| Muted | `#B9B5D4` | Secondary text |
| Outline | `#39315E` | Borders and dividers |

Platform icons use a dedicated light background palette defined in `design/tokens.json` under `icon`: `backgroundStart` `#C4DEFF` (ice blue), `backgroundEnd` `#CDBCFC` (lilac), `glow` `#FFFFFF`, `wave` `#EC6AA5`, `monogram` `#FFFFFF`, `monogramContour` `#4A3496`, and `sakura` `#FF8FBD`. The `Navy`/`Raised`/`Indigo` tokens remain active for UI surfaces and are not used as icon backgrounds.

## Components, States, and Layout

Buttons, search fields, text inputs, radio buttons, checkboxes, toggles, sliders, tabs, menus, cards, lists, toolbars, modals, progress bars, and tooltips share the same semantic tokens. Clear visual hierarchy: navy background, raised surfaces, violet primary, magenta hover, visible cyan focus, white text. Error, warning, and success states use dedicated functional colors distinct from brand accents. Spacing scale: `4`, `8`, `16`, `24`; corner radii: controls `8`, cards `12`, modals `20`. Legibility and contrast take priority over decoration; avoid continuous animations and respect reduced-motion preferences.

## Platform Adaptation

- **Generic Windows / Linux / WebApp**: White K monogram with soft indigo contour over the light ice/lilac gradient and subtle sakura blossoms; PNG icon, WinRes `.ico`, and per-app PWA icons. On WebApp, only the strongly lightened central accent glow varies per game while preserving exact proportions.
- **Android**: Material 3 widgets, adaptive icon foreground/background layers, themed/monochrome icon, and native navigation patterns (no simulated Liquid Glass).
- **iOS and macOS**: Native SwiftUI typography and gestures; Liquid Glass on OS releases supporting `glassEffect` and system materials on earlier versions. Never replace native system controls with static bitmaps.
- **Dear ImGui**: Preserves existing navigation workflows with tokenized button, selector, popup, bar, tab, card, and window states across the engine launcher and debug overlay.
- **Third-party game UIs**: Original in-game interfaces of ported titles are never replaced by engine branding.

## Universal Splash

Pure `#000000` background with the centered Kotonoha mark, preserving aspect ratio without landscape backgrounds, decorative frames, or orientation assumptions. The renderer letterboxes any remaining viewport area in pure black. (Note: `school-days` retains its owner-authorized port override in `assets/SPLASH.PNG` and `assets/SchoolDays.ico`.)

## Generation, Distribution, and Source of Truth

The `design/` folder is the single source of truth for Kotonoha brand assets. The generator writes directly to `design/generated/` (tracked in Git) rather than an ephemeral build directory. CMake, Xcode, Android Gradle, WinRes, and the Web shell consume only their platform-specific generated files; the `design/` directory itself is **never** bundled into game packages as a runtime asset folder.

Generate all brand assets with:

```sh
python3 tools/generate_brand_assets.py --kind all
```

For macOS Icon Composer (`.icon` export), generate the full-bleed `1024x1024` Apple PNG:

```sh
python3 tools/generate_brand_assets.py --kind apple
# -> design/generated/apple/AppIcon-1024.png
```

Pass `--out <dir>` whenever running isolated tests or ad-hoc exports so tracked `design/generated/` files are not mutated.

`--kind desktop` also emits the same full-bleed `1024x1024` PNG (`alpha=255` in all four corners, eliminating white corner halos on macOS). Files inside `design/generated/` are derived artifacts and must not be hand-edited. `design/tokens.json` stores the machine-readable token table, and `tools/generate_pwa_icons.py` defines the reference K geometry.

## PWA and Packaging Notes

WebAssembly stages the generated universal splash for the HTML shell while keeping any per-game splash independent. CMake embeds `icon.ico` into Windows executables via WinRes (except `school-days`, which embeds `assets/SchoolDays.ico`), installs `icon.icns` into macOS `.app/Contents/Resources/`, and references the generated Apple PNG icons from the shared iOS `Info.plist`.

## Visual Interface Reference v2.0 & "Soft" ImGui Variant

`design/tokens.json` (version 2) governs the web shell (violet/indigo controls, cyan focus, gradient progress bar, centered black splash), Android Material 3 surfaces, and Apple SwiftUI views.

For the Dear ImGui launcher header and debug overlay (`lib/src/debug/ImGuiTheme.cpp`), a calmer, desaturated **"Soft"** derivation is applied while keeping `design/tokens.json`, the K monogram, and platform icons unchanged:

| Role | Base v2 | Soft ImGui |
| --- | --- | --- |
| Window background (`Navy`) | `#0D142E` | `#1B2740` |
| Elevated surface (`Raised`) | `#182449` | `#273451` |
| Table header / selection (`Indigo`) | `#2C3E9A` | `#525D84` |
| Accent / title (`Violet`) | `#6A5BE7` | `#A79BD4` |
| Hover (`Magenta`) | `#C86BD9` | `#C3B4E0` |
| Table border (`Cyan`) | `#00D6FF` | `#8FB8D9` |
| Border / divider (`Outline`) | `#39315E` | `#3D4763` |
| Primary text (`White`) | `#FFFFFF` | `#DCE3F0` |
| Secondary text (`Muted`) | `#B9B5D4` | `#9DA9C2` |
| Primary button | `#6A5BE7` | `#525D84` |
| Success / warning / error | `#47D7A1` / `#FFBE68` / `#FF7389` | `#7FC8A8` / `#E8C89A` / `#E0A3AC` |

Text selection uses lilac at `0.25` alpha (down from `0.35`) and alternating table rows use slate at `0.16` alpha, maintaining WCAG AA contrast (`> 4.5:1` for normal text and `> 3:1` for large headings).
