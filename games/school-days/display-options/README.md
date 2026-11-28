# School Days custom Display Options artwork

**English (US)** | [Português (Brasil)](README.pt-BR.md)

The custom Display Options page uses user-authored PNG/CMAP files from a private build source. Commercial art is not distributed in Git.

- `Option_Display.png` and `Option_Display_Chip.png` are the custom panel/atlas.
- Matching custom Display CMAP files are required; `display_options_controls.json` documents the 22 control regions.
- General/Sound continue using their original game's CMAPs. Do not change `Packs/System.GPK`.
- Windows and Android can embed the approved UI directly in the native binary. External overrides remain optional. See the [current build instructions](../docs/configuration/display-options/README.md).

See the [original investigation](../docs/DISPLAY_OPTIONS_USER_ART_2026-11-05.md) and [integration evidence](../docs/PR_PARITY_CONSOLIDATION_2026-11-05.md).
