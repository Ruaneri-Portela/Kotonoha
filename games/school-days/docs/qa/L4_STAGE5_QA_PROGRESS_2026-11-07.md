# School Days HQ L4 — Etapa 5 / Windows: evidências e pendências (2026-11-07)

## NOVA DECISÃO DE ENTREGA (07/11/2026, posterior à integração GPK)

O usuário **dispensou novo QA físico interno** dos 14 finais L×H e
preferiu entregar uma build L4 testável à comunidade para receber relatos
de defeitos posteriores. **0/14 significa não certificado fisicamente no
GPK integrado, não uma falha comprovada dos finais.** Manter testes de
compilação, integração de pacote e smoke headless; não voltar a abrir GUI
automatizada nem usar debugJump para fabricar certificação.

Release alvo: `build/school-days-dev-control4/games/school-days/Release`.
Limpeza auditada: `display-options/Script/English` estava **vazio**, e
`display-options/LxH` já não existia. Removida a árvore vazia Script e o
manifesto de 28 arquivos loose obsoleto; `System/Option` permanece intacto
e os 28 recursos L×H estão somente em `Packs/LxH.GPK`. Backup privado
em `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-release-cleanup-backup-20261107`.

Idioma inicial definido para **English** no `settings.json`
(`translationPath` vazio) e `language.txt` (`english`). ES posterior via
launcher existente `SchoolDays.exe --launcher`; traduções ES/L×H JSON
permanecem instaladas. Sete saves originais **já presentes e autenticados**
por SLog/GlobalFlag em `Release/Save`: slots 0=A2-Z00, 1=A2-F00,
2=G0-L03, 12=KC-F00, 13=SC-L03, 17=SB-E01, 19=B0-F00.
Cópia de segurança SHA256 em
`build/school-days-dev-control4/backup-lxh-seven-saves-20261107/Save`.
Não criar sidecar L×H para saves comerciais legacy nem alterar GlobalFlag;
reprodução desde cada save ainda será observada pelo usuário/comunidade.

**Pedido das duas DLLs ENTREGUE:** novo binário compilado no build isolado
`build/school-days-l4-two-dll-staticmd`, MSVC/`x64-windows-static-md`,
`KOTONOHA_STATIC_ENGINE=ON`, `KOTONOHA_STATIC_IMGUI=ON`,
`KOTONOHA_DEV_CONTROL=OFF` e SDL3/SDL3_ttf compartilhadas. Publicados
somente `SchoolDays.exe`, `SDL3.dll` e `SDL3_ttf.dll` no diretório L4
acima. Auditoria `dumpbin /dependents`: **exatamente duas DLLs locais**;
demais imports são Windows/MSVC Runtime, sem DLLs extraídas temporariamente.
EXE publicado SHA256
`31e8cf33e7f6215ed71dfd7e72b00f0e0104462afc0c30c1ae05328545fa8316`.
Antigo executável + **29 DLLs** com backup em
`build/school-days-dev-control4/backup-old-runtime-dlls-20261107`.

Validação da mesma variante: `ALL_BUILD Release` **PASS**; `CTest
--parallel 1 -C Release` **67/67 PASS (71,24 s)**, `SchoolDaysLxhPrivateMountProbe`
em 30 GPKs reais **PASS**, smokes headless **EN e ES exit0** na
Release publicada com `--documents` separados e SDL dummy/software,
logs em `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-published-two-dll-smoke-20261107`.
Após publicação, `Packs/LxH.GPK` mantém SHA256
`772b501fc70ae89e52861378a99ff1b27d3c47eb20b2473836591069143edb64`;
**sete saves e GlobalFlag preservaram hash** conforme backup.
Avisos e licenças de terceiros em `Release/third-party-licenses/`.
**Ainda 0/14 finais físicos certificados**, conforme dispensa do usuário;
testes futuros pertencem à comunidade. Uma recompilação com o preset antigo
`build/school-days-dev-control4` ainda usa dependências dinâmicas antigas;
para futuras builds compactas usar `build/school-days-l4-two-dll-staticmd`
e publicar somente após validar, sem executar build antigo sobre a Release.

## NOTA DE REPLANEJAMENTO APROVADO PELO USUÁRIO (07/11/2026)

L×H agora será **integrado sem toggle**, por `Packs/LxH.GPK` novo,
preservando 29 GPKs comerciais. A matriz legada ON/OFF 8/28 registrada
abaixo é **histórica**, não representa o novo gate. Cinco dos oito
percursos completados tinham L×H ON (ENDG5 EN, ENDG6 EN/ES,
ENDG7 ES, ENDG8 EN); somente quatro desses cinco foram 100% SDL/timeout.
Os três OFF são regressões úteis do HQ base, mas deixam de ser cenários
do produto. Matriz nova **14 = 7 finais × EN/ES**, com **0/14
revalidados no novo GPK**; cinco baselines ON podem orientar a campanha.
PlayData físico com save real, ReplayMode e retorno ao título está PASS
e continua válido. O teste legado de warning DLC ON→OFF não deve ser
retomado; o protótipo físico interrompido antes do epílogo não criou
save e não provou o warning. Ver plano atualizado no documento
`../planning/PLANO_DEFINITIVO_L4_LXH_IDIOMAS_LAUNCHER_2026-11-07.md`.

**Integração compilada na L4:** `Packs/LxH.GPK` novo com 28/28 recursos
round-trip, SHA256
`772b501fc70ae89e52861378a99ff1b27d3c47eb20b2473836591069143edb64`.
`SchoolDaysAssetMount` obriga o GPK adicional e dá prioridade aos sete
ORS e 21 mídias do pacote; não usa ON/OFF. Os 28 recursos L×H que
restavam soltos em `Release/display-options` foram movidos após
conferência de hashes para backup scratch, conservando `System/Option`.
`school_days` e `SchoolDays` Release PASS, EXE SHA256
`D05BA329E30AF2F32DA03ABEDB8D7E6044265C251C8047D26C12DC99C8EB6393`.
EN e ES `--frames 5` SDL dummy, sem `--node`, ambos exit0 **após**
remover as cópias L×H soltas. Evidência privada
`D:/Dev/_temp_lxh_static_audit_worker8/qa-integrated-lxh-packonly-headless-20261107/`.
São apenas smokes de inicialização: matriz física integrada segue
**0/14**, e não substituem QA AV/Save/Replay.

**Atualização de regressão do GPK integrado (07/11, após a primeira build):**
o CTest completo inicial da migração ficou em **64/67** (falhas em
`SchoolDaysGpkStreamingTest`, `SchoolDaysRuntimeSmoke` e
`SchoolDaysLauncherTest`). Os dois últimos passaram após preservar a
compatibilidade de fixtures ORS extraídas sem enfraquecer o contrato das
instalações com Packs; `SchoolDaysPortTest` também passou. O teste de
streaming encontrou um bypass da prioridade GPK pelo `AssetProvider`
via overlay e a resolução de nomes `.png.PNG`. Ambos foram corrigidos
**somente em `games/school-days`**; a última execução isolada de
`SchoolDaysGpkStreamingTest` terminou **1/1 PASS**.

`SchoolDaysLxhPrivateMountProbe.exe` foi recompilado e executado
somente leitura contra os **30 GPKs reais da Release** e o overlay
`display-options`: **PASS** para sete ORS grafted, contagem/tempos
retail→L×H, 21 PNG/WMV/OGG com assinatura válida, raw streaming e seek,
prioridade sobre `Script.GPK` e preservação da opção visual.
`LxH.GPK`: 237.329.938 bytes, SHA256
`772b501fc70ae89e52861378a99ff1b27d3c47eb20b2473836591069143edb64`;
30 GPKs presentes. `git diff --check` PASS e nenhuma referência funcional
ao toggle antigo no source/include/tests. **CTest serial completo dos
binários de teste disponíveis: 67/67 PASS (70,74 s)**, incluindo
SchoolDaysRuntimeSmoke, Launcher, GpkStreaming, SaveProvenance, Replay e
PlayData; substitui o resultado intermediário 64/67 acima.
Durante aquela rodada havia um `SchoolDays.exe` aberto pelo Explorador
(PID 17220, sem argumentos); foi preservado até sua saída natural.
O hash antigo do EXE acima é histórico, não o hash final.

**Fechamento da build e regressão integrada (07/11/2026):** após a
janela ser fechada, `cmake --build ... --config Release --target ALL_BUILD
--parallel 2` terminou **PASS**. Novo `ctest -C Release
--output-on-failure --parallel 1` sobre os binários finalizados:
**67/67 PASS (73,44 s)**. EXE final
`D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release/SchoolDays.exe`,
SHA256 **`3B9602AD38AEA9C7652845E1C670B286BC8D1493E653372FEDF082F8459C29A5`**.
O `Packs/LxH.GPK` manteve o mesmo SHA256 `772b501fc70ae89e52861378a99ff1b27d3c47eb20b2473836591069143edb64`.
Smokes headless **EN/ES exit0**, SDL dummy/software, `--frames 5`,
`--documents` novos e privados, sem `--node`, usando somente o pack
integrado (sem assets L×H loose). Logs em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-integrated-lxh-final-smoke-20261107-160326/`.
**Matriz física natural do novo GPK permanece 0/14**: build, CTest,
probe e smokes não substituem os sete finais EN × sete ES com vídeo/áudio
natural, escolhas por UI, Save/Load/GlobalFlag/Replay/PlayData.

---

## Escopo e grau da prova

Build única de trabalho:
`D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release`.
Somente o executável **Kotonoha compilado** foi executado, nunca
`SchoolDays.exe` comercial. Cenários usam cópia de EXE/DLL em scratch e
`--documents` privados; GPK/ORS/WMV/OGG/PNG, saves reais e JSONs instalados
são somente leitura. Sem commit/push nem Android.

**Etapa 5 ainda não fechou o gate de 28 finais naturais com audiovisual 1×.**
Há evidência independente de KTRF, cena inicial, mídia, traduções e uma
reprodução contínua completa de epílogo. Não confundir as camadas abaixo.

## Compilação e regressão depois de Etapa 4

- Release MSVC/MSBuild, alvo `ALL_BUILD`: **PASS**.
- CTest serial de School Days: **42/42 PASS**, incluindo
  `SchoolDays.SaveProvenance`, idiomas, Choice, Replay/PlayData, media,
  SaveFeel, fades, Saves, Launcher.
- CTest serial genérico do engine: **21/21 PASS**.
- Quatro inicializações headless após sidecar
  (`en/es × lxh on/off`, `--documents` isolados): **4/4 PASS**.
- Última conferência pós-build: 8/8 fingerprints protegidos (incluindo
  `Script.GPK` e baseline de saves QA), manifesto L×H 28/28, seis JSONs de
  traduções 6/6, 29 GPKs presentes, `git diff --check` PASS.
- Implementação da Etapa 4 documentada separadamente em
  `L4_SAVE_PROVENANCE_STAGE4.md`. Saves legacy sem sidecar continuam ALLOW.

## Matriz estática/headless dos sete finais

Relatório:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-worker12-20261107/QA_STAGE5_WORKER12.md`.

