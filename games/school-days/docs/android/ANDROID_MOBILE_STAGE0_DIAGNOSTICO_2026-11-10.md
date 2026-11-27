# School Days HQ Android — Etapa 0 (somente diagnóstico)

Data: 2026-11-10. Escopo: análise estática de código e inventário local; sem execução do jogo, instalação Android ou modificação de saves/GPKs.

## Situação de partida

- Branch: `kotonoha-v3-ruaneri-sync-transitions`. As modificações pré-existentes, **não commitadas**, em `Android/app/src/main/java/org/kotonoha/android/BootstrapActivity.java` e `games/school-days/src/SchoolDaysLauncher.cpp` são de um trabalho anterior GPK/JSON e devem ser preservadas.
- A última compilação anterior foi `assembleSchoolDaysDebug`: isto **não prova** uma distribuição comercial, nem execução no telefone.
- `adb` encontrado; `adb devices` listou `QO5XJZFYDMRWDU6T unauthorized`. Nenhum teste físico executado.

## 1. Reaproveitamento inadequado do PC

1. `games/school-days/src/main.cpp:275-282, 451-477`: Android abre o mesmo `SchoolDays::Launcher` SDL/ImGui do desktop após uma Activity Java de importação. `lib/src/core/Launcher.cpp:412-459` tem campos de overlay, vídeo, upscaling, tradução e Advanced orientados a desktop; a versão School Days esconde parte disso, mas herda a janela e infraestrutura de arquivo.
2. `games/school-days/src/SchoolDaysLauncher.cpp:1748-1798`: tela principal fixa proporções da arte e botões com altura mínima de 35 unidades ImGui; não estabelece alvos em dp ou tamanho físico adequado ao dedo. `games/school-days/src/SchoolDaysLauncher.cpp:1084-1235` já faz alguns ajustes Android, mas os controles físicos e a UI continuam compartilhados com PC.
3. `lib/src/core/Launcher.cpp:291-301`: seletor `SDL_ShowOpenFolderDialog` do desktop; `BootstrapActivity.java` usa SAF separadamente e a aba `Game Files` em `SchoolDaysLauncher.cpp:1919-1931` não permite selecioná-lo ali. O fluxo de importação e a launcher estão fragmentados.
4. `lib/src/core/Engine.cpp:694-707`: atalhos F11 / Alt+Enter e atualização de dimensão de janela permanecem no fluxo genérico; `SchoolDaysRuntime.cpp` inclui ações de teclado, mouse, hover e controles de desktop. `SchoolDaysMenuBarPresenter.cpp:125-189` exige seleção por hover previamente gravada para aceitar cliques.
5. `SchoolDaysLauncher.cpp:1285-1349`: edição manual de paths GLSL e presets de desktop que podem não ser compatíveis com GLES; devem ser filtrados por capacidade real do Android.

## 2. Falha provável do toque — análise estática, não confirmação física

- O SDL3 produz eventos `SDL_EVENT_FINGER_*` normalizados e também pode encaminhar eventos mouse associados. `lib/src/events/InputActions.cpp:27-75` converte dedo em mouse usando `SDL_GetWindowSize`. `lib/src/core/Launcher.cpp:303-324` envia movimento sintético antes do botão ao ImGui. Já `lib/src/core/Engine.cpp:623-656` despacha o botão sintético do dedo **sem enviar movimento prévio**, conforme `runtime.continuousTouch()` e estado de Settings; em outros casos `Engine.cpp:774-789` sintetiza somente um tap ao soltar. Existe potencial de duplicação e ordenação diferente entre telas.
- Causa forte para a barra do jogo: `games/school-days/src/SchoolDaysMenuBarPresenter.cpp:167-185` só ativa um controle se `available == selection_`, sendo `selection_` atualizado por movimento de mouse. Um dedo parado que toca diretamente não necessariamente gerou o movimento que a condição exige. A ação pode ser ignorada mesmo dentro do retângulo correto.
- A cadeia de escala tem duas apresentações: `Engine.cpp:600-621` aplica `SDL_LOGICAL_PRESENTATION_LETTERBOX` no Novel e `STRETCH` no Screen; `Engine.cpp:676-730` converte coordenadas SDL e ajusta escala vertical, depois `UI::toFrame`. O botão móvel manual em `Engine.cpp:732-753` calcula coordenadas de `FINGER_DOWN` por outra via. Isso exige testes para proporções, barras pretas, notch e área útil; não assumir que a conta está correta para todos os aparelhos.
- Já existem `TouchActivation`, `ScrollGesture`, `InputActions` e tratamento de `SDLK_AC_BACK`, mas distribuídos entre engine, runtime, Settings e widgets. `SchoolDaysRuntime::onScreenEvent` (`5199-5299`) diferencia dedo/mouse e suprime parte dos cliques duplos na Display. Não há uma política única de deduplicação/gestos para todas as telas. Toque longo não é uma ação central comprovada.
- Telas a auditar em hardware: Title/New, Save, Load, Config, Replay HScene/PlayData, BackLog, Route Map, opções, escolhas com timeout, Auto, Skip, Exit; não classificá-las como OK apenas pela existência de handlers.

