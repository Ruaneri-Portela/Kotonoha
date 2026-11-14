# Arte personalizada de Display Options — School Days

[English (US)](README.md) | **Português (Brasil)**

A tela personalizada **Display Options**, exclusiva do Windows, exige **PNGs e CMAPs externos feitos pelo usuário** na pasta `System/Option/` deste diretório. Arte comercial não é distribuída.

- `Option_Display.png` e `Option_Display_Chip.png` são o painel e seu atlas.
- São necessários CMAPs específicos de Display; `display_options_controls.json` descreve as 22 regiões de controle.
- General/Sound mantêm os CMAPs originais. Não modifique `Packs/System.GPK`.
- Recompile o alvo Windows SchoolDays para copiar `display-options/` ao lado do executável (alvo CMake `SchoolDaysOptionAssets`). Ao usar `--asset-overlay` explícito, inclua a arte no overlay escolhido.

Veja a [investigação original](../docs/DISPLAY_OPTIONS_USER_ART_2026-11-05.md) e a [integração](../docs/PR_PARITY_CONSOLIDATION_2026-11-05.md).
