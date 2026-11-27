# Arte externa opcional do School Days HQ (Windows L4)

[English (US)](README.md) | **Português (Brasil)**

Esta pasta contém apenas metadados públicos e instruções. A arte fornecida
pelo usuário permanece fora do Git. A fonte privada preferencial é
`games/school-days/game-assets/overlay/`; a pasta antiga
`games/school-days/game-assets/display-options/` continua aceita.

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
compile o SchoolDays para Windows. A tarefa CMake `SchoolDaysOptionAssets`
copia os arquivos para `<diretório de SchoolDays.exe>/overlay/System/Option/`
em DEV e DIST. Ela usa a fonte privada antiga
`game-assets/display-options/` somente quando o par
`Option_Display.png` / `Option_Display.cmap` não estiver completo em
`game-assets/overlay/`.

Durante o jogo, tanto o launcher quanto os assets preferem `<exe>/overlay/`
e recorrem ao legado `<exe>/display-options/` quando falta o novo par
PNG/CMAP. Um caminho de `--scripts`, `--asset-overlay` ou das configurações
salvas tem precedência; inclua nele os recursos personalizados, se necessário.

Nunca modifique, reempacote ou substitua `Packs/System.GPK`.
