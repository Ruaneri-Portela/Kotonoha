# School Days HQ no Android — Launcher em Retrato e Port Completo

O port Android do School Days HQ compartilha a mesma lógica C++17 de jogo, montagem de pacotes, validação de traduções, persistência retail `Config.DAT` e integração L×H da build Windows L4. O launcher abre em **retrato**; o gameplay inicia em **paisagem tela cheia** (`sensorLandscape`) com controles por toque.

## Estrutura da Pasta no Celular (`SchoolDaysHQ/`)

Coloque os seus arquivos legítimos do School Days HQ no armazenamento interno (por exemplo `/storage/emulated/0/SchoolDaysHQ/` ou `/storage/emulated/0/Download/SchoolDaysHQ/`):

```text
SchoolDaysHQ/
├── Packs/
│   ├── Script.GPK
│   ├── System.GPK
│   ├── ... todos os 29 GPKs originais HQ ...
│   └── LxH.GPK
├── game-assets/
│   ├── school-days-hq.ktnroute
│   ├── styles.ass                  (folha de estilos ASS opcional)
│   ├── launcher-art/
│   │   └── SchoolDaysHQ-Launcher.png  (arte opcional do launcher)
│   └── forced_subtitles/           (opcional, <idioma>/<nodo>.ass)
│       ├── en/
│       ├── pt-BR/
│       ├── es/
│       ├── ru/
│       └── zh-CN/
├── overlay/
│   └── System/Option/              (Display Options e substituições opcionais)
└── translations/
    ├── scripts/                    (pacotes GPK nativos: Script - *.GPK + *-lxh.GPK)
    └── jsons/                      (pares JSON: <idioma>.json + <idioma>-lxh.json + reviewed-identities)
```

> **Importante:** Selecione sempre a pasta pai `SchoolDaysHQ/` no seletor do Android — **nunca** a subpasta `Packs/` diretamente. O SAF do Android só concede leitura à pasta escolhida e às suas subpastas; escolher `Packs/` impede acesso a `game-assets/`, `overlay/` e `translations/`.

## Guia Rápido de Uso

1. Instale o APK ARM64 no aparelho Android (Android 11 / API 30+).
2. Abra o **School Days HQ** (launcher em retrato).
3. Toque em **Selecionar pasta SchoolDaysHQ** e autorize a pasta `SchoolDaysHQ`.
4. Abra **Opções** para configurar:
   - **Tela**: Resolução nativa / taxa de atualização, VSync, Proporção narrativa (`Automático` / `4:3` / `16:9`), Limite de quadros Android (`Ilimitado` / `30` / `60` FPS).
   - **Gráficos**: Escala de renderização (`0.25x`–`1.00x`), indicador OpenGL ES, Upscaler (`Auto` / `None` / `Linear` / `MPV`), Fator de upscale (`1.00x`–`4.00x`), Preset de shader (Anime4K / ArtCNN em níveis Low/Medium/High).
   - **Áudio**: Volumes retail `BGM`, `SE`, `Vozes` (0–10), Vozes masculinas, Silenciar jogo, Volumes Master e Vídeos do motor, e **Música de fundo do launcher** (reproduz `TitleBGM` de `Ini.GPK`/`BGM.GPK` somente no launcher, independente do mudo do jogo).
   - **Geral**: Exibir texto, Pular cenas não lidas, SuperSkip / próxima escolha, Auto draw, Modo automático, Inverter botões A/B, Ativar legendas forçadas (`.ass`).
   - **Idioma**: Escolha entre **Pacotes GPK nativos** (`translations/scripts/`) ou **Dicionários JSON** (`translations/jsons/`) para `en`, `pt-BR`, `es`, `ru`, `zh-CN`. Arquivos HQ e L×H são obrigatórios; fallback silencioso para inglês é bloqueado.
   - **Arquivos**: Confira contagem de pacotes (`30/30`), rota KTRF, `styles.ass`, `forced_subtitles/`, `overlay/` e traduções detectadas; troque ou desconecte a pasta.
   - Toque em **Aplicar** para gravar (`settings.json`, `save/Config.DAT`, `save/Kotonoha.ini`), **Cancelar** para descartar ou **Restaurar padrões**.