- **28/28** combinações `EN/ES × OFF/ON × 7 cenas` em cópia stage,
  SDL dummy e cinco frames; `engine-ready`, primeiro quadro e exit0 em
  todos. Os 14 ON armam fade L×H, os 14 OFF não armam. São somente
  smoke/debugJump `--node`, **não** os 28 finais naturais.
- **273/273** traduções contextuais vinculadas aos sete ORS nativos,
  zero chaves extras/faltantes e identidades revisadas (EN 20, ES 264,
  nove correções ES V4b).
- **28/28** hashes dos recursos, 7/7 PNG ingleses de 800×452.
- **14/14** WMV/OGG inteiros decodificados pelo FFmpeg sem erro:
  WMV3 800×452 24fps e Vorbis 44,1k estéreo.
- Sete títulos da arte inglesa: Blue Smile; Behind the Scenes; Cold Scarf;
  Braided Hair; The End of Madness; Light; Innocent Sunset.

### Particularidade de áudio SB-E01

OGG 323,727 s versus WMV 316,999 s, diferença **+6,728 s**.
Análise `ffmpeg -af volumedetect` *somente leitura*:
o trecho 300–316 s do OGG teve média **−37,6 dB** e pico **−7,4 dB**;
o trecho 316,999–323,727 s teve média **−91,0 dB** e pico
**−80,8 dB**. É uma cauda praticamente silenciosa, não conteúdo audível
equivalente ao trecho anterior. Isso **não prova sincronização física**
no mixer do Kotonoha; escuta/loopback real ainda necessários.

## Rotas KTRF sem debugJump — sete witnesses

Relatório privado:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-natural-worker12-20261107/QA_STAGE5_NATURAL_ROUTE_WORKER12.md`.

O `KTRF` v3 atual possui SHA256
`557830833AA3BB4A8EFBA82FE75964211491E9F02D60D6090DDA1D5174D4E745`,
igual ao KTRF usado no inventário anterior. Reexecutados **7/7**
witnesses nativos `ResetNewGame → CommitChoice → Advance`, sem
`debugJump`, com `ENDG`/flags de final autenticados. Fixture Runtime
SDL dummy antiga fez 7/7 `LEGIT_TERMINAL_FOUND`,
`LEGIT_TERMINAL_DISK_SAVE_PASS` e `LEGIT_REPLAY_DATA_PASS` (isolados):

| code KTRF | final ORS | escolhas normais | timeouts `-1` |
|---|---|---:|---:|
| 5 | 05-A2-Z00 | 36 | 0 |
| 6 | 05-A2-F00 | 37 | 0 |
| 7 | 05-B0-F00 | 52 | 0 |
| 8 | 05-G0-L03 | 37 | 15 |
| 10 | 05-KC-F00 | 44 | 14 |
| 15 | 05-SB-E01 | 55 | 0 |
| 17 | 05-SC-L03 | 48 | 17 |
| **Total** | **7/7** | **309** | **46** |

**Limite:** fixture Runtime antiga usa ORS sintéticas, não mede os
timestamps de `SetSELECT` da ORS real nem a UI comercial ou a mídia.
Os 46 `timeout=-1` são outcomes válidos do KTRF, mas dependem de
comprovar temporização real das escolhas com Auto OFF.

## Capturas físicas com renderizador software

Back-end `d3d11` solicitado explicitamente no build atual respondeu
`d3d11 not available`, portanto não houve prova D3D11. Em vez disso,
o backend **software** abriu janela **1366×768**, `renderScale=1`,
sem SDL dummy, áudio lógico ativo; toda execução usou
`--documents` novo, cópia stage do `SchoolDays.exe` e DevBridge
TCP loopback com `capture_frame`.

- **7/7 entradas reais** dos sete vídeos L×H: `hasVideoFrame=true`,
  `playbackRate=1.0`, áudio lógico com RMS diferente de zero,
  `gameState=Gameplay`, capturas PNG; sem crash.
  Evidência `qa-l4-physical-six-epilogues-20261107-20261107-015048-5884627/results.json`,
  complementada por `05-A2-F00` em
  `qa-l4-physical-lxh-entry-20261107-20261107-014634-1661533/results.json`.
- No `05-A2-F00`, duas capturas **reais** do mesmo trecho a ~tick6230
  mostraram **“You're here...”** (EN) e **“Estás aquí..”** (ES),
  mantendo a identidade de speaker/cor.
  Evidência:
  `qa-l4-physical-distinct-subs-20261107-20261107-014850-7822775`,
  PNGs em `en/distinct_lxh_cue.png` e `es/distinct_lxh_cue.png`.
- Em `05-B0-F00`, foi reproduzido o **epílogo completo a 1×** após
  a entrada pela cena `--node` e um único `Right` para saltar os
  créditos originais. Capturas de vídeo aproximadamente nos quartos,
  metade, três quartos e final; `Next` no tick8107, logo na saída
  do episódio. `gameState` passou de 2 para 1 após **31,397 s** desde o
  primeiro Next observado (countdown de 30 s + fades/transição).
  Evidência em
  `qa-l4-one-complete-epilogue-20261107-20261107-015653-9039242/results.json`
  e `engine.log`, com `countdown fade OUT` e `countdown fade IN`.
- Primeira captura B0 no tick7955 foi preta porque o frame terminal do
  WMV ainda cobria o `CREATE_BG`. Captura posterior no **tick8005**
  revelou explicitamente o PNG inglês **“Cold Scarf”**; no tick8038
  a mesma arte estava progressivamente escurecida pelo `BlackFade OUT`
  autoral. Evidência:
  `qa-l4-png-visible-20261107-20261107-020202-7156254`,
  PNGs `08B_png_visible_8000.png` e `08C_png_fade_start.png`.

## Defeitos/limitações ainda abertos

1. Marcador de tradução `^` visível em tela nas falas
   `Makoto?^`/`¿Makoto?^` e nos créditos `...2021^`.
   Os bytes de ORS/JSON **devem permanecer intactos**; solução
   game-specific de apresentação está sendo testada separadamente
   (sem alterar identidade/source/cobertura).
2. Falta executar os sete vídeos **inteiros** a 1× na janela, com áudio
   ouvido/loopback, captions amostradas ao longo da reprodução, pause,
   seek/restart e opções. Só B0 teve sequência inteira observada.
3. Faltam **28/28 finais por escolhas reais na GUI** combinando
   os witnesses KTRF com os filmes/fades/SaveFeel. A matriz 28/28 anterior
   foi apenas `--node --frames 5`; isto não libera a Etapa 6.
4. Falta inspecionar PNG/countdown/retorno visualmente em todos os sete
   com L×H ON, e OFF até o fim HQ. `d3d11` indisponível neste build;
   software funcionou; não afirmar certificação de backends não testados.
5. As gravações `SaveFile+GlobalFlag+sidecar` não são transação
   atômica tripla; limite e política `Untrusted/legacy ALLOW`
   documentados em `L4_SAVE_PROVENANCE_STAGE4.md`.

**Gate da Etapa 6 (launcher moderno): ainda não autorizado pelas provas**.
Não modificar o launcher visual até completar os 28 cenários naturais
e o QA audiovisual real do Plano Definitivo.

## Rodada posterior à correção de caret (2026-11-07)

- Implementação game-specific de projeção visual em
  `SchoolDaysDisplayNarrativeMarkup`, `Subtitles::refreshLocalizedCues` e
  `Runtime::dispatch PRINT_TEXT` (BackLog/PlayData/fallback).
  Não modifica ORS, traduções em JSON, speaker, SaveFile ou GPK.
  Caret terminal e imediatamente antes de quebra `\\n`/`\\N` removido
  somente na cópia de apresentação; carets literais internos preservados.
- **64/64 CTest Release PASS** (suite completa, serial, 67,10 s),
  agora incluindo `SchoolDays.DisplayNarrativeMarkup` junto dos testes
  genéricos, EN/ES/DLC, SaveProvenance, Replay, opções e mídia.
  No comando, `PATH` contém `lib/Release` e
  `games/school-days/Release`, SDL video/audio `dummy`.
- **EN/ES 1× visual PASS após rebuild**: o `05-A2-F00` mostra
  exatamente `Makoto?` e `¿Makoto?` sem `^` no mesmo evento real.
  Capturas em
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-visual-caret-fixed-20261107-20261107-022715-8526936/{en,es}/first_lxh_cue.png`.
- **7/7 sequências finais reais PASS**: cada final inicia vídeo L×H,
  acelera a aproximação ao término com controles do MenuBar até 24×,
  volta a 1× **antes** do cartão inglês, exibe PNG (captura original
  sem arte artificial), abre `Save this game?` com contador `30`,
  e volta ao menu após **30,987–31,161 s** desde o primeiro Next/SaveFeel
  observado. Sem processos órfãos ou gravações em saves reais.
  Evidências:
  `qa-l4-accel-ui-pilot-20261107-20261107-021906-6057972/results.json`
  (A2-F00) e
  `qa-l4-accel-ui-remaining-20261107-20261107-022016-4540837/results.json`
  (seis restantes). Todos os testes percorrem a cena final com
  `--node`; NÃO são partidas integrais por escolhas.