## 3. Entry point e orientação

- `Android/app/src/main/AndroidManifest.xml`: filtro MAIN/LAUNCHER pertence à `BootstrapActivity`, que em `onCreate` copia assets, mostra uma tela de configuração/importação e só então inicia `GameActivity`. `main.cpp` abre a launcher SDL após a segunda Activity.
- As duas Activities têm `android:screenOrientation="sensorLandscape"`, mas não há aqui prova de modo imersivo/cutout seguro nem de testes de 180°. A captura de barras/notch deve usar insets reais do Android e a viewport SDL, não coordenadas absolutas presumidas.

## 4. Separação DEV / distribuição comercial

- `CMakeLists.txt:23-28`: `KOTONOHA_DISTRIBUTION_BUILD` é **OFF por padrão** e incompatível com `KOTONOHA_DEV_CONTROL`. `lib/CMakeLists.txt:145-147,217-218` exclui fontes de dev/define a macro. `main.cpp` e `Engine.cpp` têm guardas `#if !defined(KOTONOHA_DISTRIBUTION_BUILD)`. A UI School Days oculta funções extras conforme plataforma/macro (`SchoolDaysLauncher.cpp:1895,1919-1960`).
- `Android/app/build.gradle` atualmente tem um único eixo de flavors por jogo e tipos `debug/release`. Não define duas edições School Days nem habilita explicitamente a macro comercial. Um APK chamado `debug` não deve ser promovido a distribuição.
- Arquitetura proposta: manter outros jogos intactos; definir flavor School Days de distribuição como padrão, adicionar variante School Days DEV com `applicationId` distinto e define nativo por variante, falhar build em caso de conflito `DISTRIBUTION_BUILD/DEV_CONTROL`. Assets de teste somente em source set/task da DEV; validar o ZIP do APK e o binário final.

## 5. Arte da launcher

- Arquivo: `games/school-days/launcher-art/SchoolDaysHQ-Launcher.png`, PNG 950 × 600 (documentação de staging), 490.250 bytes no disco atual.
- Windows: `games/school-days/CMakeLists.txt:111-134` copia para `assets/SchoolDaysHQ-Launcher.png`; `SchoolDaysLauncher.cpp:1655-1697` busca em `assets/`, `launcher-art/` ou resolver, decodifica via `MediaDecoder::decodePicture` e cria textura SDL; `drawMainScreen` a estica sobre a janela.
- Atualmente o `stageSchoolDaysDebugAssets` do Gradle inclui apenas route/styles, não a arte. Android deve incluí-la como asset da launcher e usar `centerCrop/fitCenter` conscientemente, com área segura para controles.

## 6. Saves, estrutura, endianness e compatibilidade