5. Toque em **Iniciar jogo** para jogar em paisagem tela cheia.
6. Controles no jogo: toque na tela para avançar/escolher opções, toque no botão **MENU** no canto inferior esquerdo para abrir a barra retail (Save, Load, BackLog, Route Map, Config, Auto, Skip, Title) e use o botão **Voltar** do Android para fechar menus modais.

## Arquitetura e Limites de Armazenamento

- **Montagem SAF somente leitura (`DocumentMount`)**: Os 30 arquivos (`Packs/*.GPK` + `LxH.GPK`) permanecem no local original e são lidos via URIs SAF com cache de stream protegido por mutex por arquivo (`SchoolDaysGpkArchive`), eliminando reaberturas repetidas sem copiar gigabytes de pacotes.
- **Cache temporário por sessão (`SchoolDaysFolder`)**: Apenas arquivos pequenos que exigem caminho físico (`school-days-hq.ktnroute`, GPKs ou JSONs + sidecars `reviewed-identities` do idioma ativo, `game-assets/styles.ass`, `forced_subtitles/` do idioma ativo e `overlay/`) são preparados no cache do app e removidos ao sair.
- **Configurações e saves compartilhados**: `settings.json` fica em `Android/data/<pacote>/files/settings.json`; `Config.DAT`, `Kotonoha.ini`, `GlobalFlag.DAT` e `Save/` ficam em `Android/data/<pacote>/files/save/` — nunca dentro do SAF nem na pasta de arquivos do jogo.
- **Legendas forçadas**: Preferência por `game-assets/forced_subtitles/<idioma>/<nodo>.ass` (encaminhado via `--forced-subtitles` em execuções SAF), mantendo compatibilidade legada com `forced_subtitles/`.
- **Segurança de fontes no ImGui**: `ImGuiTheme::loadFonts` valida limites SFNT/TTC e compatibilidade com o carregador ativo do ImGui, usando `AddFontDefaultVector()` como fallback explícito quando coleções CJK do sistema (como `/system/fonts/NotoSansCJK-Regular.ttc`) são incompatíveis com `stb_truetype` ou em telas de alto DPI.
- **Empacotamento do APK**: Nenhum GPK comercial, rota KTRF, tradução ou arte comercial entra no APK (`-PbundleGameAssets=false`). Todas as bibliotecas ARM64 `.so` usam alinhamento ELF de 16 KiB (`-Wl,-z,max-page-size=16384`).

## Estado Atual de Verificação (POCO X7 Pro, Android 16 / API 36)

> **Atenção:** Esta tabela distingue explicitamente o que está **implementado**, **compilado**, **funcionando parcialmente**, **validado em hardware real** e **pendente**. Apenas itens com evidência direta no POCO X7 Pro (`QO5XJZFYDMRWDU6T`) estão marcados como validados em hardware real.