- Novo piloto sem `--node`, iniciado via botão START na tela comercial
  de título, chegou a `01/01-00-C02` e realizou **5/5 primeiras escolhas**
  da witness ENDG5 `05-A2-Z00` na ordem do KTRF original,
  por `DevBridge.choose(slot)` com o `Runtime::CommitChoice` atual
  (não `debugJump`). A interface de escolha foi carregada com textos
  originais antes de cada aceite. A automação passou a enviar `Right`
  apenas uma vez por cena (em vez de continuamente durante busca assíncrona
  de mídia), o que eliminou o bloqueio artificial visto no piloto inicial.
  Evidência em
  `qa-l4-natural-code5-five-20261107-20261107-023954-2812471/events.json`.
  Isso ainda **não atesta** hit-test por clique nem terminal de ENDG5
  via GUI; `choice` é uma ação direta de controle para validação da rota.

**Permanecem pendentes:** sete partidas completas pela interface (cada uma
com duas combinações ON/OFF e EN/ES), stress de Save/Load/replay entre
sessões, escuta física de todos os vídeos inteiros a 1×, e backend D3D11
quando disponível. Não liberar Etapa 6 apenas com as provas parciais.

## Rota natural ENDG5: 36 escolhas até 05-A2-Z00 (prova posterior)

Foi executada uma sessão **real do SchoolDays.exe recompilado da L4**,
iniciada pelo botão `START` da tela de título, sem `--node`, sem
`debugJump`, com perfil `--documents` isolado, inglês HQ nativo e
L×H ON. A navegação usou `Right` da interface para saltar cenas
e `DevBridge.choose(slot)` para ativar o `Runtime::choose` real na
opção visível; **não** foi chamada a API do roteador KTRF diretamente
nem pré-preparado um save com flags.

Evidência privada:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code5-full-20261107-20261107-024148-5185898`.

- **36/36 escolhas** aceitas em ordem, com `scene`, índice KTRF e
  candidato de slot coincidentes com
  `choice-plans/code-5.tsv`. Exatamente 217 mudanças de cena
  registradas pelo observador e 228 comandos de avanço, em ~449,46 s.
- O DevBridge registrou **`NATURAL_TERMINAL_FOUND 05/05-A2-Z00`**,
  `tick=24`, após a última opção `05/05-KA-A05`.
  SHA256 do KTRF atual
  `557830833AA3BB4A8EFBA82FE75964211491E9F02D60D6090DDA1D5174D4E745`
  foi revalidado no mesmo scratch.
- O script de coleta original encerrou com **exit code 1 falso-negativo**:
  comparou sua variável `last_scene` anterior (`05/05-A2-A00`)
  após interromper o loop assim que detectou a cena terminal.
  O evento terminal fora corretamente registrado **antes** da assertiva
  incorreta. Auditoria independente das 36 decisões e do evento terminal
  em `natural_route_audit.json`: `valid=true`. Portanto não reportar
  `Python wrapper exit0`, mas a **rota efetivamente alcançada**.
- Este teste valida o caminho de escolhas **somente em inglês, L×H ON,
  com `DevBridge.choose`**; não valida hit-testing por clique manual
  para as 36 opções nem o epílogo completo após a chegada ao nó final,
  pois a sessão de QA foi encerrada nesse ponto. Não converter isso
  em 28/28 cenários naturais completos.

## ENDG5: START → 36 escolhas autênticas → epílogo integral 1× → menu, com GlobalFlag

Nova execução física no **mesmo build L4 Release**, ainda sem `--node`, sem
`debugJump`, KTRF original e inglês nativo com L×H ON. O Python anterior foi
copiado **somente para scratch QA**, corrigido para não parar no primeiro
`05/05-A2-Z00`, mantendo os controles de navegação já autenticados.

Evidência privada:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code5-endtoend-20261107-0300`:
`natural_code5_endtoend.py`, `events.json`, `engine.log`,
`documents/Save/GlobalFlag.DAT`, `stage/dev-captures/frame-1.bmp` até
`frame-7.bmp`, `audit_globalflag_readonly.py`.

- **Exit 0**, `NATURAL_END_TO_END_RESULT 36 / 36 terminal True menu True
  movie True rights 228`, e `PROCESS_STOPPED True`.
- **36/36 escolhas reais do Runtime e KTRF** via `DevBridge.choose`, 217
  mudanças de cena, cena terminal atingida `05/05-A2-Z00` tick19 após
  **445,022 s** desde START. Não é ainda prova de hit-test físico por mouse;
  não houve estado KTRF ou save de origem artificialmente injetado.
- O próprio runtime em `engine.log` autenticou os limites:
  `EndRoll.end=6003`, `extraMovie.start=6075`; fade de saída dos créditos
  em tick6002, fade de revelação do vídeo em tick6075. Speed index 24×
  mudou via MenuBar para **1× no tick5580** antes da mídia extra.
- `hasVideoFrame=true` no filme L×H tick6087, `speed=1.0`;
  `events.json` registrou quatro screenshots com mídia e áudio lógico
  nos checkpoints de **25%, 50%, 75% e 96%**, depois frame terminal
  no tick13015 sem mídia aberta. **Não foi utilizado skip, seek ou
  aceleração durante o filme L×H**.
- `stage/dev-captures/frame-7.bmp`, convertido apenas para inspeção
  privada em `review-frame-7.png`, foi inspecionado: cartão final real
  preto com logotipo `School Days L×H` e o título inglês universal
  **“Behind the Scenes”** visível a 1366×768; nada foi gerado ou
  redimensionado dentro do overlay. `review-frame-5.png` mostra a
  legenda inglesa renderizada sobre vídeo de céu em 1×, sem caret.
- `engine.log` confirmou `LxH countdown fade OUT at Next` em tick13131 e
  `LxH countdown fade IN` (contador segurado até a revelação). O DevBridge
  registrou `gameState=1` de volta no menu após **792,024 s** desde START.
- Auditoria **independente read-only FlgH** após o processo encerrar:
  `audit_globalflag_readonly.py` decodificou integralmente
  `documents/Save/GlobalFlag.DAT` (6482 B, 236 entradas,
  SHA256 `0f829203f59cc9cbf1ad5dc4e22feb4b516c370ee18b7ed0814458329464838a`),
  `valid=true`, com exatamente `EndNo` tipo3=5, `EndClear` tipo11=−1,
  `[End05]="` tipo11=−1. O perfil isolado anterior encerrado ao entrar
  na cena terminal continha 231 entradas e **nenhuma** dessas chaves;
  entrada na cena não deve ser confundida com handoff persistido.
- SaveFeel expirado sem salvar slot **não** cria SaveFile nem Replay Data;
  a existência de GlobalFlag com marcador de final é a evidência correta.
  Áudio 1× é evidência **lógica** do mixer/streams, não escuta física ou
  loopback Windows; certificação audiovisual plena ainda pendente.

**Grau da prova:** uma rota START→final EN ON→menu com escolha legítima
por controle da ponte e filme completo 1×; ainda **zero** rotas integralmente
auditadas com hit-test físico para cada escolha, ainda **não 28/28**.

### Próximas rotas e timeouts originais (auditoria independente)

Em scratch `qa-stage5-timeout-worker12-20261107`, análise somente leitura
abriu os `SetSELECT` de **46/46 timeouts −1** das ORS originais do
`Script.GPK` e validou **129/129** prompts completos dos roteiros 8,10,17
contra o KTRF. Com Auto OFF, o timeout ocorre em `endTick−1`; 45 janelas
duram 5,0 s e uma 5,625 s. Velocidade 24× é **reduzida a 1× no
SETSELECT**; não antecipar o timeout com `choose(-1)` nem `Right`.
Relatório detalhado: `QA_STAGE5_TIMEOUTS_8_10_17_WORKER12.md` no referido
scratch. Prova física desses 46 ainda pendente.

Controladores QA em scratch `qa-l4-natural-mouse-runner-20261107`:
`natural_mouse_route.py` e `natural_timed_route.py`, com hit-test via
`mouse_move`/`click` reais (não `DevBridge.choose`), coordenadas
de opção 1 `(683,240)`, opção 2 `(683,528)`, escolha única `(683,384)`
para janela 1366×768. O script timed aguarda a expiração natural. Um
auditor independente `audit_natural_mouse_route.py` verifica o witness
e os reconhecimentos do input. Esses programas **não contam como PASS
até que cada execução física e sua auditoria terminem**.

## Regressão física ENDG6 e correção preventiva de preparação de mídia (2026-11-07)

O ENDG6 EN + L×H ON **alcançou legitimamente `05/05-A2-F00` após
37/37 cliques de mouse físicos reconhecidos**, sem `--node`, sem
`DevBridge.choose`, sem debugJump ou injeção de flags. Todos os 37
`real_mouse_choice_input` foram observados após `mouse_move` e evento
`ChoiceSelected`, conforme a witness KTRF.

Entretanto, o primeiro teste **NÃO passou de ponta a ponta**:

- Scratch isolado:
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-mouse-en-on-20261107-0312`.
- O EndRoll original armou `EndRoll.end=5867`, `extraMovie.start=5915`
  e registrou fade OUT preto em `tick=5866`. A janela depois ficou
  **inteiramente preta no tick5914 por mais de oito minutos**, sem
  `playingMediaCount`, com `paused=false`, `storyPaused=false`,
  `gameState=2`, CPU/Windows responsivos e nenhum avanço de roteiro.
  O screenshot `os-window-diagnostic.png` comprova o quadro preto real.
- O DevBridge secundário READ-ONLY registrou **dois**
  `MediaPrepareBegin:Media` (~485297/485333ms), correspondentes a
  PlayMovie WMV e PlaySE OGG no mesmo tick5915, sem `MediaPrepareEnd`
  posterior. `Runtime::waitForDueMedia` retornava indefinidamente
  enquanto havia pending; o fade permanecia em `AwaitMovie`, alpha255.
  Não foi provado em qual chamada interna de FFmpeg/resolver/fila o worker
  gastava tempo: **não declarar deadlock específico de codec**.
- Após provas/status/linha de comando/copias de tela, **somente o EXE
  staged privado PID18244** foi interrompido. A campanha emitiu exit1
  por interrupção diagnóstica, não por crash nativo espontâneo. Saves
  comerciais e GPK ficaram intocados. A auditoria independente está em
  `qa-stage5-code6-media-stall-worker12-20261107/QA_CODE6_MEDIA_STALL_WORKER12.md`.

Correção mínima **apenas game-specific** aplicada depois de encerrar o
processo: `AbcAnalysisWorker` agora tem lane própria para preparação de
mídia, independente da fila serial de análise ABC/background; logs de
`worker BEGIN/END` incluem geração/ordinal/recurso/tempo; cada media
pending ganha watchdog de parede 12s com marca terminal de timeout,
descarta conclusão tardia, impede reenfileiramento e reporta warning
explícito antes de permitir o fallback `play()` já existente.
`completePendingSeek` reutiliza o mesmo timeout. Sem editar `lib/`.
**Limitação importante:** esse deadline elimina a espera ilimitada na
fila, mas o `play()` direto ainda abre decodificador **sincronamente**;
não provar que toda causa de atraso FFmpeg está sanada até teste físico.

Novo helper e testes:
`include/Kotonoha/routing/SchoolDaysMediaPreparationWatchdog.hpp`,
`tests/SchoolDaysMediaPreparationWatchdogTest.cpp`, alvo CTest
`SchoolDays.MediaPreparationWatchdog` (WMV/OGG mesmo tick5915,
completions, timeouts, late result, rollover, no requeue).
**ALL_BUILD Release PASS; 65/65 CTest Release PASS em 68,25s**.
SHA256 do `SchoolDays.exe` resultante:
`D33C40A5B0231A8AD45E08F8C13EA571FB189F888EB2EAAB7F853D15306FB11D`.
Reteste natural ENDG6 está em scratch
`qa-l4-natural-code6-mouse-wasapi-retest-20261107-0340` e **ainda não
é PASS enquanto o processo não retornar ao menu e o FlgH não for auditado**.

Para verificar saída de áudio digital Windows, utilitário **WASAPI loopback
C++ Release** compilado em scratch privado
`qa-wasapi-loopback-worker14-20261107`; `--list` confirmou Realtek e
SAMSUNG/NVIDIA (padrão console/multimedia). O harness
`qa-l4-natural-mouse-runner-20261107/natural_mouse_with_wasapi.py`
liga captura para os dois endpoints **apenas após ter vídeo L×H real**.
Arquivo WAV/CSV e escuta/sincronismo A/V exigem evidência física; enumeração
e métricas lógicas não equivalem a reprodução ou sincronização audíveis.

**Gate:** continua FECHADO. ENDG5 tem evidência integral de fluxo via
DevBridge.choose, ENDG6 tem navegação 37/37 com hit-test físico porém
primeira reprodução bloqueada, e a matriz 28/28 continua PENDENTE.

## ENDG6: segundo bloqueio, áudio WASAPI e revisão da correção (04:08 BRT)

Segundo percurso EN + L×H ON em scratch
`qa-l4-natural-code6-mouse-wasapi-retest-20261107-0340`:

- 37/37 escolhas físicas novamente; `05/05-A2-F00` alcançado e
  vídeo L×H iniciado em `tick5915`, **1×**. Checkpoints visuais reais em
  25%/50%/75%/96%, com WMV terminando e `playingMediaCount=0`.
- A abertura foi via **recuperação síncrona após watchdog**: logs
  `media worker END ... ordinal36/37 ready=0 cancelled=1` e expiração
  em `ageMs≈12013`. Não foi uma preparação assíncrona saudável.
- Em `tick8866`, o jogo permaneceu visualmente no último frame do
  cemitério (screenshot privado `os-window-endcard-bg-stall.png`), com
  `MediaPrepareBegin:Background` no log mas sem End; impediu o
  `CreateBG` do cartão final universal, SaveFeel, countdown e retorno
  ao menu. A campanha foi encerrada **após** captura e DevBridge READ-ONLY
  pelo PRIME, somente no executável staged PID8400, sem tocar saves reais.
- WASAPI loopback real: `wasapi-samsung.wav`, **165 s**, PCM16 estéreo
  48 kHz, 12.216 pacotes, 118 segundos RMS maior que −50 dBFS,
  RMS total −37,59 dBFS, pico −8,61 dBFS, 754 frames de lacuna.
  `wasapi-realtek.wav` não recebeu pacotes; 192 kHz 2 canais de
  silêncio. Relatório em `wasapi-audit.json`. Isso comprova atividade
  do endpoint digital durante o epílogo, **não** escuta humana nem
  exclusividade/sincronismo labial. Este segundo percurso **não é PASS**.

**Causa do cancelamento autenticada no fonte:**
`lib/src/media/MediaPlayer.cpp:91-100` (`State::~State`) chama
`cancelled->store(true)`. Antes, `AbcAnalysisWorker::enqueueMedia`
atribuía um token **compartilhado pela cena** para todos os decoders.
Descartar um `PreparedInput` anterior anulava mídias futuras na mesma
geração; o par L×H WMV/OGG cancelado em 0ms reproduz exatamente essa
condição. Não alterar a biblioteca genérica: criado helper específico
`SchoolDaysMediaCancellationPool.hpp` para tokens independentes por
recurso e também separados para intro/loop BGM. `CancelAll()` somente
no reset/teardown legítimo da geração. Fundo ganhou fila e thread
próprias, para não ficar atrás de longas análises ABC. O watchdog
conta agora do **START devido** (não do speculative preload);
12 s para mídia, 30 s para fundo, incluindo dois recursos no mesmo tick,
e resultados atrasados de fundo não são aplicados após timeout.

Novo teste `SchoolDays.MediaCancellationPool` cobre token isolado
de WMV/OGG, descarte de voz, BGM intro/loop, geração e compactação.
Novo build L4 Windows Release **ALL_BUILD PASS**, SHA256 `SchoolDays.exe`
`1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.
**66/66 CTest Release PASS**, serial, 74,62 s. Todas as mudanças estão
no código game-specific; não houve commit/push, alterações de GPK,
JSON canônico, arte universal nem saves reais.

Terceira campanha ENDG6, nova L4 e documentos inteiramente isolados:
`qa-l4-natural-code6-mouse-cancelpool-20261107-0410`, iniciada via
`natural_mouse_with_wasapi_v2.py` e porta DevBridge `48565`.
O harness privado preserva witness terminal `last_scene/last_tick`,
acompanha a mídia até menu, aborta polling sem progresso antes/depois
do filme e copia as DLLs da **mesma build Release** para o staged QA.
**Estado inicial naquela coleta:** START e escolha 1 por SDL mouse;
o resultado final e o FlgH estão documentados no checkpoint posterior.

### Matriz progressiva de 28 cenários — sem completar por inferência

| Final | EN + DLC ON | EN + DLC OFF | ES + DLC ON | ES + DLC OFF |
|---|---|---|---|---|
| ENDG5 (A2-Z00) | **Ponta a ponta** com 36 escolhas GUI/DevBridge, filme 1×, FlgH=5; audit hardware/áudio ainda parcial | Pendente | Pendente | Pendente |
| ENDG6 (A2-F00) | **Ponta a ponta PASS** no terceiro reteste, 37/37 SDL mouse, 1×, FlgH=6, WASAPI digital; screenshot direto do BG final ainda não arquivado | **Ponta a ponta PASS**: 37/37 SDL mouse, EndRoll HQ original 1×, FlgH=6, WASAPI digital; zero recursos/fades L×H | Pendente | **Ponta a ponta PASS**: 37/37 SDL mouse, EndRoll HQ original 1×, FlgH=6, WASAPI digital; zero recursos/fades L×H |
| ENDG7 (B0-F00) | Pendente: TSV 52 escolhas, 0 timeout | Pendente | **Ponta a ponta PASS**, 52/52 SDL mouse, 1×, FlgH=7, WASAPI digital e cartão `Cold Scarf` visto após `CreateBG` | Pendente |
| ENDG8 (G0-L03) | **Ponta a ponta PASS**: 22 SDL mouse + 15 timeouts naturais, 1× EndRoll e L×H, FlgH=8, WASAPI digital, cartão `Braided Hair` após `CreateBG` | Pendente | Pendente | Pendente |
| ENDG10 (KC-F00) | Pendente: TSV 44 escolhas, 14 timeouts naturais | Pendente | Pendente | Pendente |
| ENDG15 (SB-E01) | Pendente: TSV 55 escolhas, 0 timeout | Pendente | Pendente | Pendente |
| ENDG17 (SC-L03) | Pendente: TSV 48 escolhas, 17 timeouts naturais | Pendente | Pendente | Pendente |

O método `DevBridge.click` gera eventos SDL
`SDL_EVENT_MOUSE_BUTTON_DOWN/UP` no canvas e aciona o hit-test normal;
**não** chama `choose(index)`, `CommitChoice(-1)` ou `--node`, mas
**não equivale a um clique vindo de um mouse USB/SendInput do Windows**.
Registrar essa distinção em qualquer relatório de navegação natural.
Legendas, WAV e clips devem ser observados por cenário, não inferidos da
cobertura estática. Há **três combinações que atingiram o menu**
(ENDG5 EN/ON com `choose`/fluxo controlado; ENDG6 EN/ON e ENDG7 ES/ON
com SDL hit-test), das quais **somente duas** completaram o percurso
inteiro com SDL mouse em cada escolha. Não declarar 28/28 até documentação
independente.

## ENDG6 EN/ON — fechamento do terceiro percurso (04:19 BRT)

Fonte imutável de QA em scratch:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-mouse-cancelpool-20261107-0410`.
Executável da **L4 Release** SHA256
`1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.

1. `route-audit.json` **valid=true**, `scene=05/05-A2-F00`, 37
   `real_mouse_choice_input` com `acknowledged=true` e
   `ChoiceSelected` correto; não houve `--node`, `choose`, seleção
   sintética de timeout, nem divergência de KTRF (SHA256
   `557830833aa3bb4a8efba82fe75964211491e9f02d60d6090dda1d5174d4e745`).
2. `engine.log`: preload WMV ordinal36 `ready=1 cancelled=0` em **5ms**,
   OGG ordinal37 `ready=1 cancelled=0` em **2ms**, ambos geração904;
   nenhum watchdog de mídia precisou expirar. Fade OUT EndRoll tick5866,
   fade IN WMV tick5915. `lxh_video_open` com `hasVideoFrame=true`
   tick5926, `speed=1`, `endTick=8858`; amostras GUI em 25%/50%/75%/96%.
   A legenda inglesa foi inspecionada no `review-lxh-code6-50pct.png`:
   **“I mean, what do you do with your life?”**, estilo ASS verde e sem
   o marcador `^` renderizado. Os 7 PNG universais ingleses ainda
   coincidem byte a byte com a fonte externa por SHA256 após rebuild.
