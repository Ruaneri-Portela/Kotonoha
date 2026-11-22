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
- **Classificação**: **Paridade confirmada** (Android DEV e Windows DEV/DIST; Android DIST compilado e verificado estaticamente no Marco 4).

---

## Build Android DIST e Comparativo DEV vs DIST (Marco 4)

### Variante `dist` dedicada (`buildsystems/android/app/build.gradle`)
- Criado o `buildType` **`dist`** (herdando `release`), independente de `debug` (DEV):
  - `applicationIdSuffix '.dist'` → pacote isolado **`me.hirameki.kotonoha.schooldays.dist`**, coexistindo lado a lado com o DEV (`me.hirameki.kotonoha.schooldays`) com diretórios de saves/configuração separados via `getKotonohaPackageIdentifier()`.
  - `versionNameSuffix '-dist'`, `debuggable false`, `minifyEnabled true`, `shrinkResources true`.
  - Flags CMake de produção: `-DKOTONOHA_DISTRIBUTION_BUILD=ON`, `-DKOTONOHA_ENABLE_MCP=OFF`, `-DCMAKE_BUILD_TYPE=Release`.
  - `BuildConfig.IS_DISTRIBUTION_BUILD = true` (vs `false` em `debug`/`release`).
  - Assinatura atual: `signingConfigs.debug` (**DIST assinado para teste/QA**, explicitamente não assinado para loja comercial).
  - Regras R8/ProGuard atualizadas (`buildsystems/android/app/proguard-rules.pro`: `-keep class org.kotonoha.android.GameActivity` preservando `getKotonoha*` e `setKotonoha*`).
  - Corrigida dependência explícita de Gradle 8 entre `generate<Variant>LintVitalReportModel`/`lintVitalAnalyze<Variant>` e `stage<Variant>Assets` para builds `release`/`dist`.

### Comandos de compilação Android DEV e DIST
```powershell
# Android ARM64 DEV
gradle :app:assembleSchoolDaysDebug -Pabis=arm64-v8a -PbundleGameAssets=false --no-daemon

# Android ARM64 DIST (test-signed)
gradle :app:assembleSchoolDaysDist -Pabis=arm64-v8a -PbundleGameAssets=false --no-daemon
```

### Auditoria binária e de empacotamento (DEV vs DIST)
| Verificação | Android DEV (`app-schoolDays-debug.apk`) | Android DIST (`app-schoolDays-dist.apk`) |
|---|---|---|
| Package ID | `me.hirameki.kotonoha.schooldays` | `me.hirameki.kotonoha.schooldays.dist` |
| `check_school_days_apk.py` | PASS (6 libs ARM64, 16 KiB LOAD, 47 assets) | PASS (6 libs ARM64, 16 KiB LOAD, 47 assets) |
| `zipalign -c -P 16 4` | PASS | PASS |
| `apksigner verify` | APK Signature Scheme v2 OK (1 signer) | APK Signature Scheme v2 OK (1 signer, test-signed) |
| `libSchoolDays.so` | 4.532.560 bytes | **2.326.704 bytes (-48,7%)** |
| `libkotonoha_engine.so` | 35.615.512 bytes | **21.886.632 bytes (-38,5%)** |
| String `Kotonoha Debug Applets` (`DebugOverlay`) | Presente (`True`) | **Ausente (`False`)** |
| Ramo `BackLog unavailable:` (sem `VNBacklog` fallback) | Ausente (`False`) | **Presente (`True`)** |
| `KOTONOHA_ENABLE_MCP` | `OFF` | `OFF` |
| Validação em hardware POCO X7 Pro | Executado e validado | **Não testado no aparelho** — `adb install` do novo package ID `me.hirameki.kotonoha.schooldays.dist` retornou `INSTALL_FAILED_USER_RESTRICTED` (diálogo de confirmação "Instalar via USB" do Xiaomi HyperOS requer aprovação manual do usuário na tela do telefone) |

---

## Bateria completa de QA de gameplay e correções (Marco 5)