| Área | Estado Atual | Evidência / Observação |
|---|---|---|
| Launcher retrato + SAF (`30/30` GPK, KTRF, `styles.ass`, `overlay/`) | Validado em hardware real | Abre em retrato no POCO X7 Pro, lê `SchoolDaysHQ/` via SAF sem copiar GPKs. |
| **P0 — Primeira legenda, avanço de gameplay e BackLog** | **Validado em hardware real (corrigido)** | Causa raiz confirmada via LLDB (`SDL_iconv_string` → Bionic `iconv` `E2BIG` em loop infinito). Substituído por `Kotonoha::Utf8ToUtf16`/`Utf16ToUtf8` em C++17 puro. Validado 3× consecutivas no Episódio 1 além de múltiplas legendas, transição para sala de aula, MenuBar superior, `MENU` e `BACKLOG`, mantendo RSS estável (~645–664 MB). |
| **P1 — Overlay customizado Display Options (`Option_Display*.png/.cmap`)** | **Validado em hardware real (corrigido)** | Guardas `#if defined(__ANDROID__)` removidas em `SchoolDaysRuntime.hpp/.cpp`; retângulo branco artificial eliminado das abas General/Sound; `Option_Display` nativo carrega modos reais (`2712x1220 @ 30/60/90/120 Hz`), VSync, Upscaler, Shader preset, Custom GLSL, Upscale factor (`1x`–`4x`), `DEFAULT` e `APPLY` com toque funcional. |
| **P1 — Volume de áudio (dupla atenuação)** | **Corrigido e validado em testes + hardware (aguarda QA auditivo do usuário)** | `SchoolDaysRuntime::applyConfig()` reseta `userVolume = 1.0f` nos bancos retail `BGM`/`SE`/`Voice` (e reaplica após `applySettings`), eliminando a dupla multiplicação (`0.2985 × 0.2985 = 0.089` / -21 dB) e o teto travado em 29,8% no nível 10/10. Ganho do `TitleBGM` do launcher ajustado de `0.35f` para `0.60f`. Asserção unitária adicionada em `SchoolDaysRuntimeConfigStorageTest`. |
| **P1 — Persistência bidirecional Launcher ↔ Jogo** | **Validado em hardware real (corrigido)** | `Config.DAT` agora faz flush imediato no Android ao alterar opções e em `SDL_EVENT_WILL_ENTER_BACKGROUND`; `SchoolDaysSetup.onResume()` recarrega e ressincroniza toda a UI; modos de tela normalizados para paisagem (`2712x1220 @ 90 Hz` bate nos dois sentidos). Validado ida e volta incluindo `force-stop` (`BGM 7/10`, `2.00x`, `90 Hz`, `4:3`, `Restaurar padrões`). |
| **P1 — Idiomas GPK Russo e Chinês** | **Diagnosticado e UX corrigida em hardware real** | Confirmado na pasta real `/storage/emulated/0/Download/SchoolDaysHQ/translations/scripts/` que existem apenas `Script - Brazilian.GPK`, `Script - Spanish.GPK`, `en-lxh.GPK`, `es-lxh.GPK`, `pt-BR-lxh.GPK` (não há GPKs `ru` nem `zh-CN`; ambos existem apenas em `translations/jsons/`). A aba Idioma agora lista exatamente os arquivos GPK ausentes sob cada opção desabilitada e informa que o par JSON está disponível abaixo. |
| **P1 — Resolução, proporção (`4:3`/`16:9`) e toque (2712×1220)** | **Validado parcialmente em hardware real** | Paisagem 2712×1220, troca 16:9 ↔ 4:3 via launcher (`Kotonoha.ini`), toques em Título, `OPTION`, `Option_Display`, popups, `Sound Options`, MenuBar superior, `MENU` e `BACKLOG` validados no POCO. |
| **P2 — Gameplay estendido (escolhas, Save/Load, Route Map, Replay, CJK/cirílico, 7 finais L×H)** | **Pendente QA estendido em hardware real** | Desbloqueado após correção do P0 (`Utf8ToUtf16`/`Utf16ToUtf8` também protege escolhas `SETSELECT` e `BACKLOG`); validação interativa longa de escolhas, Save/Load, Route Map, Replay, legendas forçadas, fontes CJK/cirílico e sequência completa dos 7 finais L×H ainda pendente. |
| Builds Windows DEV Release / DIST Release + CTests | Compilado e testado | `SchoolDays` e todos os 57/57 testes `SchoolDays*` + `Kotonoha.EncodingUtf` + `Kotonoha.AudioChannels` aprovados no Windows DEV Release; Windows DIST Release verificado em seguida. |

## Regressões e problemas funcionais observados no POCO X7 Pro — 21/11/2026

Registro contínuo dos defeitos observados em hardware real (POCO X7 Pro, Android 16 / API 36, APK DEV `me.hirameki.kotonoha.schooldays`) a partir do HEAD `0e7dc7cf`:

### 1. [P0] Congelamento completo ao exibir a primeira legenda (`......`)
- **Sintoma**: Ao iniciar um novo jogo (Episódio 1, modo GPK `pt-BR`), o vídeo da abertura no trem reproduzia até ~32s e congelava completamente no exato instante da primeira legenda (`......`), com `SDLThread` travado em 100% de CPU e RSS escalando até `3,8 GiB` até LMKD encerrar o processo.
- **Reprodução**: POCO X7 Pro (`QO5XJZFYDMRWDU6T`), `Iniciar jogo` → `START` → aguardar ~32s no Episódio 1.
- **Causa raiz confirmada (via LLDB + `llvm-addr2line` acoplados ao `SDLThread` congelado)**:
  ```text
  SDL_iconv (SDL_iconv.c:57) ← SDL_iconv_string (SDL_iconv.c:822)
    ← SchoolDays::Utf16 (SchoolDaysRuntime.cpp:645)
    ← SchoolDays::Runtime::dispatch PRINT_TEXT (SchoolDaysRuntime.cpp:2998)
    ← SchoolDays::Runtime::update (SchoolDaysRuntime.cpp:3708)
  ```
  No Android API 28+, o SDL3 compila com `HAVE_ICONV=1` chamando o `iconv()` da Bionic (`libc.so`). Ao converter `"......"` + NUL de `UTF-8` para `UTF-16LE` em `SDL_iconv_string`, a Bionic retorna `E2BIG` repetidamente, e o ramo `case SDL_ICONV_E2BIG:` de `SDL_iconv.c:824-839` dobra `stringsize *= 2` com `SDL_realloc` indefinidamente (sem checar progresso) até esgotar gigabytes de RAM. O mesmo padrão existia em `SchoolDaysScenes.cpp` (escolhas) e `SchoolDaysBackLogPresenter.cpp` (BackLog).
- **Arquivos alterados**:
  - `lib/src/localization/Encoding.cpp` (`Kotonoha::Utf8ToUtf16` e `Kotonoha::Utf16ToUtf8` reescritos em C++17 puro conforme RFC 3629, sem `SDL_iconv` nem overhead JNI, com tratamento seguro de pares substitutos e sequências inválidas → `U+FFFD`)
  - `games/school-days/src/SchoolDaysRuntime.cpp` (`Utf16` delega a `Kotonoha::Utf8ToUtf16`)
  - `games/school-days/src/SchoolDaysScenes.cpp` (`Utf8ToUtf16`/`Utf16ToUtf8` delegam a `Kotonoha::*`)
  - `games/school-days/src/SchoolDaysBackLogPresenter.cpp` (`Utf16ToUtf8BackLog` delega a `Kotonoha::Utf16ToUtf8`)
  - `lib/tests/EncodingUtfTests.cpp` + `lib/tests/CMakeLists.txt` (novo teste unitário `Kotonoha.EncodingUtf`)
- **Testes executados**: `Kotonoha.EncodingUtf` aprovado (Windows DEV/DIST); 3 execuções completas no POCO X7 Pro no Episódio 1 avançando por múltiplas legendas acentuadas em `pt-BR`, transição do trem para a sala de aula, abertura do MenuBar superior, `MENU` e `BACKLOG` com RSS estável (~645–664 MB).
- **Estado**: **Corrigido e validado em hardware real**.

### 2. [P1] Idiomas Russo e Chinês indisponíveis em modo GPK
- **Sintoma**: Na aba Idioma do launcher Android, `Russian (GPK)` e `Chinese - Simplified (GPK)` apareciam desabilitados sem explicar o motivo.
- **Inspeção real na pasta do usuário**: `/storage/emulated/0/Download/SchoolDaysHQ/translations/jsons/` possui os 5 pares completos (`en`, `pt-BR`, `es`, `ru`, `zh-CN`), mas `translations/scripts/` contém apenas `Script - Brazilian.GPK`, `Script - Spanish.GPK`, `en-lxh.GPK`, `es-lxh.GPK` e `pt-BR-lxh.GPK`. Não existem `Script - Russian.GPK`/`ru-lxh.GPK` nem `Script - chinese-compatible.GPK`/`zh-CN-lxh.GPK` no aparelho (mesma exigência de `SchoolDaysGpkLanguageProfile.hpp` e `SchoolDaysLauncher.cpp` no Windows).
- **Correção aplicada**: `SchoolDaysFolder.java` expõe `missingGpkFiles(locale)` e `hasLegacyIncompatibleChineseGpk()`; `SchoolDaysSetup.java` + `strings.xml` (EN e PT-BR) exibem sob cada botão GPK desabilitado exatamente quais arquivos faltam em `translations/scripts/` e indicam quando o par JSON está disponível abaixo.
- **Estado**: **Diagnosticado e UX validada em hardware real** (não habilita falsamente pacotes inexistentes/incompatíveis).

