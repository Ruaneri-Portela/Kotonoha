# Arte externa opcional do School Days HQ (Windows L4)

[English (US)](README.md) | **Português (Brasil)**

Esta pasta contém apenas metadados públicos e instruções. A arte fornecida
pelo usuário permanece fora do Git em
`games/school-days/game-assets/display-options/`.

Nenhuma PNG comercial ou arte derivada personalizada é distribuída neste
repositório. A página Display Options do Windows exige recursos fornecidos
pelo usuário em `System/Option/` sob essa pasta privada. A lista completa,
dimensões e comportamento estão documentados em
[`PR_PARITY_CONSOLIDATION_2026-11-05.md`](../../PR_PARITY_CONSOLIDATION_2026-11-05.md).

Os CMAPs normais de General/Sound continuam vindo do jogo original. Os CMAPs
específicos de Display devem acompanhar `Option_Display.png` e
`Option_Display_Chip.png`. As 22 regiões estão descritas em
`display_options_controls.json` para reprodução.

Após colocar os arquivos criados localmente em `System/Option/` privado,
compile novamente o alvo SchoolDays para Windows. A tarefa CMake
`SchoolDaysOptionAssets` copia a arte privada ao lado de `SchoolDays.exe` como
`display-options/`. O jogo monta esse overlay, exceto quando um
`--asset-overlay` explícito for selecionado; nesse caso, inclua os arquivos
no overlay escolhido.

Nunca modifique, reempacote ou substitua `Packs/System.GPK`.
