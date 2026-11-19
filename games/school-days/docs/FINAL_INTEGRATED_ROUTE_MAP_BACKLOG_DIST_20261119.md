# School Days HQ v1.02 / Kotonoha v3 — validação integrada final (19/11/2026)

**Natureza:** auditoria técnica e homologação **privadas**, sem executar o EXE
ou instalador comercial, alterar os 29 GPKs originais, sobrescrever partidas,
publicar remotes ou certificar a paridade visual absoluta. **GPU/OpenGL real e
comparação de percursos retail ainda exigem QA manual.**

## Estado real recuperado

- Worktree: `D:\Dev\_worktrees\school-days-menubar-cursor-20261119`;
  branch `fix/school-days-dist-backlog-20261119`; baseline anterior do hover
  `0c6e61eb22693c527d1416d4e58d26fa9c5605b0`, seguida do commit
  integrado do BackLog `f8a31452`; worktree limpa após a consolidação.
- Commits locais reaproveitados: `32af5e4e` (cursor da MenuBar), `9181cc5d`
  (launcher JSON/GPK), `2e02f9b9` (DIST sem Dialog Story e BackLog),
  `bea77ad6` (BackLog longo/multilíngue), `9eeb367e` (matriz Route Map e
  comparações nativas), `0c6e61eb` (hover Open/Close Route Map e diagnóstico
  da partida importada) e **`f8a31452`** (Unicode, falantes longos e perda de
  foco no BackLog/LOAD/RouteMap). Nenhum push feito nesta integração.
- DIST do usuário, preservada:
  `D:\Dev\Kotonoha-v3\dist\SchoolDaysHQ-Windows-DIST-Integrated-Final-20261119`.
  Contém **21 arquivos** no diretório `Save`, incluindo `GlobalFlag.DAT`.
- DIST corrigida para o hover, preservada separadamente:
  `D:\Dev\Kotonoha-v3\dist\SchoolDaysHQ-Windows-DIST-RouteMap-Hover-20261119`.
  `PRIVATE-BUILD-MANIFEST.json` identifica o commit `0c6e61eb`, **91/91
  hashes SHA-256 coincidem**, nenhum `Save`, `GlobalFlag.DAT`, `Config.DAT` ou
  `Packs` pessoal foi copiado. O executável tem SHA-256
  `C09336F879F0294A50E3CA362EF148AD79AF408AEBE161C1E4EECB24D466FFA5`.
- DIST para a validação integrada **após os achados adicionais do BackLog**:
  `D:\Dev\Kotonoha-v3\dist\SchoolDaysHQ-Windows-DIST-RouteMap-BackLog-QA-20261119`.
  **Produzida e verificada com 91/91 SHA-256**; manifesto do código
  `f8a31452` e executável `SchoolDays.exe` SHA-256
  `BAC53ECBAF5810B76717FF4417B7777F188875782D2E9C1B2F96488FF81785E6`.
  Não contém diretório `Save`, `GlobalFlag.DAT` ou `Config.DAT`; as duas
  distribuições anteriores ficaram intactas.
- O pacote antigo também conservou **91/91 hashes**. Os arquivos e hashes
  oficiais PT-BR HQ/L×H e `school-days-hq.ktnroute` são idênticos nas três
  distribuições. A rota KTRF é SHA-256
  `557830833AA3BB4A8EFBA82FE75964211491E9F02D60D6090DDA1D5174D4E745`.

## 1. Corrigido: estado visual Open/Close Route Map

**Causa concreta autenticada pelas imagens de `System.gpk`:** as texturas
base `SAVELOAD/LOAD.PNG` e todos os 16 `ROUTEMAP*/*.PNG` já trazem seus
botões em cinza. O atlas `SAVELOAD_CHIP.PNG` tem um sprite azul de hover no
controle LOAD **21**, e cada RouteMap CHIP tem um sprite azul no controle
**6**. O port desenhava ambos **sempre**. A correção em `0c6e61eb` preserva a
base retail e emite os overlays apenas se o CMAP seleciona o respectivo
controle. `RestoreSelection(21)` não ressuscita uma seleção de hover antiga
quando CLOSE/Escape retorna ao LOAD; `SDL_EVENT_WINDOW_MOUSE_LEAVE` limpa
seleções quando o cursor sai da janela.

**Arquivos alterados por `0c6e61eb`:**