3. Após a mídia, `engine.log` registrou `LxH countdown fade OUT at Next`
   tick8963 e `LxH countdown fade IN` de volta ao SaveFeel, com o relógio
   suspenso durante revelação. `events.json` registrou
   `NATURAL_END_TO_END_TITLE` e `NATURAL_END_TO_END_RESULT`:
   37/37, `terminal=true`, `menu=true`, `movie=true`.
   **Ressalva:** `lxh_end_card_and_fade` fez screenshot no tick8861,
   seis ticks antes do `CreateBG` (8867): `review-lxh-code6-end-card.png`
   mostra ainda o cemitério, logo **não** é prova visual direta do PNG
   “Blue Smile”. Houve avanço para Next e persistência válida, mas a
   captura do cartão precisa de Replay/QA adicional sem repetir rota
   legitimamente aprovada só para elevar contagem.
4. `globalflag-audit.json`: decodificação binária FlgH de
   `documents/Save/GlobalFlag.DAT` — **valid=true**, 6.678 B,
   243 entradas, SHA256
   `b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`;
   `EndNo` tipo3=6, `EndClear` tipo11=−1, `[End06]="` tipo11=−1,
   cada chave única. Saves reais e GPK comerciais não foram tocados.
5. `wasapi-audit.json`: captura do endpoint real
   **SAMSUNG (NVIDIA High Definition Audio)**, WAV PCM16 estéreo
   48kHz, **165s**, 12.238 pacotes, 117 segundos com RMS acima de
   −50dBFS, RMS total −37,59dBFS, pico −8,61dBFS e 1.124 frames de
   lacuna reconstruída. O loopback capta o endpoint misto Windows,
   **não** equivale a escuta auditiva humana ou prova de lipsync perfeito.

Status **ENDG6 EN/ON aprovado para navegação SDL, filme 1× e FlgH**;
ressalvas visuais do BG final e escuta física devem continuar explícitas.
O gate geral da Etapa 5 continua **não satisfeito**: outros finais,
idiomas, DLC OFF, Save→Load, Replay/PlayData, Options/Skip/Auto etc.
seguem pendentes. Nova campanha ENDG7 **ES/ON** foi iniciada isolada em
`qa-l4-natural-code7-es-on-20261107-0421`, sem paralelizar GUI.

## ENDG7 ES/ON — fechamento auditado (04:39 BRT, 07/11/2026)

Scratch privado, preservado sem alterações comerciais:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code7-es-on-20261107-0421`.
Mesma L4 Release SHA256
`1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.

1. `route-audit.json`: **valid=true**, 52/52 escolhas executadas por
   `DevBridge.mouse_move`/`click` com hit-test SDL, `real_mouse_ack=52`,
   timeouts esperados/observados=0, alvo `05/05-B0-F00`, vídeo aberto e
   `returned_to_title=1`; KTRF SHA256
   `557830833aa3bb4a8efba82fe75964211491e9f02d60d6090dda1d5174d4e745`.
   Isto **não** representa mouse USB/SendInput. Runner principal terminou
   `ENDG7_ES_ON_PHYSICAL_GUI_PASS`, `PROCESS_STOPPED True`, exit code 0.
2. `engine.log`: WMV ordinal76 preparado em 5ms e OGG ordinal77 em 2ms,
   ambos `ready=1 cancelled=0`. Fade OUT após EndRoll (tick5682), IN na
   mídia L×H (tick5731), apresentação em velocidade 1×, checkpoints
   25/50/75/96%; fade OUT/IN antes do SaveFeel/countdown (tick8107).
   O cartão universal **“Cold Scarf” em inglês** está visível na captura
   `review-code7-es-final-card-after-CreateBG.png`, tick7967, **após**
   `CreateBG`. `review-spanish-choice-live.png` confirma UI ES
   (“Quería darte una sorpresa” / “Lo olvidé”).
3. `globalflag-audit.json`: **valid=true**; FlgH 6.818 bytes/248 entradas,
   SHA256 `aaa0a79ccfbe0c1951de85ff3f5d740a48d18b4e215b13d63f9b700f2fb743d2`;
   `EndNo`=7, `EndClear`=−1 e `[End07]="`=−1. Escrita somente em
   `documents/Save/GlobalFlag.DAT` do scratch privado.
4. `wasapi-audit.json`: PCM16 estéreo 48kHz, 112,0s, 9.271 pacotes no
   endpoint SAMSUNG/NVIDIA; RMS médio −35,5dBFS, pico −10,16dBFS,
   68s acima de −50dBFS, 738 frames de gaps reconstruídos.
   `physical_output_activity_detected=true` prova atividade digital no
   endpoint compartilhado, **não** áudio isolado do jogo, escuta humana
   nem lipsync perfeito.
5. `countdown_readonly_observer.json` contém cartão após CreateBG, fade
   OUT/IN, checkpoints do countdown a 5,306s / 15,808s / 28,418s. O
   observador passivo encerrou `complete:false` com `ConnectionResetError`
   porque sua conexão DevBridge foi fechada **depois** de o runner principal
   sair normalmente; não observou pessoalmente o estado de título.
   **Não é evidência de crash da runtime** nem deve ser convertido em
   `observer PASS` autônomo. O retorno ao menu é certificado pelo runner
   principal e pela auditoria independente.

Total até esta evidência: **3/28 combinações ponta a ponta**, apenas
**2/28 completamente por SDL mouse hit-test**; Etapa 5 segue em progresso.
Nenhum caso com timeout natural físico e nenhum DLC OFF concluído até aqui.

## ENDG8 EN/ON — 15 timeouts KTRF naturais e epílogo completo (07/11/2026)

Nova sessão exclusiva, scratch privado:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code8-en-on-20261107-0445`.
Mesmo binário da L4 Release, SHA256
`1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.
Harness usado: `qa-stage5-timeout-review-20261107/natural_timed_route_v3.py`.

1. Runner: **exit 0**, `NATURAL_END_TO_END_RESULT 37 / 37 terminal True
   menu True movie True rights 241`, `PROCESS_STOPPED True`.
   Foram **22 cliques SDL mouse via hit-test** com `lastAction` e
   `ChoiceSelected`, e **15 timeouts naturais** (ordinais
   `1,2,5,6,8,10,11,13,14,17,21,23,24,31,33`) aguardados em
   velocidade **1×**. Cada timeout exigiu `ChoiceOpened`, novo
   `ChoiceSelected` após o prazo autoral e transição para cena seguinte,
   **sem clique, `choose(index)`, `CommitChoice(-1)` ou `--node`**.
   O runner apresentou o final nativo `05/05-G0-L03`.
2. Auditoria independente `route-audit.json`: **valid=true**,
   `choices=22`, `real_mouse_ack=22`, `natural_timeouts=15`,
   `expected_timeouts=15`, `video_open=1`, `returned_to_title=1`,
   `reasons=[]`. KTRF SHA256
   `557830833aa3bb4a8efba82fe75964211491e9f02d60d6090dda1d5174d4e745`.
   A primeira tentativa de auditoria deu falso-negativo: seu código
   privado interpretava `timeout<0`, mas o TSV documentado usa `timeout=1`
   e guarda o valor semântico `-1` em `ktrf_semantic_value`. Corrigido
   **somente o auditor scratch** para exigir os 15 times, sem suavizar
   suas provas de relógio; segunda execução **PASS**. Não é bug do jogo.
3. EndRoll HQ original rodou em 1× até `EndRoll.end=4095`;
   fade OUT tick4094, fade IN do WMV L×H tick4119.
   `lxh_video_open` tick4129, `speed=1.0`, `endTick=6951`, quatro
   checkpoints de vídeo 25/50/75/96% reais. WMV L×H ordinal43
   `ready=1 cancelled=0` em **6ms**, OGG ordinal44 em **2ms**;
   nenhum timeout/watchdog foi necessário. Fade OUT antes do countdown
   tick7119 e fade IN com relógio mantido até a revelação.
4. **Cartão inglês “Braided Hair” visto diretamente** na imagem
   `review-code8-en-final-card-after-CreateBG.png`, screenshot da janela
   1366×768 no tick6975, `background=LxH/05-G0-L03-LXH.png`;
   o quadro mostra `School Days L×H`, “Braided Hair” e barra/menu,
   e foi capturado **depois** da adoção do `CreateBG`.
5. `globalflag-audit.json` **valid=true**: FlgH 6.720 bytes/245 entradas,
   `EndNo=8`, `EndClear=−1`, `[End08]="`=−1, SHA256
   `c0fcf0e0a8581549f4dfaaaf493cfc479baa7f979b2ce5b44054e3b771d7cf7f`.
   Apenas `documents/Save/GlobalFlag.DAT` do scratch foi escrito;
   nenhum save real/comercial alterado.
6. `wasapi-audit.json`: Samsung/NVIDIA, WAV PCM16 48kHz estéreo,
   **137s**, **11.781 pacotes**, RMS médio −32,34dBFS,
   pico −8,4dBFS, **118s** com RMS acima de −50dBFS,
   994 frames de gaps reconstruídos, `physical_output_activity_detected=true`.
   Captura do mix digital do endpoint, **não** áudio isolado do jogo,
   escuta humana nem lipsync auditivamente certificado.

**Matriz após ENDG8: 4/28 combinações concluídas** (ENDG5 EN/ON,
ENDG6 EN/ON, ENDG7 ES/ON e ENDG8 EN/ON). Destas, **3/28 inteiramente
via SDL mouse hit-test/timeout natural**, excluindo ENDG5 com `choose`.
Todos os DLC OFF permanecem pendentes, bem como Save→Load A→B→A,
Replay/PlayData, Options, BackLog e as demais combinações. Etapa 5
segue **ABERTA**; nenhuma ação de launcher, Android, commit ou push.

## ENDG6 EN/OFF — percurso retail completo sem recursos L×H (07/11/2026)