### 3. [P1] Persistência bidirecional entre Launcher e Gameplay
- **Sintoma**: Alterações feitas dentro do jogo ou no launcher não refletiam de forma consistente nos dois sentidos após fechar/reabrir; além disso, a resolução escolhida no jogo (`2712x1220 @ 90 Hz` em paisagem) não casava com o spinner do launcher retrato (`1220x2712 @ 90 Hz`).
- **Causas raiz confirmadas**:
  1. `SchoolDaysRuntime.cpp` só gravava `Config.DAT` em `shutdown()`, perdendo alterações se a tarefa fosse encerrada no Android.
  2. `SchoolDaysSetup.onResume()` não chamava `syncUiFromDraft()` após recarregar o disco.
  3. `SchoolDaysSetup` comparava `Display.Mode` em retrato (`1220x2712`) contra `fullscreenWidth x fullscreenHeight` gravado em paisagem (`2712x1220`).
- **Arquivos alterados**: `games/school-days/src/SchoolDaysRuntime.cpp` (flush imediato de `Config.DAT` no Android em `cacheConfigState`, `CacheWindowMode` e `SDL_EVENT_WILL_ENTER_BACKGROUND`, mantendo deferred flush do Windows inalterado; matching independente de orientação em `SelectedAndroidMode`), `buildsystems/android/app/src/schoolDays/java/org/kotonoha/android/SchoolDaysSetup.java` (normalização paisagem dos modos de tela e `syncUiFromDraft()` em `onResume()`).
- **Testes executados**: Ciclo completo no POCO X7 Pro: Display in-game (`2712x1220 @ 90 Hz`, `2x`) + Sound in-game (`BGM 7/10` → `save/Config.DAT` criado imediatamente) → `force-stop` → Launcher exibiu `2712 x 1220 @ 90 Hz`, `2.00x` e `BGM: 7 / 10`; depois Launcher `4:3` → `Aplicar` → jogo abriu em 4:3 Standard; depois `Restaurar padrões` → `Aplicar` restaurou `ContentAspectPreference=0`.
- **Estado**: **Corrigido e validado em hardware real**.

### 4. [P1] Overlay customizado Display Options não utilizado no Android
- **Sintoma**: As abas General/Sound mostravam um retângulo branco artificial `"DISPLAY OPTIONS"` sobre a 3ª aba, e a aba Display abria um painel escuro textual legado (`"DISPLAY / ANDROID"`) em vez de `overlay/System/Option/Option_Display.png`, `Option_Display_Chip.png` e `Option_Display*.cmap`.
- **Causa raiz confirmada**: Guardas `#if defined(__ANDROID__)` / `#if !defined(__ANDROID__)` em `SchoolDaysRuntime.hpp` e `SchoolDaysRuntime.cpp` desativavam o fluxo compartilhado de Display Options no Android.
- **Arquivos alterados**: `games/school-days/src/SchoolDaysRuntime.hpp` e `games/school-days/src/SchoolDaysRuntime.cpp` (unificação do fluxo `renderDesktopDisplayPanel` / `handleDesktopDisplayPanel` / `activateDesktopDisplayControl` no Android usando `AndroidDisplayModesUnique()` + `AndroidRequestDisplayMode()`, preservando `renderer = "opengles2"` e mantendo Windows 100% intacto).
- **Testes executados**: Validado visualmente e por toque no POCO X7 Pro (abas General/Sound limpas sem retângulo branco; aba Display usando `Option_Display.png`/`Chip`/`CMAP`, popup de resolução listando `30/60/90/120 Hz`, seleção de `90 Hz` e `2x` persistindo imediatamente).
- **Estado**: **Corrigido e validado em hardware real**.