1. `games/school-days/src/SchoolDaysSaveLoadRenderPlan.cpp`;
2. `games/school-days/src/SchoolDaysRouteMapRenderPlan.cpp`;
3. `games/school-days/include/Kotonoha/routing/SchoolDaysSaveLoadModel.hpp`;
4. `games/school-days/src/SchoolDaysSystemUi.cpp`;
5. `games/school-days/tests/SchoolDaysRouteMapMatrixTest.cpp`;
6. `games/school-days/tests/SchoolDaysSaveTest.cpp`;
7. `games/school-days/docs/ROUTE_MAP_HOVER_IMPORTED_SAVE_AUDIT_20261119.md`.

O teste `SchoolDays.RouteMapMatrix` verifica estado neutro, hover, remoção do
hover, restauração de LOAD sem destaque obsoleto, e seletor CMAP nas quatro
variantes de tela. O teste integrado `SchoolDaysSaveTest` verifica movimento
físico do mouse, sair da janela e restaurar hover no LOAD. **Demais sprites e
estados de nós não foram arbitrariamente modificados.**

## 2. Bolinhas pretas e o save antigo de 11%: o que foi demonstrado

Foram **lidos**, sem qualquer gravação, os arquivos `GlobalFlag.DAT` e os
11 saves `SaveFile*.DAT` da distribuição de origem e da integrada; também
há análise histórica de um backup mais antigo. O FlgH e SLog foram validados
com as mesmas regras de leitura do código do jogo.

| Evidência | Distribuição de origem | DIST integrada usada pelo usuário |
| --- | ---: | ---: |
| Flags FlgH | 250 | 252 |
| SceneKeys lidas | 213 | 215 |
| Progresso `floor(SceneKeys × 100 / 1825)` | **11%** | **11%** |
| SP não-zero global | 8 | 8 |
| SP episódios 1–2 | `SP100`–`SP103`, `SP200`, `SP201`, `SP204`, `SP205` | Idênticos |
| SP episódios 3–6 | **0** | **0** |
| Checkpoints SLog tag3 em 11 slots | Só os oito SP acima | Idênticos |

SHA-256 da `GlobalFlag.DAT` integrada inspecionada:
`0D22EA15144C55FD9B822D5D706A3D728A91A33E474202508A507E37C6E1C705`.
Saves posteriores efetivamente apontam a cenas dos episódios 3–6, inclusive
um final; alguns correspondem aos seletores `SP408`, `SP501`, `SP603` e
`SP616`, **nenhum gravado como SP/tag3**. Portanto **há lacuna histórica de
checkpoint nessa partida importada**. O Route Map está projetando fielmente
as flags disponíveis; **11% e um final não implicam desbloquear todos os
nós das cenas já lidas**. O usuário observou o mesmo estado no retail com
esses mesmos saves.

**Não demonstrado:** se os SP faltantes decorreram de callbacks não
disparados em uma build antiga, de um salto/replay ou de importação parcial.
A implementação atual registra `overflow.sdhq:callback_00` usando um
checkpoint SLog/tag3 e um SP global; a leitura do mapa consulta apenas SP
efetivamente persistidos. Nunca se deve inventar um tag3 a partir do capítulo
mais avançado ou do percentual. Para atribuir causa, usar uma **nova** partida
controlada com `--documents` isolado e verificar SP/tag3 antes/depois do
primeiro checkpoint do episódio 3. Preservar todos os dados do usuário.

## 3. Route Map: matriz de paridade e limites

| Área | Implementado/testado internamente | Prova original independente | Pendente de QA físico/retail |
| --- | --- | --- | --- |
| Seis episódios, 16 subpáginas | Todas as páginas; Standard/Wide/Wide Full/Wide Note | 16 bases + 16 CHIP + 64 CMAP, 96/96 recursos | Hover pixel a pixel, transições animadas |
| Nós e seletores | 161/161 hotspots; 161×4 ativações sintéticas | 161 hotspots CMAP, 162 registros BackRules para 161 SP únicos | 161 cliques retail e handoffs efetivos |
| `ROUTE`, páginas, história | 55 valores, seis grupos, 12 arms | 55/55 tabela de páginas; 55/55 CheckScript fallback, 161 SP em ordem; GetStory grupos `[1,3,2,13,13,23]` | SCENE overrides e predicados um a um |
| Checkpoint `SP428` | Primeira visita em ambas as ordens SCENE85/SCENE108, sem tag3 duplicado | Duas coordenadas nativas para mesmo SP | Precedência temporal em retail real |
| GlobalFlag sem SLog | Nó visualmente desbloqueado; salto sem tag3 proibido | Branch de falta tag3 no EXE retail | Quadros intermediários e estados visuais |
| Grafo | 1.857 NODE, 2.458 TRAN; 287 CHOI/485 opções; 22 ENDG | 1.855 nós com ORS físico, dois dispatchers, SP físicos alcançáveis ignorando predicados | Dois ORS retail-only, `04/04-C0-A05` sem transições; todas as escolhas/finais |
| Percentual de conclusão | Fórmula e limites 200/201/218/219/1825; 10/11/12/100% | **1.825/1.825 chaves** extraídas do `GetReadScriptCount` retail | Como e quando retail marca dispatchers em gameplay; execução 100% |