Sessão exclusiva em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-en-off-20261107-0500`
com o mesmo executável L4 Release SHA256
`1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`.
Runner privado `natural_mouse_off_original_1x_v2.py` com cópia das DLLs
da Release; `--lxh off`, `--renderer software`, perfil novo `--documents`.

1. **37/37 escolhas via `DevBridge.mouse_move/click` (hit-test SDL)**,
   zero timeouts; final legítimo `05/05-A2-F00`.
   `NATURAL_OFF_END_TO_END_RESULT 37 / 37 terminal True menu True
   originalMovie True rights 234`, exit0, `PROCESS_STOPPED True`.
   Auditoria independente `route-audit.json`: **valid=true**,
   `choices=37`, `real_mouse_ack=37`, `natural_timeouts=0`,
   `video_open=0` (corretamente: *nenhum WMV adicional L×H*),
   `returned_to_title=1`, `reasons=[]` e KTRF SHA256
   `557830833aa3bb4a8efba82fe75964211491e9f02d60d6090dda1d5174d4e745`.
   SDL injection pelo DevBridge não significa hardware USB/SendInput.
2. Filme original do EndRoll HQ abriu no tick194, `endTick=1101`,
   **velocidade 1×**, com capturas de 25/50/75/96%; o encerramento
   prosseguiu em 1× até o tick5867 e voltou ao menu. Testes em execução
   recusaram mídia/fundo `LxH` e, na inspeção pós-run do `engine.log`,
   houve **zero** ocorrências de `LxH/Media/`, `LxH fade armed`,
   `LxH entrance fade`, `LxH countdown fade` e `-LXH.png`.
   Nenhum sidecar L×H nem SaveFile foi gerado no perfil isolado.
3. `globalflag-audit.json` **valid=true**: FlgH 6.678 bytes/243 entradas,
   `EndNo=6`, `EndClear=−1`, `[End06]="`=−1, SHA256
   `b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`.
   Somente `documents/Save/GlobalFlag.DAT` privado foi escrito.
4. `wasapi-audit.json`: WAV do endpoint Samsung/NVIDIA, PCM16 estéreo
   48kHz, **57s / 5.700 pacotes**, RMS médio −41,76dBFS,
   pico −20,08dBFS, 53s acima de −50dBFS e 561 frames de gaps
   reconstruídos; `physical_output_activity_detected=true`.
   É evidência de PCM no endpoint compartilhado durante a reprodução,
   não captura isolada do jogo nem prova de escuta humana/lipsync.

**Matriz acumulada: 5/28 combinações ponta a ponta** (ENDG5 EN/ON,
ENDG6 EN/ON e EN/OFF, ENDG7 ES/ON, ENDG8 EN/ON). **4/28 por escolhas
SDL e/ou timeout natural em toda a rota**; ENDG5 ainda utiliza `choose`
nas 36 decisões. Todos os ES/OFF e demais finais continuam pendentes,
assim como Save→Load A→B→A, opções e Replay/PlayData. Etapa 5 aberta.

## Etapa 5 funcional — Save→Load A→B→A, MenuBar e correção mesma página (07/11/2026)

**QA físico do estado persistido no mesmo processo — PASS, com ressalva de defeito
de página detectado e corrigido em seguida.** Perfil privado, binário L4 anterior
SHA256 `1A996138129204C15647C33F3B9B4467E67EF110BCB6F524326553FEAB565122`:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-save-load-prime-20261107-1131`.
Fonte retail `Ini.GPK`/`System.gpk` usada **somente em leitura** para autenticar
`FILMENGINE.INI` (`TextInput=1`) e o CMAP WideFull 1280×720. O teste clicou na
MenuBar SAVE, na página 1, nos slots 0 e 1; fotografou e confirmou visualmente
o editor nativo de comentários vazios antes do `SDLK_RETURN`. **Ambos SaveFiles
foram escritos pelo próprio jogo**, nunca por RPC de gravação direta:

- A: `Save/SaveFile000.DAT`, 519 bytes, SHA256
  `1e65f1bcb9dca7074bb94bb5ff82bc2bb433f136b6f7bd38d543d2b4e876f26a`,
  origem `00/00-00-B01`, nó 9.
- B: `Save/SaveFile001.DAT`, 1.378 bytes, SHA256
  `61f9f47e9596b047db73b4d30e5b2309dbab010bc71f46f0f5b392713cc6d61c`,
  origem `01/01-00-E01`, nó 44.
- Sidecars `*.kotonoha-lxh.v1` criados e ligados aos hashes/bytes,
  `lxh=0` (saves comuns, não epílogo L×H).

No primeiro Load A, o clique redundante na aba de página 1 limpou
`occupancyReady_`/`rowOccupied_` do modelo, **sem alterar `Page()`**; o runtime
`refreshSaveMetadata()` só roda se `saveLoadPage != Page()`. Portanto o painel
continuou visualmente ocupado, mas recusou ativação da linha. O testador
privado abortou após 12s, **sem declarar PASS**. A hipótese foi validada com
cliques nativos página 2 → página 1, que forçaram a atualização e permitiram
**A→B→A no mesmo PID**, cenas/nós corretos e nenhum SaveFile alterado pelo Load.
Comparação de pixels da barra de sentimentos (x=320..1030, y=13..28,
passo=2, 2.848 amostras): original A vs A restaurado **0 diferenças RGB**;
original B vs B restaurado **0 diferenças**; original A vs segunda carga A
**0 diferenças**; A original vs B original **313 amostras** diferentes,
soma RGB absoluta **12.995**. Evidência em `meter-pixel-comparison.json`.

Na mesma sessão privada, **funcional MenuBar PASS** por cliques SDL:
Pause ON/OFF (relógio congelado no tick62 durante 1s), AUTO ON/OFF fotografado
sem avançar narrativa, velocidades 1/2/4/12/24 (chips x1/x2/x4/x16/x32),
BackLog retail aberto/fechado com screenshot e relógio estável.
`get_audio_state`: BGM/Voice/SE/Movie silenciados logicamente nas taxas 12/24,
não silenciados em 1/2/4, recuperados ao voltar para 1×. Back/Restart nativo:
mesma cena `00/00-00-D00`, tick 3512→1; SkipToEnd: tick 17→122,
nenhuma escrita nos SaveFiles/sidecars. Todos scripts/prints/capturas ficam
exclusivamente em scratch; não atestam escuta humana nem o SkipNextChoice.

**Correção game-specific mínima:** `SchoolDaysSaveLoadModel.cpp` retorna sem
limpar ocupação ao clicar na página já selecionada; mudança real de página
continua zerando cache até o host publicar novas linhas. Novo teste puramente
sintético `SchoolDaysSaveLoadSamePageTest.cpp`, registrado como
`SchoolDays.SaveLoadSamePage` no CMake. Release **ALL_BUILD PASS** e
**67/67 CTest Release serial PASS**, incluindo o teste novo. SHA256 da nova
Release `SchoolDays.exe`:
`0FAEA26D2D1C6316511B714022DCF72BADA545C1899FBC91023D8C4690C3560B`.

**Reteste físico após a correção — PASS**, pasta privada:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-samepage-retest-20261107-r2`.
Copiados apenas os dois SaveFiles **gerados no QA anterior** e seus sidecars
para `--documents` descartável novo (não houve alteração nos saves reais).
Partindo de START legítimo, em **cada** LOAD clicou na aba página 1 *já
selecionada* antes de selecionar a linha ocupada. Resultado: A (`B01`, nó9),
B (`E01`, nó44), A (`B01`, nó9) restaurados sucessivamente; 3/3 transições
sem bloqueio, tela fotografada, `saveLoadOpen=false`, tick0 seguido de avanço,
SHA256 imutável dos SaveFiles/sidecars/GlobalFlag no reteste, processo
encerrado normalmente. `PHYSICAL_SAME_PAGE_LOAD_A_B_A_PASS`, exit0.
O primeiro ensaio do novo runner abortou **antes de lançar jogo** por diferença
de separador Windows `\\` vs `/` no dicionário privado; corrigido em scratch
via `.as_posix()` e reexecutado com sucesso no diretório `-r2`.

O gate funcional **Save→Load A→B→A e correção aba atual foi cumprido**. A
Etapa 5 geral permanece **ABERTA**: matriz ainda **5/28**, faltam idiomas/
finais OFF, Options General/Sound/Display, Replay HScene/PlayData, aviso
de provenance ON→OFF e SkipNextChoice (SuperSkip), entre outros. Etapas 6–7
continuam bloqueadas; sem Android/commit/push.

## ENDG6 ES/OFF — percurso físico HQ original 1× e sem L×H (07/11/2026)

Scratch privado `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-es-off-20261107-1230`,
runner `natural_mouse_off_original_1x_v2.py`, `--translation <Release>/translations/es.json`
SHA256 `17f0db1490f97e84e1a83001ee50feee3d82f19749b3625c2f49eaf0fba8495a`,
`--lxh off`, `--documents` exclusivo, executável L4 Release corrigido SHA256
`0FAEA26D2D1C6316511B714022DCF72BADA545C1899FBC91023D8C4690C3560B`.

1. **37/37 escolhas por mouse SDL hit-test**, sem `choose`, `--node` ou
   `CommitChoice`; rota KTRF termina de forma legítima em `05/05-A2-F00`.
   Runner `NATURAL_OFF_END_TO_END_RESULT 37 / 37 terminal True menu True
   originalMovie True rights 234`, `PROCESS_STOPPED True`, exit0. A auditoria
   independente `route-audit.json` é **valid=true**, `choices=37`,
   `real_mouse_ack=37`, `natural_timeouts=0`, `expected_timeouts=0`,
   `video_open=0` (corretamente sem *vídeo adicional* L×H), retorno
   ao título=1, `reasons=[]`. KTRF SHA256
   `557830833aa3bb4a8efba82fe75964211491e9f02d60d6090dda1d5174d4e745`.
2. EndRoll HQ original abriu no tick137, endTick1101, e foi reproduzido
   a **1×**, com capturas 25/50/75/96%; a timeline avançou até o
   tick5867 e retornou ao título. Verificação pós-run de `engine.log`:
   **zero** referências `LxH/Media/`, `LxH entrance fade`,
   `LxH countdown fade`, `-LXH.png`, `LxH fade armed`, e **nenhum
   sidecar L×H** criado em `documents/Save`.
3. `globalflag-audit.json` **valid=true**: FlgH 6.678 bytes / 243 entradas,
   SHA256 `b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`,
   `EndNo=6`, `EndClear=-1`, `[End06]="=-1`.