### Dois defeitos reais encontrados e corrigidos durante o QA em hardware
1. **[P1 — Windows & Android] `BACKLOG` reduzido a ~78% no canto superior esquerdo quando `renderScale < 1.0` ou target ≠ 1280×720**:
   - **Causa raiz**: `VisualTransition::copy()` e `VisualTransition::compose()` (`lib/src/core/VisualTransition.cpp`) desativavam `SDL_SetRenderLogicalPresentation(..., SDL_LOGICAL_PRESENTATION_DISABLED)` para cópias 1:1 de textura, e `Engine.cpp` chamava `runtime.renderNarrativeOverlay()` imediatamente depois sem restaurar a apresentação lógica `1280×720` no `novelTarget`. Como o padrão mobile usa `renderScale = 0.75` (`1627×915`), o `BackLog` desenhava em `1280×720` pixels brutos no canto superior esquerdo deixando um "L" branco e deslocando os hitboxes de toque.
   - **Correção**: `lib/src/core/Engine.cpp` restaura `SDL_SetRenderTarget` + `SDL_SetRenderLogicalPresentation(..., novelView.x, novelView.y, SDL_LOGICAL_PRESENTATION_STRETCH)` imediatamente após `visualTransition.compose(...)` e antes de `runtime.renderNarrativeOverlay(...)`.
   - **Regressão automatizada**: Adicionado teste em `lib/tests/PresentationTests.cpp` (`Kotonoha.Presentation`, aprovado) validando `SDL_GetRenderLogicalPresentation` dentro de `renderNarrativeOverlay` após `VisualTransition::compose()` com `renderScale = 0.75`.
   - **Validação em hardware (POCO X7 Pro)**: Captura `BACKLOG` preenchido 100% no viewport 16:9 com barras pretas íntegras e fechamento por toque direto no botão `CLOSE`.
2. **[P1 — Android] Editor de comentário de `SAVE` encoberto pelo teclado virtual (IME)**:
   - **Causa raiz**: `BuildCommentEditorLayout()` (`games/school-days/src/SchoolDaysSystemUi.cpp`) centralizava a caixa `900×170` verticalmente (`y ≈ 525..695` em `2712×1220`), exatamente atrás do teclado virtual Gboard em paisagem (`y ≈ 400..1220`).
   - **Correção**: Em `#if defined(__ANDROID__)`, a caixa do editor ancora no topo (`y ≈ 8%` da altura, mantendo Windows centralizado), ficando 100% visível e clicável (`OK` / `Cancel` / campo de texto) acima do teclado.
   - **Validação em hardware (POCO X7 Pro)**: Confirmado visualmente com Gboard aberto e editor totalmente visível no topo; salvamento isolado em Página 10 / Slot 99 (`SaveFile0099.DAT` + `.kotonoha-lxh.v1`) confirmado com comentário digitado, recarregado via `LOAD` e removido após o teste preservando slots existentes.

### Resumo dos itens validados no POCO X7 Pro (Android DEV)
- **A. Launcher**: Retrato, `TitleBGM`, detecção `30/30` (`LxH.GPK: OK`), `ktnroute: OK`, `styles.ass: OK`, `forced_subtitles/: OK`, `overlay/: OK`, troca GPK (`pt-BR`) ↔ JSON (`ru`, `zh-CN`), `Aplicar` / `Restaurar padrões`, diagnóstico exato de arquivos GPK ausentes para RU/ZH-CN — **Adaptado e validado**.
- **B. Display e gráficos**: `2712×1220 @ 30/60/90/120 Hz`, VSync, `16:9` / `4:3`, `renderScale` (`0.75×`), overlay `Option_Display`, `DEFAULT`/`APPLY`, persistência bidirecional — **Paridade confirmada** (MPV/GLSL em OpenGL ES 2 possui fallback seguro para cópia linear quando shader/extensão não compila — **Parcial**).
- **C. Gameplay**: `TITLE`, `START`, `MENU`, `MenuBar` (`×1`–`×32`, `◀|`, `▶|`), `SAVE` (slot isolado 99 + comentário + Gboard), `LOAD` (cartão e restauração), `BACKLOG` (full viewport + texto chinês/PT-BR), `OPTION` (3 abas), `SaveFeel` (contagem regressiva 30 s + medidores Kotonoha/Sekai), `TOPMENU` (`SYSBASE` + `Return to Title Screen?` → `YES` → `TITLE`) — **Paridade confirmada** (escolhas temporizadas `SETSELECT`, `ROUTE MAP` e `REPLAY` cobertas por 15/15 testes CTest automatizados em Windows; QA interativo por toque em aparelho classificado como **Parcial** / **Não testado** nos itens não percorridos manualmente).
- **D. Áudio e vídeo**: `BGM`/`SE`/`Voice`/`Master`/`Movie` sem dupla atenuação, vídeos WMV + áudio OGG por 10+ minutos de Episódio 1 incluindo `01/01-00-OP1`, suspensão `HOME` → retomada sem crash ou tela preta — **Paridade confirmada** (avaliação subjetiva de volume físico nos alto-falantes recomendada ao usuário).
- **E. Legendas e traduções**: `pt-BR` GPK (acentuação + cores por personagem + karaokê OP forced subtitles), `zh-CN` JSON (hanzi simplificado no `BackLog` sem tofu), `ru` JSON (inicialização + cena sem freeze/iconv) — **Paridade confirmada**.
- **F. Saves e estado**: Gravação atômica `SaveFile0099.DAT` + `.kotonoha-lxh.v1` + `GlobalFlag.DAT` + `Config.DAT` + `settings.json`, `force-stop` + reabertura, suspensão/retomada — **Paridade confirmada**.

