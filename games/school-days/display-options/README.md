# School Days custom Display Options artwork

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The Windows-only custom Display Options page requires **user-authored, external PNG/CMAP files** in this directory's `System/Option/` path. Commercial art is not distributed.

- `Option_Display.png` and `Option_Display_Chip.png` are the custom panel/atlas.
- Matching custom Display CMAP files are required; `display_options_controls.json` documents the 22 control regions.
- General/Sound continue using their original game's CMAPs. Do not change `Packs/System.GPK`.
- Build the SchoolDays Windows target again to stage `display-options/` beside the executable (`SchoolDaysOptionAssets` CMake target). If you explicitly select `--asset-overlay`, supply these files in that overlay instead.

See the [original investigation](../docs/DISPLAY_OPTIONS_USER_ART_2026-11-05.md) and [integration evidence](../docs/PR_PARITY_CONSOLIDATION_2026-11-05.md).