4. `wasapi-audit.json`: Samsung/NVIDIA PCM16 estéreo 48kHz,
   **60 segundos, 5.998 pacotes**, RMS médio −41,70dBFS, pico −20,23dBFS,
   57 segundos acima de −50dBFS, 870 frames de lacunas reconstruídas;
   `physical_output_activity_detected=true`. Demonstra atividade PCM no
   endpoint compartilhado, **não** escuta humana, áudio exclusivo do jogo
   nem lipsync perfeito.

Este novo resultado eleva a matriz Stage5 para **6/28 cenários ponta a
ponta**: ENDG5 EN/ON, ENDG6 EN/ON + EN/OFF + ES/OFF, ENDG7 ES/ON,
ENDG8 EN/ON; **5/28** integralmente com cliques SDL/timeout natural
autêntico (ENDG5 permanece com `DevBridge.choose`). Faltam **22** combinações,
entre outras pendências funcionais; Etapa 5 segue aberta, 6–7 bloqueadas.

## ENDG6 ES/OFF — segundo cenário sem DLC L×H concluído (07/11/2026)

Nova sessão exclusiva em scratch
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-es-off-20261107-1230`.
L4 Release corrigida SHA256
`0FAEA26D2D1C6316511B714022DCF72BADA545C1899FBC91023D8C4690C3560B`;
tradução ES instalada em `Release/translations/es.json`, SHA256
`17F0DB1490F97E84E1A83001EE50FEEE3D82F19749B3625C2F49EAF0FBA8495A`;
`--lxh off`, `--renderer software`, `--documents` novo exclusivo,
**sem `--node` ou escolha semântica por RPC**.

1. Runner `natural_mouse_off_original_1x_v2.py`: **37/37 escolhas SDL**
   por `mouse_move` seguido de `click`, com hit-test e `ChoiceSelected`
   verificados; zero timeouts. Final retail legítimo `05/05-A2-F00`,
   EndRoll original em **1×**, quatro checkpoints 25/50/75/96%,
   `NATURAL_END_TO_END_TITLE ... tick 5867 choices 37`,
   `NATURAL_OFF_END_TO_END_RESULT 37 / 37 terminal True menu True
   originalMovie True rights 234`, exit0, `PROCESS_STOPPED True`.
   KTRF SHA256 `557830833aa3bb4a8efba82fe75964211491e9f02d60d6090dda1d5174d4e745`.
2. `route-audit.json` independente: **valid=true**, `locale=es`, `lxh=off`,
   `real_mouse_ack=37`, `natural_timeouts=0`, `returned_to_title=1`,
   `reasons=[]`. `video_open=0` corretamente refere-se apenas à ausência de
   vídeo *adicional* L×H, não ao EndRoll original comprovado pelo runner.
3. `globalflag-audit.json` **valid=true**: FlgH 6.678 bytes/243 entradas,
   `EndNo=6`, `EndClear=-1`, `[End06]="=-1`, SHA256
   `b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`.
   Um único `GlobalFlag.DAT` foi escrito no perfil exclusivo; nenhuma SaveFile
   nem sidecar foi criada nessa campanha.
4. `engine.log`: **0** referências a `LxH/Media/`, `LxH fade armed`,
   `LxH entrance fade`, `LxH countdown fade` ou `-LXH.png`.
   Amostras físicas do EndRoll HQ original em 1× e retorno ao título.
5. `wasapi-audit.json`: WAV endpoint Samsung/NVIDIA, PCM16 estéreo 48kHz,
   **60s / 5.998 pacotes**, RMS médio −41,7dBFS, pico −20,23dBFS,
   57s acima de −50dBFS, 870 frames de gaps reconstruídos,
   `physical_output_activity_detected=true`. PCM do endpoint compartilhado,
   sem prova de áudio isolado, escuta humana nem sincronismo labial perfeito.
6. Screenshot direto `frame-1.png` da fase final (`stage/dev-captures/frame-1.bmp`)
   mostra **“Un año después”**, tradução espanhola efetivamente apresentada
   em ASS sobre o cartão retail japonês. A interface de cada uma das 37
   escolhas não foi fotografada isoladamente; `status.choices` do DevBridge
   preservou labels canônicos ingleses, que não representam, por si, a
   apresentação visual; não declarar auditoria visual exaustiva das escolhas ES.

**Matriz acumulada: 6/28 combos ponta a ponta**: ENDG5 EN/ON; ENDG6 EN/ON,
EN/OFF e **ES/OFF**; ENDG7 ES/ON; ENDG8 EN/ON. **5/28 integralmente por
SDL mouse/timeout natural**, excluindo apenas ENDG5 com `choose`. Gate Stage5
geral ainda aberto, faltam 22 combinações e QA físico de Options/Replay,
PlayData, SuperSkip/aviso provenance ON→OFF; sem launcher/RC/Android/commit/push.

## Retomada Stage5 — reconciliação Options/Replay e build L4 (07/11/2026)

Conferido o relatório privado concluído em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-timeout-review-20261107/functional-options-replay-prime-20261107-r2/qa_stage5_gui_result.json`,
gerado pelo processo staged PID18284 / DevBridge48572. Na retomada, **nenhum
`SchoolDays.exe` permanecia ativo**. O relatório `verified` contém:

- `TITLE_FAMILY_MANUALLY_ATTESTED`: visual da variante Title Clear;
- `OPTIONS_NAVIGATION_SCREENSHOTS`: abertura de General, Sound e Display,
  fotografadas, com retorno ao Title, sem Apply/troca de resolução;
- `HSCENE_UNLOCK_VISUAL_ONLY_NO_PLAYBACK`: miniaturas HScene desbloqueadas e
  bloqueadas exibidas; **não houve reprodução HScene**;
- `REPLAY_GALLERY_NAVIGATION_NO_SCENE_ACTIVATION`: abas HScene/PlayData,
  paginação e retorno ao título; **não houve reprodução PlayData**;
- `PRIVATE_SAVEFILES_UNCHANGED_ON_DISK`: SaveFiles privados não alterados.

**Bloqueio explícito:** `PLAYDATA_UI_EMPTY despite private SLog slots [0, 1]`.
Há `SaveFile000.DAT` (519 bytes) e `SaveFile001.DAT` (1.378 bytes) no
`--documents` privado da sessão; a presença no disco **não implica** que o
modelo Replay/PlayData deva tratá-los como entradas disponíveis. Auditoria
READ-ONLY independente do worker16 confirmou a **causa no fixture de QA**:
`GlobalFlag.DAT` da sessão Replay tem 243 entradas, `EndClear=-1` e **zero**
chaves `FILMEngine/SaveFile...`; os dois SLog foram copiados de outro perfil
que tinha `GlobalFlag.DAT` com 37 entradas e as quatro chaves metadata
primária/`_Sub` dos slots 0 e 1 (tipo8), sem copiar essas flags junto. Os
dois SLog são byte-idênticos entre perfis, válidos e já carregados via UI
privada; `SchoolDaysSaveServiceRead.cpp` corretamente exige metadata primária
do mesmo perfil para marcar `occupied=true`. Relatório em
`qa-stage5-timeout-review-20261107/QA_STAGE5_PLAYDATA_EMPTY_R2_CAUSE_WORKER16.md`
e `qa_stage5_r2_playdata_empty_audit.json`. **Não é prova de defeito do
ReplayModel.** Novo teste físico deve gerar SaveFile **e** metadata em
`GlobalFlag.DAT` pela UI nativa no mesmo perfil com Replay disponível;
não mesclar flags à mão. Reprodução PlayData permanece pendente. Tampouco
há prova de mudança/restauração funcional de opções; até aqui, apenas
navegação não destrutiva. Screenshots, hashes e estados por etapa estão no
JSON e nos PNGs do mesmo diretório.

O executável L4 Release verificado tem SHA256
`379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`.
**CTest Release serial 67/67 PASS reexecutado neste exato hash**, incluindo
`SchoolDays.SaveLoadSamePage` e `SchoolDays.SaveProvenance`. Não inferir
compilação nova a partir desse CTest nem equivalência audiovisual/funcional
dos testes sintéticos com a matriz física. Etapa 5 segue aberta.

## ENDG6 ES/ON — epílogo Blue Smile 1× e cartão pós-CreateBG (07/11/2026)

Sessão exclusiva nova:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code6-es-on-20261107-1334`;
staged L4 EXE SHA256
`379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`,
`translations/es.json` SHA256
`17F0DB1490F97E84E1A83001EE50FEEE3D82F19749B3625C2F49EAF0FBA8495A`,
`--lxh on`, `--documents` exclusivo, 1366×768 software/DevBridge48573.

1. Runner privado `natural_mouse_lxh_audio_v3.py` concluiu **37/37**
   decisões por `mouse_move`/`click` via hit-test SDL com ack e
   `ChoiceSelected`, nenhum `choose`/`--node`/`debugJump`, nenhum timeout.
   Final KTRF legítimo `05/05-A2-F00`, EndRoll original seguido do epílogo
   L×H completo **a 1×**, retorno ao título tick8963, exit0,
   `PROCESS_STOPPED True`. `route-audit.json` independente:
   `valid=true`, `real_mouse_ack=37`, `video_open=1`,
   `returned_to_title=1`, `reasons=[]`, KTRF SHA
   `557830833AA3BB4A8EFBA82FE75964211491E9F02D60D6090DDA1D5174D4E745`.
2. `engine.log`: worker WMV pronto em **62ms** e OGG em **2ms**, ambos
   `ready=1 cancelled=0`; nenhum fallback watchdog detectado nesse trecho.
   EndRoll original termina tick5867, fade OUT registrado em 5866; L×H
   fade IN no tick5915, filme aberto tick5922 em velocidade 1×, fade OUT
   para SaveFeel no Next tick8963 e fade IN após 500ms, sem corte do
   countdown autoral de 30s. Amostras WMV 25/50/75/96% em ticks
   6653/7393/8123/8747, todas em 1×.
3. `lxh_final_card_after_CreateBG` efetivo no tick8880 com background
   `display-options/LxH/05-A2-F00-LXH.png` e nenhuma mídia ainda tocando;
   screenshot `review-code6-es-final-card-after-CreateBG.png` **inspecionado**:
   cartão inglês universal **“Blue Smile”** visível, SHA256
   `D7EFFF428B266406641805A53A92F3B903B011F0E0C986E9D48E9299703DC6D1`.
   Corrige a limitação da captura prematura EN/ON, sem invalidar aquela rota.