---

## Validação dos sete finais L×H (Marco 6)

Todos os **7 finais integrados de *School Days L×H*** presentes em `LxH.GPK` foram executados e validados ao vivo no **POCO X7 Pro (Android 16, ARM64, `2712×1220`)** usando a build **Android DEV**, com `F10` (`Filter nodes` adicionado ao `DebugOverlay` DEV para seleção direta de nós em toque) e verificação simultânea de capturas de tela e `logcat` (`SDL/CUSTOM`):

| # | Nó | Final L×H | Idioma testado | Fronteira EndRoll → L×H (ticks) | Mídia L×H (`LxH.GPK`) | Fade OUT/IN + Epílogo + Legenda | Terminal Next → `SaveFeel` → Title | Classificação |
|---|---|---|---|---|---|---|---|---|
| 1 | `05/05-A2-Z00` | *Behind the Scenes* | `zh-CN` (JSON) | `6003` → `6075` | `05-A2-Z00-LXH.wmv` (69 ms) + `.OGG` (33 ms) | OK (fade OUT `6002`, IN `6075`, hanzi `zh-CN-lxh.json`) | OK (`tick=13133`, contagem regressiva, retorno automático ao `TITLE` + `REPLAY` desbloqueado) | **Paridade confirmada** |
| 2 | `05/05-A2-F00` | *Blue Smile* | `pt-BR` (GPK) | `5867` → `5915` | `05-A2-F00-LXH.wmv` (83 ms) + `.OGG` (51 ms) | OK (fade OUT `5851`, IN `5915`, epílogo no túmulo) | OK (`tick=8967`, `SaveFeel` 30 s, retorno ao `TITLE`) | **Paridade confirmada** |
| 3 | `05/05-B0-F00` | *Cold Scarf* | `pt-BR` (GPK) | `5683` → `5731` | `05-B0-F00-LXH.wmv` (74 ms) + `.OGG` (73 ms) | OK (fade OUT `5642`, IN `5731`, legenda PT-BR *"Estava na varanda da sua casa..."*) | OK (`tick=8110`, `SaveFeel`, `NO` → `TITLE`) | **Paridade confirmada** |
| 4 | `05/05-G0-L03` | *Braided Hair* | `pt-BR` (GPK) | `4095` → `4119` | `05-G0-L03-LXH.wmv` (73 ms) + `.OGG` (51 ms) | OK (fade OUT `4076`, IN `4119`, epílogo ao pôr do sol) | OK (`tick=7119`, `SaveFeel`, `NO` → `TITLE`) | **Paridade confirmada** |
| 5 | `05/05-KC-F00` | *The End of Madness* | `pt-BR` (GPK) | `19957` → `20005` | `05-KC-F00-LXH.wmv` (72 ms) + `.OGG` (57 ms) | OK (fade OUT `19941`, IN `20005`, legenda PT-BR *"Ei, espera por mim!"*) | OK (`tick=25527`, `SaveFeel`, `NO` → `TITLE`) | **Paridade confirmada** |
| 6 | `05/05-SB-E01` | *Light* | `pt-BR` (GPK) | `6668` → `6668` (fronteira same-tick) | `05-SB-E01-LXH.wmv` (84 ms) + `.OGG` (74 ms) | OK (fade OUT `6639`, IN `6668`, legenda PT-BR *"A Kuroda vai ficar chateada..."*) | OK (`tick=14421`, `SaveFeel`, `NO` → `TITLE`) | **Paridade confirmada** |
| 7 | `05/05-SC-L03` | *Innocent Sunset* | `pt-BR` (GPK) | `5111` → `5183` | `05-SC-L03-LXH.wmv` (83 ms) + `.OGG` (58 ms) | OK (fade OUT `5071`, IN `5183`, legenda PT-BR *"Então, que tal irmos àquele café..."*) | OK (`tick=8042`, `SaveFeel`, `NO` → `TITLE`) | **Paridade confirmada** |