- A raiz da persistência Android é `getExternalFilesDir(null)/Kotonoha/<packageName>` (fallback `getFilesDir()`), pois `GameActivity.getKotonohaDocumentsDirectory()` alimenta `FileSystem.cpp:275-315` e `directoryLayout():185-190`; `EngineSettings::defaultPath()` põe `settings.json` ali. Na runtime Android a raiz de save também vem de `preferredDocumentsDirectory("school-days")` (`SchoolDaysRuntime.cpp:1065-1078`).
- No Windows comercial, `Config.DAT` fica na raiz da distribuição, e `Save/GlobalFlag.DAT` e `Save/SaveFileNNN.DAT` no diretório configurado por `FILMENGINE.INI`. O perfil valida `SaveFolder`, `SaveFileName`, `FlagFileName` e `SaveConfig` em `SchoolDaysSaveProfile.cpp:42-110`; suporta slots 0..99. Metadados de PlayData/slot residem em FlgH e no SLog.
- `Config.DAT` aceita o contêiner `DFLT` + zlib ou texto do perfil (vide `SchoolDaysConfigStorage.cpp`). `GlobalFlag.DAT` usa `FlgH`, slots usam `SLog`. `SchoolDaysPersistenceCodec.cpp:42-169` implementa varints, U32 little-endian, UTF-16LE/XOR explicitamente, em vez de escrever `struct` nativo x86/x64; logo a arquitetura ARM64 em si **não implica** incompatibilidade. O parser exige versão/estado compatíveis com v1.02 e deve validar cada arquivo antes de importar.
- Metadados extras L×H: `Save/SaveFileNNN.DAT.kotonoha-lxh.v1`. `SchoolDaysSaveProvenance.cpp:87-92,158-163,277-329` vincula SHA-256 do SaveFile + slot + **caminho absoluto normalizado** a um sidecar. Logo copiar o sidecar Windows para outro caminho Android falha a validação. Não regenerar nem ignorar esse vínculo sem decisão expressa; nunca alterar os SaveFile ou sidecars originais.
- A única pasta comercial verificada em leitura, `dist/SchoolDaysHQ-Windows-Clean-Distribution-v3/Save`, possui slots **0, 1 e 2**, cada um com sidecar **lxh=0** (cenas 01/01-1S-P00, 02/02-2S-A00 e 02/02-2S-Q00). **Não atende** ao objetivo de saves já afetados pelos finais L×H; o caminho de origem exigido na especificação ficou em branco.

## Arquitetura em sequência sugerida (Etapas 1–8)

1. **Build**: edição School Days `dist` padrão + `dev` isolada por identificador de pacote, CMake define de distribuição propagado aos targets e checklist do APK sem instrumentos de QA.
2. **Inicialização/launcher**: Activity Android de launcher real (sem tela intermediária), que prepara assets na mesma tela, usa SAF com permissão persistente e UI Android responsiva com imagem local; `GameActivity` só é aberta quando o usuário toca Iniciar. Outros jogos mantêm seu bootstrap.
3. **Traduções**: task Gradle limitada a fonte externa declarada explicitamente, com manifesto de versão/hash, cópia sem modificar originais para o perfil esperado por `SchoolDaysGpkLanguageProfile`, locale/L×H estritos. Packs retail nunca entram no APK.
4. **Display**: controles Android para renderScale/aspect/fps/vsync/upscalers GLES realmente suportados, persistidos por `settings.json`; não expor modos de janela/monitor de desktop.
5. **Entrada**: um adaptador Android no limite SDL->runtime (com código guardado para Android) para deduplicar origem dedo/mouse, converter coordenadas depois de insets/letterbox e mapear tap, long-press, drag e Back; remover dependência de hover na barra e testar matriz das telas.
6. **DEV saves**: identificar origem fornecida, verificar SLog/FlgH/Config e os sete epílogos, importar para perfil DEV em modo exclusivo (`não sobrescrever`). Sidecars vinculados ao caminho exigem política explícita: (a) recriação **somente no app DEV** com prova de integridade e metadados originais validados, ou (b) tratar como `Untrusted` sem simular origem L×H.
7. **Build/QA**: APKs separados, varredura de conteúdos e strings/defines, APK dist sem save/teste/GPK retail, APK dev com fixtures permitidas; dispositivos apenas com `adb` autorizado ou roteiro manual.

## Bloqueios/decisões para prosseguir

1. **Saves L×H**: informar qual pasta local contém os saves de teste; a única candidata inspecionada contém três saves normais, nenhum L×H. Confirmar política para sidecars dependentes de caminho. Não converter ou re-assinar silenciosamente.
2. **Traduções no APK**: indicar a pasta de origem autorizada. Existem cópias organizadas em `Downloads/SchoolDaysHQ-Android-Complementos-2026-11-09/SchoolDaysHQ/translations` e `dist/...-v3/translations`; são alternativas, não uma escolha implícita. Não incluir nenhuma em Git.
3. **Device QA**: `adb` vê o dispositivo como `unauthorized`; a autorização no celular seria necessária antes de testes, sem instalar aplicativos sem pedido.

**Estado final desta etapa**: inspeção estática concluída. Zero testes de jogo em Android; zero arquivos originais alterados. Implementação aguarda resolução dos itens que bloqueiam a Etapa 7, conforme a regra explícita de parar diante de decisão arquitetural.