4. `globalflag-audit.json` independente `valid=true`: FlgH EndNo6,
   EndClear−1, `[End06]`−1, 243 entradas/6.678 bytes, SHA256
   `b72337455a217646e586885bb7f5981d19a75d3a5ed60b4c411cb737909643e2`.
   Nenhum SaveFile/sidecar criado nesta campanha de final.
5. `wasapi-audit.json`: endpoint compartilhado Samsung/NVIDIA,
   **142s / 12.191 pacotes**, PCM16 estéreo 48kHz, RMS médio
   **−36,94dBFS**, pico **−8,61dBFS**, atividade acima de −50dBFS em
   114 segundos, 888 frames de gaps reconstruídos; atividade digital
   detectada, **não** áudio do jogo isolado, escuta humana nem lipsync
   perfeito. Status do DevBridge contém labels canônicos EN para escolhas,
   o que não certifica visualmente cada legenda/opção ES.

**Matriz Stage5 atualizada: 7/28 finais ponta a ponta**, seis com mouse
SDL/timeouts naturais e somente ENDG5 EN/ON com `choose`; faltam
**21 combinações**. QA Replay HScene/PlayData playback, proveniência save
epílogo ON→OFF, SuperSkip, toggles Options e demais gates seguem abertos.
Não iniciar Etapa 6 nem 7, Android ou commit/push.

### ENDG5 EN/OFF — primeira tentativa SDL não certificada como 1×

O primeiro runner OFF v2 terminou a rota natural de 36/36 escolhas SDL
e retornou ao Title, mas o filme EndRoll **já abriu na velocidade 24×**
ao entrar em `05/05-A2-Z00` (tick42), antes de o botão 1× ser processado.
As amostras rotuladas 25/50/75/96% foram indevidamente capturadas no
**mesmo tick259** após a transição de velocidade. Assim, o `exit0` e o
`NATURAL_OFF_END_TO_END_RESULT` do runner original não bastam para PASS AV
1× desse cenário, e a matriz continua **7/28**. Evidência privada:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code5-en-off-20261107-1350/events.json`.
Não há conclusão de defeito no runtime: a troca ocorreu tarde demais num
EndRoll curto. Preparado apenas em scratch o runner
`natural_mouse_off_original_1x_v3.py`, com mudança da velocidade via MenuBar
antes da oitava escolha final, guarda de primeiro frame 1× e exigência de
quatro checkpoints 1× em ticks distintos; AST PASS. Reteste físico iniciado
em novo perfil isolado antes de considerar qualquer incremento de matriz.

### ENDG5 EN/OFF — reteste físico 1× auditado (07/11/2026)

Reteste completo isolado em
`D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-natural-code5-en-off-v3-20261107-1410`,
com a **mesma L4 Release** (SHA256
`379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`),
idioma EN, `--lxh off`, perfil `--documents` novo e DevBridge exclusivo.
Não reutiliza a tentativa AV invalidada acima nem edita material retail.

1. `natural_mouse_off_original_1x_v3.py`: **36/36** escolhas KTRF
   naturais com `mouse_move` + `click`, hit-test SDL e `ChoiceSelected`,
   sem `choose`, `--node`, `debugJump` ou timeout fabricado.
   Mudança de velocidade pelo próprio botão MenuBar antecipada na rota,
   com `early_original_one_x.reported=1.0` **antes** de chegar a
   `05/05-A2-Z00`. Primeiro filme real abriu **em 1×** no tick2,
   não em 24× como no primeiro intento.
2. Checkpoints **25/50/75/96% desse filme** em ticks **56/108/148/187**,
   todos 1× e **distintos**, EOF após tick187. A sequência autoral restante
   avançou continuamente 1× nos registros até tick6003, com retorno normal
   ao título e runner **exit0 / `PROCESS_STOPPED True`**. Os quatro PNGs
   são do filme inicial; os demais `ending_progress` comprovam avanço e
   velocidade do restante da cena, não quatro screenshots separados de toda
   a sequência de créditos.
3. `route-audit.json valid=true`, 36 SDL ack, final legítimo e retorno
   ao Title, sem rejeições. Auditor novo independente e *fail-closed*
   `audit_code5_off_1x_extra.py` resultou
   `code5-off-1x-audit.json valid=true`, exigindo mídia iniciar em 1×,
   quatro ticks distintos, fim no tick6003, zero L×H; como controle negativo,
   rejeitou corretamente a tentativa anterior por início 24× e quatro
   amostras no mesmo tick.
4. `engine.log`: zero `LxH/Media/`, `LxH fade armed`, `LxH entrance fade`,
   `LxH countdown fade` e `-LXH.png`. Nenhum sidecar L×H gerado.
   `globalflag-audit.json valid=true`, `EndNo=5`, `EndClear=-1`,
   `[End05]=-1`, SHA256
   `0f829203f59cc9cbf1ad5dc4e22feb4b516c370ee18b7ed0814458329464838a`.
5. `wasapi-audit.json`: Samsung/NVIDIA endpoint **compartilhado**, WAV
   PCM16 estéreo 48kHz **40s / 3.999 pacotes**, RMS médio **−38,0dBFS**,
   pico **−15,35dBFS**, atividade física digital identificada. Não
   equivale a escuta humana, isolamento do jogo ou lipsync perfeito.

**Matriz agora 8/28 cenários físicos ponta a ponta**, **7/28** por mouse
SDL e/ou timeout natural; ENDG5 EN/ON permanece a única exceção com
`DevBridge.choose`. **20 combinações** ainda pendentes, além de
Save L×H ON→Load OFF warning EN/ES, Replay/PlayData, SuperSkip,
QuickSave/Overwrite/Options, perfis, áudio e demais gates. Não iniciar
Launcher Etapa 6, RC Etapa 7, Android ou commit/push.

### Replay Data — SAVE genuíno no mesmo perfil, playback real e retorno (07/11/2026)

**Gate funcional PlayData PASS, sem alterar a matriz de endings 8/28.**
Perfil completamente isolado:
`D:/Dev/_temp_lxh_static_audit_worker8/qa-stage5-playdata-legit-prime-20261107-r2`
com EXE L4 SHA256
`379F48E60530D334B980A7D39E2C8C52DB32BA077CC2B8182B9B13858F218033`,
DevBridge48578/PID7992, `--documents` novo, L×H ON. Inicializou com
**apenas** FlgH autêntico ENDG6 de scratch anterior (EndClear−1,
7 chaves REP desbloqueadas), sem SLog nem sidecar pré-copiados.

1. **Save nativo:** Title Clear START por mouse SDL → Gameplay real,
   MenuBar SAVE → CMAP SaveLoad_Wide_Full página1/slot0 → editor original
   `TextInput=1` confirmado por screenshot → `Return` SDL no modal.
   `SaveFile000.DAT` 186 bytes, sidecar 242 bytes `lxh=0` porque a gravação
   foi fora do epílogo L×H, e GlobalFlag 6.828 bytes, todos escritos pelo
   jogo no MESMO perfil. `SaveFile000` SHA256
   `3ac96a8b2609b4d9fb4757688f62c013d80b3d005371108b57e7eedb64649244`;
   sidecar SHA256
   `d15c88c075d3fb64d708b6bb57890c04fc24db5ab7fbf4779bdc2d1d5b9478a2`;
   GlobalFlag SHA256
   `80ce51e400830ba3886fadaffdbc5606812cec2d61dc92d8c7602d5c94442854`.
   Parser SLog v1.02 + chaves `FILMEngine/SaveFile000` e `_Sub` tipo8 na
   MESMA FlgH, EndClear/REP intactos, PASS.
2. **Retorno e ocupação:** MenuBar TOPMENU → modal original
   `Return to Title Screen?` YES por SDL → Title Clear fotografado,
   Replay → HScene tab → Replay Data tab página1. Slot0 **visualmente
   ocupado** com data e história, ao contrário do fixture anterior R2
   que tinha SLog copiado mas FlgH sem SaveConfig. Capturas em
   `qa-stage5-timeout-review-20261107/playdata-resume-prime-20261107-r2/`.
3. **Playback PlayData real:** clique SDL na linha 0 ocupada abre
   `gameState=Gameplay`, `playDataOpen=true`, `scene=00/00-00-A00/A01`.
   Captura `real-playdata-gameplay-retry1.png` mostra vídeo/narrativa com
   legenda inglesa e chip azul `REPLAYMODE`; não é somente galeria.
4. **Retorno do replay:** o TOPMENU da MenuBar em PlayData usa
   `activateMenuAction(OpenMainMenuConfirm)` (`SchoolDaysRuntime.cpp`),
   portanto abre confirmação nativa `Return to Title Screen?` e **não**
   retorna imediatamente. Harness anterior presumiu fluxo direto de
   `activateControl(14)` e expirou, preservando evidência sem mutar assets.
   Captura modal real, clique SDL no `YES` autenticado da CMAP, `gameState=1`,
   `playDataOpen=false`, captura Title. Script scratch
   `qa_playdata_confirm_native_return_v1.py` resultou
   `PHYSICAL_PLAYDATA_NATIVE_RETURN_AND_SAVE_INTEGRITY_PASS`.
   Relatório final
   `qa-stage5-timeout-review-20261107/playdata-popup-confirm-prime-20261107-r2/qa_playdata_native_return_result.json`:
   `status=PASS`, **SaveFile/sidecar/GlobalFlag hashes idênticos** antes e
   depois de reprodução e retorno. A GUI privada PID7992 foi encerrada
   após o QA; nenhuma janela SchoolDays.exe permaneceu ativa.

**Limites e diagnósticos:** o primeiro harness R1 parou corretamente diante
de um frame preto de START; no R2 a captura posterior com barra SAVE
visível autorizou a gravação. `gameState=Title` pode preservar `scene`
anterior não vazia: a exigência indevida `scene==""` gerou falso bloqueio
no primeiro retorno. O segundo falso bloqueio veio da confirmação
TOPMENU não prevista. Os relatórios R2/resume individuais preservam
`BLOCKED`; o **gate combinado de evidência e retorno é PASS** pelo
resultado independente final, sem atribuir PASS àqueles harnesses.
HScene foi apenas navegada/miniaturas observadas; **playback HScene segue
pendente**. Não afirmar teste humano de áudio/lipsync, nem completar 28/28.
