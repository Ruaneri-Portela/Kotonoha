# Optional external artwork for School Days HQ (Windows L4)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

This directory contains public metadata and instructions only. The actual
user-supplied artwork remains outside Git. The preferred private source is
`games/school-days/game-assets/overlay/`; the previous
`games/school-days/game-assets/display-options/` source remains supported.

No PNGs from the commercial game or customized derivative artwork are
distributed in this repository. The new Windows Display Options page needs
user-supplied resources in `System/Option/` beneath that private directory. The
complete list, dimensions and behavior are documented in
[`PR_PARITY_CONSOLIDATION_2026-11-05.md`](../../PR_PARITY_CONSOLIDATION_2026-11-05.md).

The standard General/Sound CMAPs continue to come from the original game.
The Display CMAPs belong to the customized third page and must accompany
`Option_Display.png` and `Option_Display_Chip.png`. The 22 regions' bounds
are described in `display_options_controls.json` for reproducibility.

After adding the locally authored files to the private `System/Option/` folder,
build SchoolDays for Windows. The CMake `SchoolDaysOptionAssets` target stages
the files into `<SchoolDays.exe directory>/overlay/System/Option/` in both
DEV and DIST. It uses the old private `game-assets/display-options/` source
only when a complete `Option_Display.png` / `Option_Display.cmap` pair is
absent from `game-assets/overlay/`.

At runtime, both the launcher and gameplay prefer `<exe>/overlay/`, then the
legacy installed `<exe>/display-options/` when the new PNG/CMAP pair is
missing. An explicitly selected `--scripts`, `--asset-overlay` or saved
asset overlay path wins over both automatic choices; supply these resources
within that explicit overlay if needed.

Never patch, repack or replace the commercial `Packs/System.GPK` for this UI.
