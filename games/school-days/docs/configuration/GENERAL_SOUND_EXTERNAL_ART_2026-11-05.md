# School Days HQ / Kotonoha v3 — General and Sound external artwork (2026-11-05)

## User instruction and asset provenance

The user edited four PNGs for the retail General Options and Sound Options
panels. Use those exact PNGs outside `System.GPK`, in the same external artwork
folder as the previously integrated custom Display Options. Do not modify the
commercial GPK, save data, original CMAPs, or the game/global engine's
General/Sound semantics. No commit or push requested.

The exact source files are under `C:\Users\Suzu\Downloads\editados`:

| Source | Installed path inside `display-options` | Dimensions | SHA-256 |
| --- | --- | --- | --- |
| `OPTION_DEF.PNG` | `System/Option/Option_Def.png` | 800x450 | `0981A9418AEBDCE22A5C83DEF2675781C39C6734EC9CF1563442395BA272896A` |
| `OPTION_DEF_CHIP.PNG` | `System/Option/Option_Def_Chip.png` | 684x436 | `F7889CF91160C4CC9B79B8E9D3037E911C70803062FCFB1A33AE2443A99D26C8` |
| `OPTION_SOUND.PNG` | `System/Option/Option_Sound.png` | 800x450 | `5F1CE6B327C64585B088FEC8FC459BF7D1C47487E2CACD48967CB58CFD9D13DE` |
| `OPTION_SOUND_CHIP.PNG` | `System/Option/Option_Sound_Chip.png` | 684x354 | `4622787742E1C6345BDE5E4AE48045628A54322E279C6510EFD0D713A69F754D` |

All four installed files were copied byte-for-byte, and source/destination
SHA-256 values were compared. The original files in Downloads remain intact.

## Resolution, rendering and build

The existing `SchoolDaysAssets.cpp` automatically mounts a game-local
`display-options` directory beside `SchoolDays.exe`, unless an explicitly
configured `--asset-overlay` directory takes priority. The GPK-mounted asset
resolver checks that overlay before the retail packs. The `SchoolDaysSystemUi`
resolver already calls `Option_Def` / `Option_Sound` base and CHIP paths. No
runtime change or new GPK writer is necessary for these two panels.

The original General and Sound CMAPs and authenticated CHIP drawing tables
are still used, with no new regions or remapping. The existing custom Display
Options PNGs and four generated CMAP variants are retained in the same folder.
The shared SettingsMenu and other Kotonoha games are unchanged.

The `games/school-days/CMakeLists.txt` artwork-copy rule was changed from a
`POST_BUILD` step to a per-build `SchoolDaysOptionAssets` custom target that
SchoolDays depends on. This ensures an asset-only change is copied on the
next `cmake --build ... --target SchoolDays` even without a C++ relink.
Both multi-config Visual Studio and single-config Windows build layouts are
handled. No commit or push was made.

## Validation

1. Windows Release target `build/school-days-dev-control4` built successfully.
   Four deployed art hashes match the source files exactly.
2. `build/school-days-dev-control2`: all targets built successfully and
   `ctest -C Release --output-on-failure --parallel 4` passed **59/59**
   (16.74 seconds). The scope of the changes is external artwork and build
   packaging; the existing test suite also preserves Config/Display behavior.
3. Isolated graphical QA with original retail packs read-only, OpenGL renderer,
   DEV node `00/00-00-A03`, and private `--documents` / `--config` profile:
   `build/school-days-display-options-2026-11-05/qa-gen-sound-2026-11-05-v1`.
   The general and sound captures showed the user's new base/CHIP artwork,
   persistent button states and BGM/SE/VOICE bars, with successful navigation
   to Display Options. The session exited 0 and was closed in a `finally` block.
   Example screenshots:
   `qa-override-general-43126025518450.png`,
   `qa-override-sound-43127551262350.png`.
4. `git diff --check` returned exit 0.
5. The original `Packs/System.GPK` SHA-256 was the same before and after:
   `915A631410CA081327FC7C11B8AA0C54A7E93EAEFD9F6EACCC7DAC9469E6F54C`.

There was no complete playthrough from an empty save profile. No real user
saves, commercial assets, original EXE/DLLs, or GPK content were changed.