**Retificação importante de hipótese antiga:** a prova independente da rotina
original `_GetReadScriptCount@4` inclui os dispatchers internos
`03/03-B2-A00` e `03/03-KB-E00` e exclui os ORS físicos retail-only
`01/01-00-OP2` e `05/05-9O-B00`. Uma substituição experimental tinha sido
proposta, mas foi **refutada e revertida antes do commit**. O contador atual
é o que corresponde à DLL original (1.825 chaves); não reaplicar o patch
incorreto. `04/04-C0-A05` está isolada no grafo KTRF, mas isso não autoriza
inventar uma aresta. `school-days-hq.ktnroute` foi preservado intacto.

**Veredito:** cobertura estática elevada e comportamento correto para as
flags observadas, **não** prova de 100% de paridade visual/funcional.

## 4. Contrato permanente da DIST e BackLog

`KOTONOHA_DISTRIBUTION_BUILD=ON` compila fora do School Days o membro
`VNBacklog`, seus fluxos de criação/registro/eventos/update/render e seu
fallback. Falha de fonte/arte no BackLog próprio resulta em erro legível,
nunca numa janela alternativa "Dialogue history". A DEV mantém o fallback;
`SchoolDays.DistDebugAbsence` valida o contrato de compilação. `lib/` não
foi alterada para implementar esta regra.

O BackLog do jogo tem rolagem **dentro de uma entrada de mais de nove linhas**,
sem perder o começo/fim, preserva ordem e cores dos falantes e o wrapping de
texto traduzido. A rasterização TTF agora ajusta largura/altura
**proporcionalmente**, em vez de achatar glifos em altura fixa. Em resolução
de autoria 1280×720 o clip de texto termina antes de x=1180 (controles da
direita) e y=640 (faixa azul), com passo vertical de 57 px. Testes
`SchoolDays.BackLogGeometry` validam não sobreposição, múltiplas linhas,
600 caracteres, 4 proporções e caracteres PT-BR/RU/ZH (inclusive `｜` e
`《`).

**A auditoria read-only posterior ao hover revelou e levou à correção de
três casos de borda concretos:**

1. `WrapFittedBackLog` podia partir um par surrogate UTF-16 (como um emoji)
   exatamente no limite de linha. O wrapper e `appendRun` agora avançam por
   símbolos completos, mantendo ambas as unidades adjacentes em uma mesma
   linha; a métrica original das duas unidades é preservada.
2. Um nome de falante longo ultrapassava x=1180 e perdia caracteres; além
   disso, era truncado antecipadamente em 48 unidades. A linha do falante
   agora é preservada integralmente, projetada dentro da coluna e o raster
   SDL_ttf é ajustado uniformemente. Um teste com **52 letras W + emoji**
   recupera todo o conteúdo e verifica bounds, sem introduzir sobreposição.
3. O BackLog não limpava sua seta/Close azul ao perder foco ou quando o
   mouse deixava a janela. A ramificação do `Runtime` agora limpa a seleção
   para ambos os eventos, sem mudar registro ou scroll. O SystemUi aplica a
   mesma regra a LOAD/RouteMap.

`SchoolDays.BackLogGeometry` também verifica agora a reconstrução do texto
com **45 letras + emoji** em linhas separadas, integridade de pares UTF-16,
conversão de cada linha por `SDL_iconv_string` para UTF-8 e a ausência de
hover stale. A montagem de SDL_ttf/texture ainda não foi comparada pixel a
pixel no OpenGL real. O presenter reconstruía a superfície 2048×1024 a cada
frame antes destas mudanças; isso pode custar GPU/CPU em hardware lento,
mas não foi reescrito sem medição de regressão. QA visual com GPU real para
fontes, skins, fluidez e conteúdo de 8+ linhas continua exigida.

## 5. Validações reproduzidas e reaproveitadas

