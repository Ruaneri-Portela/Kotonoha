# Optional external artwork for School Days HQ (Windows L4)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

This directory contains public metadata and instructions only. The actual
user-supplied artwork remains outside Git under
`games/school-days/game-assets/display-options/`.

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
run the SchoolDays Windows build again; the CMake `SchoolDaysOptionAssets` target
copies that private artwork directory next to `SchoolDays.exe` as
`display-options/`. The game's mount
uses that overlay unless an explicit `--asset-overlay` is specified. In that
case, include these files in the explicit overlay instead.

Never patch, repack or replace the commercial `Packs/System.GPK` for this UI.