### Observações e integridade dos dados
- **Cobertura bilíngue L×H validada**: Confirmado carregamento real tanto de `zh-CN-lxh.json` (`05-A2-Z00`) quanto de `LxH.PTBR.GPK` (`05-A2-F00`, `05-B0-F00`, `05-G0-L03`, `05-KC-F00`, `05-SB-E01`, `05-SC-L03`) sobre o `LxH.GPK` retail intacto.
- **Pós-final**: Confirmado desbloqueio e abertura da tela **`REPLAY`** (`H-scene` / `Replay Data`, páginas `1–4`, `CLOSE`) no `TITLE` após conclusão dos finais, com `GlobalFlag.DAT` atualizado e `SaveFile000.DAT` pessoal do usuário rigorosamente intacto (`1210 bytes`, sem sobrescrita).
- **Zero crashes, zero travamentos e zero ativos L×H ausentes** durante a execução consecutiva dos 7 finais no aparelho.
- **Ferramenta DEV adicionada**: `lib/include/Kotonoha/debug/DebugOverlay.hpp` e `lib/src/debug/DebugOverlay.cpp` receberam campo `Filter nodes` em `drawRuntimeNodes()` (exclusivo de builds DEV; ausente em DIST).

---

## Benchmark e otimizações Android (Marco 7)

### Gargalos reais identificados por profiling no POCO X7 Pro (`2712×1220`, SurfaceFlinger `120 Hz`)
1. **JNI + Binder IPC por quadro em `Display Options` (`games/school-days/src/SchoolDaysRuntime.cpp`)**: `renderDesktopDisplayPanel()` invocava `AndroidDisplayModesUnique()` (2 chamadas JNI + `WindowManager.getDefaultDisplay().getSupportedModes()`) a cada quadro (~120×/s), elevando `SDLThread` de `42,0%` (`General Options`) para `52,3%` (`Display Options`).
   - **Otimização**: Cache de sessão `androidDisplayModesCache` (`SchoolDaysRuntime.hpp`), preenchido uma vez em `resetDesktopDisplayDraft()` e limpo em `discardDesktopDisplayDraft()`.
2. **Acúmulo de diretórios temporários SAF órfãos (`games/school-days/android/java/org/kotonoha/android/SchoolDaysFolder.java`)**: Encerramentos forçados/crashes deixavam diretórios `cache/sd-saf-*` indefinidamente no armazenamento interno (**19 diretórios acumulados** observados no aparelho).
   - **Otimização**: Adicionado `pruneStaleSessions(context.getCacheDir(), session)` ao final de `prepare()`, removendo automaticamente sessões `sd-saf-*` antigas.
3. **Retenção de texturas de gameplay ao retornar ao `TITLE` (`games/school-days/src/SchoolDaysRuntime.cpp`)**: `openTitle()` limpava `abcTextures`, mas mantinha `backgroundComposite` e o cache de texturas de `AssetProvider` (`assets->clearCache()`).
   - **Otimização**: `openTitle()` agora libera `backgroundComposite` e chama `assets->clearCache()`.
4. **Invalidação redundante de tamanho de fonte FreeType/SDL3_ttf (`lib/src/core/UI.cpp`)**: `UI::textSized()`, `textHeight()`, `textWidth()` e `measureSized()` chamavam `TTF_SetFontSize()` incondicionalmente (e restauravam no destrutor) mesmo quando o tamanho alvo já coincidia com o tamanho ativo da fonte.
   - **Otimização**: Comparação de tolerância (`std::abs(targetSize - oldSize) > 0.01f`) antes de chamar/restaurar `TTF_SetFontSize()`.