| Verificação | Resultado e contexto |
| --- | --- |
| DEV Release | Compilação concluída no cache `build/cursor-qa`, `KOTONOHA_DISTRIBUTION_BUILD=OFF` |
| DIST Release | Compilação concluída no cache `build/json-dist-qa`, DIST=ON e DEV_CONTROL=OFF |
| Baseline anterior | DEV 9/9; DIST 7/7 direcionados: reaproveitados sem repetir bateria integral |
| BackLog/Route Map integrados | DEV e DIST: testes de geometria, sessão/oracle e matriz anteriormente PASS; DIST focados 5/5 |
| Hover novo | `SchoolDays.RouteMapMatrix` DEV PASS e DIST PASS, 4 variantes, botões 21/6 e retorno |
| Negativo DIST | `SchoolDays.DistDebugAbsence` e `SchoolDays.SaveLoadSamePage` PASS, DIST focados 3/3 |
| Teste integrado LOAD/SAVE | `SchoolDaysSaveTest` DEV, SDL software/dummy e arquivo retail somente para leitura: **exit 0** com arte privada Display Options correta e documentos temporários |
| Falha de ambiente anterior | O primeiro CTest `SchoolDays.Save` falhou por não encontrar o overlay de Display Options na pasta de teste. O mesmo binário/teste com overlay privado legítimo passou; não interpretar a falha de fixture como regressão de código |
| BackLog adicional, DEV | Build Release aprovada; `SchoolDays.BackLogGeometry` PASS e `RouteMapMatrix` PASS. `SaveLoadSamePage` estava não gerado neste cache; foi compilado e passou em execução direcionada posterior |
| BackLog adicional, DIST | Build Release aprovada; `DistDebugAbsence`, `BackLogGeometry`, `RouteMapMatrix`, `SaveLoadSamePage` **4/4 PASS** |
| Conversão UTF-16 → UTF-8 | Teste com speaker e fala contendo emoji: build/teste `BackLogGeometry` atualizado PASS na DEV e DIST |
| Hover com perda de foco | Integração DEV `SchoolDaysSaveTest` usando arte privada correta e documentos temporários: **exit 0** depois do evento SDL de perda de foco |
| Nova DIST, inicialização | Smoke da **nova DIST f8a31452** em cópia temporária externa ao repositório, com `--documents` isolado e SDL dummy/software: PT-BR GPK **exit 0**, PT-BR JSON **exit 0**; cópia de smoke eliminada após validação |
| Integridade artefatos | 91/91 hashes verificados nas três DISTs privadas; original com 21 arquivos Save e GlobalFlag SHA idêntico preservados |

Não se reexecutou jogo comercial original, CTest completo ou stress sem
mudança material. Nenhuma das novas mudanças tocou SetSELECT/Feel/AUTO,
Skip, Back, Replay, NextChoice, persistência/saves duráveis, sete epílogos
L×H, traduções, estilos, MenuBar, Android, motor `lib/`, arquivos retail ou
o grafo KTRF. Ausência de diff não substitui testes completos futuros, mas
evita regressões causadas por edições estranhas ao objetivo.

## 6. Homologação local e pendências físicas

**Pasta correta**: `D:\Dev\Kotonoha-v3\dist\SchoolDaysHQ-Windows-DIST-RouteMap-BackLog-QA-20261119`.
Executar `INICIAR-QA-OPENGL-ISOLADO.cmd`. Seu script define
`--renderer opengl`, `--documents <nova DIST>\qa-documents` e perfil
`qa-settings.json`, com os GPK retail permanecendo na instalação externa.
Não copiar saves reais para a pasta nem substituir a distribuição anterior.

Roteiro final de QA: observar os dois botões neutros e azuis sob hover;
fechar por clique e Escape, sair da janela e reentrar; percorrer as 16 páginas
do Route Map e os estados de nó no perfil isolado; em nova partida passar
por um callback_00 do episódio 3 e confirmar novo SP/tag3; comparar o
BackLog com textos curtos, PT-BR longo (8+ linhas), ZH/RU, quatro proporções,
controles laterais e barra azul; validar cursor na MenuBar, escolhas/Feel,
Replay/Save/Load e um handoff L×H. O retail deve ser usado **somente pelo
usuário** para comparação visual autorizada, nunca executado pela auditoria.

Sem visualização física GPU/OpenGL de toda essa matriz, **não declarar
reprodução retail perfeita nem publicação pública pronta**. Referências
detalhadas: `ROUTE_MAP_FIDELITY_AUDIT_20261119.md`,
`ROUTE_MAP_HOVER_IMPORTED_SAVE_AUDIT_20261119.md` e
`FINAL_INTEGRATED_SCHOOL_DAYS_QA_20261119.md`.
