# School Days HQ / Kotonoha v3 — authored Display Options replacement (2026-11-05)

## Final user correction and scope

The **SECOND** user-uploaded image is the final base, with variable value
texts already removed. Never regenerate, paint over or erase more of the base.
The **FIRST** upload is the exact custom three-band CHIP atlas. This new UI
replaces the obsolete SOMCON page **for School Days on Windows only**, without
changing the global SettingsMenu provided by Kotonoha to other games.

Keep General Options exclusively responsible for WINDOW/FULL and
STANDARD/WIDE. FULL remains native-desktop borderless (not exclusive);
WINDOW remains a decorated Windows application. STANDARD+FULL still uses
the previously implemented 75%-height narrative presentation and adjusted
config input mapping. Sound settings, Android-specific controls, real saves
and commercial assets must not change.

## Original user files and validated hashes

* Base: second image `Design sem nome (1)(2).png`, 800x450 RGBA, Windows
  original `C:\Users\Suzu\Downloads\Design sem nome (1).png`.
  SHA-256 `DCEE4C073875D0C1DC7F9E5F2A4A1B0A5C98604D593460DD887F67EE63C62161`.
* CHIP: first image `Design sem nome(1).png`, 684x1212 RGBA with transparent
  background, Windows original `C:\Users\Suzu\Downloads\Design sem nome.png`.
  SHA-256 `AE50EC5847FD6D6F0F27B9EEF403A462031AFC4874FDA51AC11E2AD4F064ACE8`.
* Both sources were copied byte-for-byte; neither was visually regenerated.
  The unmodified final source assets are in
  `games/school-days/display-options/System/Option/`.

## Files, CMAP and rendering

The external, user-owned assets are:

* `Option_Display.png` — base image (800x450), including the neutral white
  **Display Options** tab and bright white CLOSE button.
* `Option_Display_Chip.png` — original 684x1212 transparent atlas, with
  normal/selected/hover bands; CLOSE only uses the normal bright glyph.
* `Option_Display.cmap` — STANDARD 800x600, authored Y offset 75.
* `Option_Display_Wide.cmap` — WIDE 800x450.
* `Option_Display_Wide_Full.cmap` — WIDE FULL 1280x720.
* `Option_Display_Wide_Note.cmap` — WIDE NOTE 1024x576.

The CMAP binary has little-endian int32 width/height and one uint8
region per pixel, with region 0 for background and region `id+1` for one of
22 interactive controls. Controls 0/1/2 are tabs, 3 is CLOSE, 4 resolution,
5/6 Resizable, 7/8 VSync, 9 upscaler, 10 shader preset, 11 GLSL path,
12 BROWSE, 13..19 factor 1x..4x in 0.5x steps, 20 DEFAULT, 21 APPLY.
Full region geometry is in the ZIP's `display_options_controls.json`.

The top of the original base already contains the normal controls. The
render plan must **not** redraw every normal CHIP over it. Only the bright
CLOSE glyph, persistent selected Resizable/VSync/factor, and pointer hover
sprites are composited, without permanently painting the third tab blue.
Atlas bands do not have identical Y offsets: tab hover at ~798 and
controls below the tabs at ~856, ~898, ~940, ... ~1152. Do not replace
the authored offsets with a single global +798 translation: that produced
a displaced duplicate 2x hover glyph and was corrected during visual QA.

## Runtime

* `SchoolDaysConfigMenuModel` points Page::Display at `Option_Display` art and
  maps CMAP controls 4..21 to `ActionKind::DisplayControl`.
* `SchoolDaysConfigMenuRenderPlan` maps the user's atlas source rectangles
  to the four canvas variants; highlights reflect the pending settings draft.
* `SchoolDaysRuntime` uses an EngineSettings draft for display controls:
  Resolution/Hz uses `DisplayModeCatalog`, Resizable/VSync are two-state,
  Upscaler offers supported backends, MPV/OpenGL offers 15 shader presets,
  factor 1x..4x, and path/BROWSE opens a modal text/paste editor with
  validation of existing `.glsl`, `.hook`, `.frag` files. APPLY calls
  `engine->applySettings(draft)` with persistent settings; DEFAULT restores
  only this page's fields, not General/Aspect/Audio. Leave or close without
  APPLY to discard the draft. Dynamic values are centered inside the blank
  fields of the correct base image.
* Borderless always uses the real desktop size/Hz. A non-desktop resolution
  is not silently promoted to exclusive fullscreen. In WINDOW, changing
  resolution restores a maximized window before applying its client size.

The BROWSE control currently opens the text/paste path editor rather than
an OS-native file chooser. This is a **known UI limitation**, not a claim of
full Explorer browsing support.

## Packaging without altering commercial content

The commercial `Packs/System.GPK` remains untouched. CMake copies only
the user-authored folder `games/school-days/display-options/` to
`<SchoolDays.exe directory>/display-options/`. `SchoolDaysAssets.cpp`
automounts that directory when no explicit game asset overlay was selected;
the global SettingsMenu and other games remain unchanged. An explicitly
configured overlay takes priority; such an overlay must contain the
Display assets if the custom tab should also appear.

For isolated testing, the exact corrected ZIP was extracted under
`build/school-days-display-options-2026-11-05/overlay/`, and QA profiles
under `build/school-days-display-options-2026-11-05/qa-*/`.

## Validation

* Release build of all targets in `build/school-days-dev-control2`: PASS.
* User-facing Release `build/school-days-dev-control4/games/school-days/Release/SchoolDays.exe`:
  PASS, including copying the user-authored display-options resources.
* Full CTest Release, parallel 4: **59/59 PASS, 15.70 s** after final CHIP
  hover-crop and text-centering corrections.
* Dedicated CMAP test verifies four real variants, 22 regions, source
  bounds, active/hover sprites, current states, and no permanent selected
  Display tab. Updated SchoolDays.Save regression verifies this new page
  rather than incorrectly expecting the superseded engine-global popup.
* Isolated DevBridge GUI smoke on real original read-only retail Packs with
  OpenGL renderer: screenshot verifies the exact base, centered live values,
  Resizable OFF, VSync OFF and factor 2x (selected CHIP aligned), APPLY
  persists the latter 3 settings, and resolution/shader/GLSL modal screens
  open. The final successful smoke prefix is
  `qa-display-*-3996*.png` through `qa-display-*-39977*.png`.
* `git diff --check`: exit 0 (normal LF/CRLF advisory output).

No commercial original GUI run, no complete story playthrough, no changes
to real saves, no GPK write, and no commit/push. Different monitors, every
advertised refresh-rate switch and arbitrary custom GLSL shader programs
were not exhaustively tested.
