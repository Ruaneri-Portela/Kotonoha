# Arte de opções embutida do School Days HQ (Windows e Android)

[English (US)](README.md) | **Português (Brasil)**

Esta pasta contém apenas metadados públicos e instruções. A arte fornecida
pelo usuário permanece fora do Git. A fonte privada preferencial é
`games/school-days/game-assets/overlay/`; a pasta antiga
`games/school-days/game-assets/display-options/` continua aceita.

Nenhuma PNG comercial ou arte derivada personalizada é distribuída neste
repositório. A página Display Options personalizada utiliza recursos fornecidos
pelo usuário em `System/Option/` sob essa pasta privada. A lista completa,
dimensões e comportamento estão documentados em
[`PR_PARITY_CONSOLIDATION_2026-11-05.md`](../../PR_PARITY_CONSOLIDATION_2026-11-05.md).

Os CMAPs normais de General/Sound continuam vindo do jogo original. Os CMAPs
específicos de Display devem acompanhar `Option_Display.png` e
`Option_Display_Chip.png`. As 22 regiões estão descritas em
`display_options_controls.json` para reprodução.

Windows e Android DEV/DIST incorporam os seis recursos aprovados de Display
ao binário nativo quando presentes na configuração. O gerador verifica seus
hashes SHA-256. Defina `KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE` como a
pasta privada e `KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_OVERLAY=ON` para falhar
se faltarem arquivos. No Android, use as propriedades Gradle
`schoolDaysDisplayOverlaySource` e `schoolDaysRequireEmbeddedOverlay=true`.

Somente para QA privado autorizado, os quatro PNGs aprovados de General/Sound
editados pelo usuário também podem ser incorporados com
`KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART=ON` ou a propriedade Android
`schoolDaysEmbedPrivateOptionArt=true`. Não redistribua esses binários sem
permissão. Nenhum byte dos arquivos privados fica no Git.

Não é necessário instalar uma pasta de overlay para a UI embutida. Overrides
externos existentes continuam opcionais e têm prioridade sobre recursos
embutidos; os packs retail originais fornecem os demais recursos.

Nunca modifique, reempacote ou substitua `Packs/System.GPK`.