### 5. [P1] Áudio extremamente baixo nos menus e gameplay
- **Sintoma**: Som anormalmente baixo em menus/gameplay e teto baixo mesmo no volume máximo.
- **Causa raiz confirmada**: Dupla atenuação em `lib/src/media/AudioMixer.cpp` (`master * userVolume * volume * entry.gain`): `settings.json` carregava `channelVolumes["BGM"|"SE"|"Voice"] = 0.2985` em `userVolume` e `SchoolDaysRuntime::applyConfig()` aplicava novamente `Policy::LinearGain` em `channel->setVolume()`, resultando em `0.2985 × 0.2985 = 0.089` (-21 dB) e travando o nível 10/10 em 29,8%.
- **Arquivos alterados**: `games/school-days/src/SchoolDaysRuntime.cpp` (`applyConfig()` força `userVolume = 1.0f` em `BGM`, `SE`, `Voice` e é reaplicado após `engine->applySettings()`), `games/school-days/android/java/org/kotonoha/android/SchoolDaysTitleBgmPlayer.java` (`LAUNCHER_MUSIC_GAIN` `0.35f` → `0.60f`), `games/school-days/tests/SchoolDaysRuntimeConfigStorageTest.cpp` (asserção garantindo `getUserVolume() == 1.0f` e aplicação única da curva retail).
- **Testes executados**: `SchoolDaysRuntimeConfigStorageTest`, `Kotonoha.AudioChannels` e `SchoolDaysLauncherTest` aprovados; controle de BGM/Config.DAT validado no POCO (confirmação subjetiva final de loudness físico recomendada pelo usuário nos alto-falantes do aparelho).
- **Estado**: **Causa raiz corrigida e testada; validação auditiva final pelo usuário pendente**.

### 6. [P1] Adaptação de tela (2712×1220), proporção e toque
- **Estado**: Paisagem 2712×1220, troca 16:9 ↔ 4:3, e áreas de toque em Título, `OPTION` (3 abas + popups), MenuBar superior, `MENU` e `BACKLOG` validados no POCO X7 Pro.

---

## White fade e viewport (Marco 2)

### Defeito P0 encontrado
- **Sintoma**: Durante a abertura/fechamento de painéis de sistema em gameplay (`SAVE`, `LOAD`, `BACKLOG`, `OPTION`), o fade branco (`SchoolDaysSystemPanelTransition`) pintava a superfície física inteira (`2712×1220`), vazando branco sobre as barras pretas laterais (pillarbox) em telas ultrawide e sobre margens letterbox.
- **Causa raiz confirmada**: `Runtime::renderSystemPanelTransition()` (`games/school-days/src/SchoolDaysRuntime.cpp`) executa em `RenderSpace::Screen` e chamava `SDL_RenderFillRect(&renderer, nullptr)` tanto para o fundo preto quanto para o overlay branco (`255, 255, 255, WhiteAlpha`), diferente de `renderExitTransition()` e `renderFrontendTransition()` que já calculavam o retângulo ajustado do viewport.
- **Correção compartilhada (Windows + Android)**:
  - Adicionado helper puro `SchoolDaysSystemPanelTransition::ComputeFittedViewport(...)` em `games/school-days/include/Kotonoha/routing/SchoolDaysSystemPanelTransition.hpp`.
  - `Runtime::renderSystemPanelTransition()` calcula o retângulo exato da área ativa do jogo/painel (`800×600` em `Standard`, `800×450`/`1280×720`/`1024×576` em `Wide*`, respeitando `narrativePresentationHeightScale()` e stretch fullscreen de `Config` no Windows Standard FULL; e `1280×720` ajustado na fase `Revealing`) e restringe o fade branco estritamente a `area`, mantendo as barras pretas externas sempre em `#000000`.
  - Preservadas intactas a temporização retail (`750 ms` abertura / `500 ms` fechamento / `500 ms` revelação) e as transições `renderLoadTransition`, `renderTransportFade`, `renderLxhFade` e `replayHSceneTransitionActive`.
- **Verificação automatizada e em hardware real**:
  - Novo teste `SchoolDaysSystemPanelTransitionTest` (`games/school-days/tests/SchoolDaysSystemPanelTransitionTest.cpp`): valida curva de alfa, geometria em `2712×1220` (`16:9` e `4:3`), Windows Standard FULL (`0.75×` + stretch), e leitura de pixels via `SDL_RenderReadPixels` em renderer software SDL3 (pillarbox e letterbox `#000000`, centro branco). Aprovado junto com `SchoolDaysExitTransitionTest` e `SchoolDaysFrontendTransitionTest` (3/3).
  - Captura real no POCO X7 Pro (`2712×1220`):
    - **16:9** (`SAVE` abertura/fechamento, `BACKLOG` abertura): bordas `x=20, 260, 2451, 2691` medidas em `(0, 0, 0)` puro enquanto o centro da área de jogo atingiu `(167, 167, 167)`, `(245, 244, 244)` e `(180, 180, 180)`.
    - **4:3 Standard** (`SAVE` abertura): bordas `x=20, 260, 500, 2211, 2451, 2691` medidas em `(0, 0, 0)` puro enquanto o retângulo `4:3` (`x=600..2111`) atingiu `(172, 172, 172)`.