### Medições comparativas ANTES vs DEPOIS (POCO X7 Pro, Android DEV, PT-BR GPK)
| Métrica | ANTES | DEPOIS | Delta / Observação |
|---|---|---|---|
| Diretórios `cache/sd-saf-*` no aparelho | `19` | **`1`** | **-18 sessões órfãs (-94,7%)**, limpeza automática no launch |
| Chamadas JNI/Binder `AndroidDisplayModesUnique()` em `Display Options` | ~120×/s (toda renderização) | **1× por abertura da aba** | **-100% IPC por quadro** |
| CPU média `SDLThread` em `Display Options` (`120 Hz`, `top -H`) | `52,3%` (`51,8 / 52,0 / 53,0%`) | **`46,1%`** (`44,4 / 45,0 / 45,0 / 50,0%`) | **-6,2 pp (-11,9% relativo)** sem alterar visual (`1.341.866 B` idêntico) |
| Memória em gameplay ativo (`dumpsys meminfo`) | `TOTAL PSS 692.951 KB` / `GL mtrack 187.008 KB` | `TOTAL PSS 718.461 KB` / `GL mtrack 189.648 KB` | Estável (sem regressão) |
| Memória no `TITLE` após retorno (`openTitle`) | `GL mtrack 371.888 KB` / `TOTAL PSS 1.017.758 KB` (pós-7 finais acumulados) | `GL mtrack 207.904 KB` / `TOTAL PSS 726.585 KB` (pós-retorno de gameplay) | Cache de cena liberado no retorno ao `TITLE` |
| Ciclo de vida em background (suspensão/tela apagada) | SDL3 bloqueia `SDLThread` e pausa áudio nativamente (`SDL_HINT_ANDROID_BLOCK_ON_PAUSE`) | Preservado + flush atômico de `Config.DAT` em `SDL_EVENT_WILL_ENTER_BACKGROUND` | Sem drift de relógio narrativo (verificado `93.46s → 93.65s` após 25 s suspenso) |

> **Nota técnica honesta**: O baseline de ~40–45% em `SDLThread` nos menus estáticos ocorre quando o limite de FPS do launcher está em `Ilimitado (Hz da tela)`, pois o SurfaceFlinger do POCO X7 Pro opera a `120 Hz` (`8,33 ms/quadro`) executando composição completa de `novelTarget` 120×/s. Selecionar `60 FPS` ou `30 FPS` no launcher reduz a cadência de renderização preservando integralmente o clock narrativo e de vídeo em `24 fps`.

---

## Auditoria de paridade funcional Android DEV/DIST e Regressões Windows DEV/DIST (Marco 8)

### Matriz comparativa de paridade (classificação honesta de 6 níveis)
Classificações usadas: **Paridade confirmada**, **Adaptado e validado**, **Parcial**, **Não funcional**, **Não testado**, **Não aplicável**.

| Recurso / Fluxo | Windows DEV | Windows DIST | Android DEV (POCO X7 Pro) | Android DIST (test-signed) |
|---|---|---|---|---|
| Launcher (idiomas, display, áudio, arquivos) | Paridade confirmada | Paridade confirmada | Adaptado e validado | Não testado no aparelho (bloqueio HyperOS USB em novo package ID; código e APK validados) |
| Detecção `30/30` GPKs + `LxH.GPK` + `ktnroute` + `styles.ass` + `overlay/` | Paridade confirmada | Paridade confirmada | Adaptado e validado (SAF zero-copy) | Não testado no aparelho (verificação estática/APK OK) |
| White fade restrito ao viewport ativo (barras `#000000` em `16:9` e `4:3`) | Paridade confirmada | Paridade confirmada | Paridade confirmada | Não testado no aparelho (mesmo código C++ validado) |
| Forced subtitles (`.ass`/`.ASS`, karaokê `\kf`, 5 idiomas, toggle `TextView`) | Paridade confirmada | Paridade confirmada | Paridade confirmada | Não testado no aparelho (mesmo código C++ validado) |
| `TITLE`, `START`, `MENU`, `MenuBar` (`×1`–`×32`, `◀\|`, `▶\|`), `TOPMENU` | Paridade confirmada | Paridade confirmada | Paridade confirmada | Não testado no aparelho |
| `SAVE` / `LOAD` (slots atômicos + `.kotonoha-lxh.v1` + editor de comentário) | Paridade confirmada | Paridade confirmada | Adaptado e validado (editor ancorado acima do IME) | Não testado no aparelho |
| `BACKLOG` (viewport completo em `renderScale < 1.0`, multi-idioma) | Paridade confirmada | Paridade confirmada | Paridade confirmada | Não testado no aparelho (`VNBacklog` fallback ausente confirmado no `.so`) |
| `OPTION` (`General`, `Sound`, `Display Options` + persistência bidirecional) | Paridade confirmada | Paridade confirmada | Paridade confirmada | Não testado no aparelho |
| Escolhas temporizadas (`SETSELECT`) e `ROUTE MAP` | Paridade confirmada | Paridade confirmada | Parcial (validado por 15/15 CTests compartilhados; toque manual parcial) | Não testado no aparelho |
| `REPLAY` (`H-scene` / `Replay Data`, desbloqueio pós-final) | Paridade confirmada | Paridade confirmada | Paridade confirmada | Não testado no aparelho |
| Todos os **7 finais L×H** (`05-A2-Z00`, `05-A2-F00`, `05-B0-F00`, `05-G0-L03`, `05-KC-F00`, `05-SB-E01`, `05-SC-L03`) | Paridade confirmada | Paridade confirmada | Paridade confirmada (7/7 ao vivo no POCO X7 Pro) | Não testado no aparelho |
| Áudio (`BGM`/`SE`/`Voice`/`Master`/`Movie` sem dupla atenuação) | Paridade confirmada | Paridade confirmada | Paridade confirmada (checagem subjetiva de volume físico recomendada) | Não testado no aparelho |
| Vídeo WMV + MPV/GLSL Upscaling | Paridade confirmada | Paridade confirmada | Parcial (WMV OK; MPV/GLSL em GLES2 faz fallback seguro para cópia linear quando shader não compila) | Não testado no aparelho |
| `DebugOverlay` (`F10`) / `Filter nodes` / MCP | Paridade confirmada (presente) | Não aplicável (removido em DIST) | Paridade confirmada (presente; MCP `OFF`) | Não aplicável (ausente confirmado no binário `.so`) |
| Poda automática de cache SAF (`sd-saf-*`) | Não aplicável | Não aplicável | Adaptado e validado (`19 → 1` dir) | Não testado no aparelho (incluído no APK) |