- **Classificação**: **Paridade confirmada** (Windows e Android).

---

## Integração real de forced subtitles (Marco 3)

### Auditoria do ambiente e da cadeia completa
- **Descoberta inicial no dispositivo**: `/storage/emulated/0/Download/SchoolDaysHQ/game-assets/` no POCO X7 Pro continha originalmente apenas `school-days-hq.ktnroute` e `styles.ass`; não havia pasta `forced_subtitles/` presente no aparelho antes deste marco.
- **População para validação real (sem alterar GPKs ou saves)**: Copiada a estrutura legítima de 3 OPs × 5 idiomas (`en`, `es`, `pt-BR`, `ru`, `zh-CN` + 33 `.ass` legados EN na raiz = 198 arquivos de `SchoolDaysHQ-Windows-DIST-3OPs-5Locales-Fallback-QA-20261120/game-assets/forced_subtitles/`) para `/storage/emulated/0/Download/SchoolDaysHQ/game-assets/forced_subtitles/`.
- **Cadeia completa validada**:
  1. `SchoolDaysFolder.java`: detecta `game-assets/forced_subtitles/` (exibindo `forced_subtitles/: OK` na aba **Arquivos**), faz staging por idioma para `<cache>/sd-saf-*/game-assets/forced_subtitles/<locale>/` e injeta `--forced-subtitles <caminho>` nos argumentos nativos.
  2. `games/school-days/src/main.cpp` + `lib/src/core/CommandLine.cpp`: consomem `--forced-subtitles` em `EngineOptions::forcedSubtitlesPath` e chamam `engine.GetForcedSubtitles().setLocale(...)` tanto em modo GPK nativo quanto em modo JSON.
  3. `lib/src/components/ForcedSubtitleRenderer.cpp`: corrigida comparação sensível a maiúsculas na extensão (`.ass` vs `.ASS`) usando `asciiLower(extension) != ".ass"` e normalizando a chave do cache para `<stem>.ass`, garantindo paridade entre `SchoolDaysFolder.java` (case-insensitive) e o scanner C++ em sistemas case-sensitive como Android/Linux.
  4. `lib/src/components/SubtitleScene.cpp` + `SystemFontFile.java`: `libass` usa `/system/fonts/NotoSansCJK-Regular.ttc` (32 MB no POCO X7 Pro), cobrindo acentos PT-BR/ES, cirílico RU e hanzi CJK sem tofu.
- **Testes automatizados**:
  - `Kotonoha.Presentation` (`lib/tests/PresentationTests.cpp`, ampliado com cobertura para extensão `.ASS` maiúscula em diretório explícito por locale), `SchoolDaysPortTest` e `SchoolDaysRuntimeConfigStorageTest` aprovados (3/3).
- **Verificação real em hardware (POCO X7 Pro, `2712×1220`)**:
  - Aba **Arquivos** do launcher confirmou `Pacotes: 30 / 30 (LxH.GPK: OK)`, `Rota (ktnroute): OK`, `styles.ass: OK`, `forced_subtitles/: OK`, `overlay/: OK`.
  - Staging confirmado em `cache/sd-saf-*/game-assets/forced_subtitles/pt-BR/01-01-00-OP1.ass` (33 arquivos).
  - Em gameplay real no nó `01/01-00-OP1` (capturas com HUD `F8` em `70.81s`, `78.16s` e `85.65s`): renderização real de karaokê `\kf` romaji no topo, tradução PT-BR com acentuação na base e posicionamento `\pos(400,316)` credit-safe sobre o vídeo da abertura, com sincronia exata ao clock do nó.
  - Na transição natural de `01/01-00-OP1` para `01/01-00-B05`, as legendas forçadas da OP desapareceram imediatamente sem texto preso.
  - Alternância de `Display subtitles` (`TextView`) `OFF`/`ON` em `OPTION -> General` validada em cena (`allowForcedSubtitles()`), assim como o switch `enableForcedSubtitles` no launcher (aba **Geral**).
- **Classificação**: **Paridade confirmada** (Android DEV e Windows DEV/DIST; Android DIST validado no Marco 4).