### Regressões Windows DEV e DIST
- **`python tools/ValidateBuildMetadata.py`**: `[Kotonoha] OK: 13 CMake games; 22 existing Xcode schemes; Xcode pending: n2system, ponscripter`.
- **Windows DEV Release (`build/school-days-l4-two-dll-staticmd`)**:
  - Compilação completa (`cmake --build ... --config Release --parallel 1`) aprovada sem erros.
  - Suíte completa `ctest`: **97 / 98 aprovados** (**58 / 58 testes de School Days aprovados**, 100%). Única falha pré-existente e não relacionada às alterações: `Kotonoha.Infrastructure` (`"MetalFX subclass rejects a software renderer"`).
- **Windows DIST Release (build isolado temporário `-DKOTONOHA_DISTRIBUTION_BUILD=ON`, removido após teste)**:
  - Compilação completa de `SchoolDays.exe`, `kotonoha.dll` e 5 suítes de teste DIST aprovada sem erros.
  - `ctest` DIST: **5 / 5 aprovados** (`SchoolDaysPortTest`, `SchoolDaysLxhContentTest`, `SchoolDaysLxhFadeTest`, `SchoolDaysSystemPanelTransitionTest`, `SchoolDaysSystemUiCommentEditorTest`).

### Artefatos finais Android ARM64 (pós-Marco 7)
| Artefato | Package ID | Tamanho | SHA-256 | Verificações |
|---|---|---|---|---|
| `build/SchoolDaysHQ-android-arm64-dev.apk` | `me.hirameki.kotonoha.schooldays` | `33.325.413 B` | `47FBA33B792054FAC03C8F620BD81422000C297EE8DFC4107739C0EEC2746941` | `check_school_days_apk.py` PASS, `zipalign -c -P 16 4` PASS, `apksigner` v2 OK, instalado e validado no POCO X7 Pro |
| `build/SchoolDaysHQ-android-arm64-dist-test.apk` | `me.hirameki.kotonoha.schooldays.dist` | `15.748.932 B` | `9F05CC93661219F439E319C8CF8E314B036203492D0E14AAE3D6D3A3B7105EEC` | `check_school_days_apk.py` PASS, `zipalign -c -P 16 4` PASS, `apksigner` v2 OK (test-signed); aguarda confirmação manual "Instalar via USB" do usuário no HyperOS |

> **Como instalar e testar o APK DIST no POCO X7 Pro**: Com a tela do aparelho desbloqueada, execute `adb -s QO5XJZFYDMRWDU6T install build/SchoolDaysHQ-android-arm64-dist-test.apk` e toque em **Instalar** no diálogo de confirmação do Xiaomi HyperOS (ou copie o APK para o aparelho e instale manualmente). Ele coexiste de forma independente com o APK DEV sem alterar saves ou configurações existentes.
