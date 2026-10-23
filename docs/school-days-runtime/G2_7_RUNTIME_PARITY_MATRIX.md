# G2.7 — Auditoria transversal do runtime ORS

HEAD inicial: `9e4820f79d92a099d8a86abd73742edc9eb15e32` na branch `research/ktrf-binary-v0.1-20261005`.

## Invariantes congelados antes da validação

1. Tempo canônico: `SceneTick` relativo a 24 Hz; `(min*60+sec)*24+frame`.
2. O parser não desloca eventos em `+1`; empates de START seguem ordem textual.
3. Preparar recursos não os ativa nem os apresenta.
4. `Next` é a única autoridade ORS de fim da Current Scene; `SkipFRAME` é metadata independente.
5. Troca de recurso/cena não gera preto, branco ou fade; apenas BlackFade/WhiteFade registram fade.
6. Resolvers preservam a identidade lógica e buscam a grafia real em filesystem sensível a case, sem fallback inventado.
7. Reset/seek limpam ownership físico anterior; estado semântico não depende de ponteiros AV, SDL, GPU ou thread.

Esta matriz será preenchida após o audit do corpus, inspeção dos consumidores e regressões. Estados de célula: `YES`, `N/A`, `EXPLICIT_NOOP`, `DEFERRED_G3` ou `UNKNOWN_NONBLOCKING`.

## Matriz master — School Days HQ v1.02

`YES` significa que o caminho existe e foi conferido por código e regressão. `N/A` significa que o conceito não se aplica. As notas após a tabela delimitam o que os testes não provam.

| EventType | Parse | Prepare | START/dispatch | Active | END | Reset | Seek/Rebuild | Next impact | External effect | Tests |
|---|---|---|---|---|---|---|---|---|---|---|
| CREATE_BG | YES | YES | YES | YES | YES | YES | YES | N/A | YES: PNG/base e grupos ABC | G2.6, G2.7 |
| PLAY_SE | YES | YES | YES | YES | YES | YES | YES | N/A | YES: slot SE 0–8 | G2.3A, G2.7 |
| PLAY_MOVIE | YES | YES | YES | YES | YES | YES | YES; PTS exato `UNKNOWN_NONBLOCKING` | N/A | YES: WMV | G2.5, G2.7 |
| WHITE_FADE | YES | YES | YES | YES | YES | YES | YES | N/A | YES: overlay branco | F1.0, G2.6, G2.7 |
| BLACK_FADE | YES | YES | YES | YES | YES | YES | YES | N/A | YES: overlay preto | F1.0, G2.6, G2.7 |
| PLAY_BGM | YES | YES | YES | YES | YES | YES | YES; seek sample-exact `UNKNOWN_NONBLOCKING` | N/A | YES: intro/loop no slot normal | G2.3B, G2.7 |
| PRINT_TEXT | YES | YES | YES | YES | YES | YES | YES | N/A | YES: track ASS | G2.6, G2.7 |
| PLAY_VOICE | YES | YES | YES | YES | YES; cauda `UNKNOWN_NONBLOCKING` | YES | YES; seek sample-exact `UNKNOWN_NONBLOCKING` | N/A | YES: voz/PCM/ABC por key | G2.3A, G2.4, G2.7 |
| SkipFRAME | YES | N/A | YES | N/A | N/A | YES | YES | N/A | YES: metadata de skip | G2.2, G2.7 |
| SetSELECT | YES | YES | YES | YES | YES | YES | `DEFERRED_G3` para resposta histórica | N/A | YES: Prompt e uma entrega KTRF | G2.6, G2.7, routing |
| END_BGM | YES | YES | YES | YES | YES | YES | YES; seek sample-exact `UNKNOWN_NONBLOCKING` | N/A | YES: OGG one-shot no slot ending | G2.3B, G2.7 |
| END_ROLL | YES | YES | YES | YES | YES | YES | YES; PTS exato `UNKNOWN_NONBLOCKING` | N/A | YES: WMV no backend de movie | G2.5, G2.7 |
| Next | YES | N/A | YES | N/A | N/A | YES | YES | YES: único fim ORS da cena | YES: transação existente | G2.2, G2.7, routing |
| MOVE_SOM | YES | N/A | EXPLICIT_NOOP | EXPLICIT_NOOP | EXPLICIT_NOOP | YES | YES | N/A | EXPLICIT_NOOP: SOMCON | G2.6, G2.7 |

Os `YES` de seek indicam coerência lógica do evento e descarte/recriação no caminho `Reset`/`RebuildActiveAt`. Eles não prometem posição de decoder sample/frame-exact. `SetSELECT` pode ser recriado como **pendente**; a resposta de uma escolha concluída exige estado G3. O FadeRuntime trata o tick final como inclusivo, conforme F1.0; janelas comuns são `start <= tick < end`. `SkipFRAME` e `Next` não são janelas.

## Corpus e invariantes globais

`SchoolDaysRuntimeCorpusAudit` chama o parser C real para cada ORS e grava [G2_7_RUNTIME_CORPUS_AUDIT.json](G2_7_RUNTIME_CORPUS_AUDIT.json), com uma entrada por cena: número de linhas/eventos, tipos, ticks Next/SkipFRAME, escolhas, recursos e linhas rejeitadas. Resultado: **1.857 arquivos; 86.539 linhas; 86.536 eventos aceitos; 3 rejeitados; 0 unknown**. Contagens por tipo: BLACK_FADE 847; CREATE_BG 13.024; END_BGM 67; END_ROLL 67; MOVE_SOM 281; Next 1.857; PLAY_BGM 1.488; PLAY_MOVIE 2.041; PLAY_SE 3.551; PLAY_VOICE 30.413; PRINT_TEXT 30.484; SetSELECT 287; SkipFRAME 1.857; WHITE_FADE 272.

Cada ORS tem exatamente um Next e um SkipFRAME. Em 1.570 cenas os marcadores coincidem; em 287, SkipFRAME < Next, sempre com um SetSELECT no mesmo tick de SkipFRAME; não há SkipFRAME > Next. O fixture misto G2.7 contém os 14 headers, compara avanço de um tick por vez com salto até Next, verifica ordem textual PrintText→PlayVoice, choice pendente e a persistência da cena após EndRoll/MoveSom. Repetiu reset/rebuild 64 vezes sem duplicar o dispatch. O fixture A03 anterior confirma 1497/1497/1567/1656 para SkipFRAME/SetSELECT/PlayVoice/Next. O parser não readicionou nudge `+1`.

## Recursos e ownership

O [audit de recursos](G2_7_RESOURCE_AUDIT.json), produzido por `tests/SchoolDaysResourceParityAudit.py`, examinou **52.139 referências físicas** aceitas pelo formato tabulado, por componentes do path. Encontrou zero colisões de grafia casefold. CreateBG: 13.024 referências, 121 diferenças de case e **1 PNG ausente** (`Event01/01-00/01-00-T00/01-00-T00-009.PNG`). PlaySe: 3.551 referências, **1.843 diferenças de case**. PlayVoice: 30.413 referências, 1 diferença de case e 149 ausências (139 paths únicos). PlayBgm: 2.976 componentes intro/loop, 386 referências ausentes correspondentes a **seis `_int.ogg` distintos**, já conhecidos; nenhum fallback foi criado. PlayMovie 2.041, EndBGM 67 e EndRoll 67 resolvem as diferenças de case existentes. O audit não prova ativação de uma mídia ausente; ele classifica apenas o contrato de path do corpus.

Uma divergência bloqueante foi corrigida em G2.7: Voice/SE ainda enviavam a grafia lógica diretamente ao backend. Agora passam pelo mesmo resolver por componentes de BGM/WMV, preservando a lógica `UNC_` preexistente. O teste usa dois recursos distribuídos cujo case difere do ORS. O resolver de imagem já rejeita colisões; o resolver compartilhado de áudio/vídeo prefere match exato e procura equivalente sem case. Como o corpus não apresenta colisões, a ausência de diagnóstico de ambiguidade nesse resolver é `UNKNOWN_NONBLOCKING` para v1.02, não um fallback de recurso.

Ownership lógico: PlaySe substitui somente seu slot 0–8; PlayBgm e EndBGM têm slots diferentes; o EOF do intro normal troca para o loop, sem rotear. Movie/EndRoll possuem decoder por evento; EOF e END não chamam routing. CreateBG tem uma base ativa; binding ABC só consulta grupos dessa base e key. `MoveSom` não cria pipe, imagem ou acesso serial/USB. Recursos preparados continuam desabilitados/invisíveis até seu START. O único caminho ORS de scene-end é `Event::CheckEnd` consultando Next; a transação F1.3 não foi alterada.

## Reset, seek e consumidores de tempo legado

`Event::Reset` remove todos os pipes de áudio, reseta decoder de vídeo, descarta grupos ABC/Imagem, MovieEvent, slots, MoveSom e Prompt anterior. `Gameplay::Reset` limpa textura de legenda e FadeRuntime. O scheduler recomeça ou executa `RebuildActiveAt(target)`; eventos encerrados não são ressuscitados, e os ativos voltam a ser elegíveis para preparo/dispatch. O Canvas conserva deliberadamente o último frame no caminho de seek dentro da mesma cena até haver novo produtor; em troca de cena a transação F1.3 administra essa retenção. `SeekForward` desloca o relógio e cruza eventos de uma vez; pipes antigos de Voice/SE ficam silenciosos após END até replacement/reset, política legada G2.3A cujo cleanup original individual é `UNKNOWN_NONBLOCKING`.

`LegacyTimeConsumers`: `src/parsers/Ors.c` ainda preenche os campos packed `event.start/end`; **o único consumidor direto dos campos packed no Event Runtime é o bridge de `Fade::Register` em `src/components/Events.cpp`**. `FadeRuntime` converte a base interna separada `+1` conforme F1.0. `Event::lastTime` é um adaptador em milissegundos derivado de Next para exibição/controle legado. Audio/Image/VideoRender têm seus próprios intervalos em milissegundos do backend; nenhum scheduler ORS novo compara os campos packed.

## Smoke real e regressões

Build desktop Release completo: `cmake --build build-win --config Release --parallel 6` passou. Suíte inteira: `ctest --test-dir build-win -C Release --output-on-failure` passou **12/12**, falhas 0, skipped 0. O audit do corpus foi executado novamente nessa suíte. ASan não estava configurado para o build MSVC; o fixture lógico de reset/seek repetiu 64 ciclos, além dos resets dos testes de mídia já existentes.

O executável Release foi iniciado em modo KTRF com `-K build/ktrf/school-days-hq.ktnroute assets -p assets/ -s assets/styles.skot`. A janela SDL/GPU abriu, A00 apresentou WMV, transicionou pelo Next para A01 e A01 apresentou vídeo, legenda e composição visual em capturas da própria janela. O log registrou `[SD-COMPOSITOR] capture presented-frame`, `prepared automatic boundary`, `scene swap transition=0 destination=00/00-00-A01`, `commit reason=current-scene` e `incoming primary visual ready; release outgoing composition`. Não foi observado crash nem frame preto nas capturas. O processo foi fechado pela janela e saiu com código 0. As capturas privadas `build-win/g2_7_a00.png`, `build-win/g2_7_a00_late.png`, `build-win/g2_7_a01_early.png` e `build-win/g2_7_a01_bg_abc.png` são geradas localmente e **não entram no commit**. O áudio inicializou e mídia foi decodificada, mas não houve verificação auditiva independente; não se afirma paridade perceptual de mixagem neste gate. FFmpeg emitiu avisos de timestamps descartados e um aviso de PTS/DTS inválidos, sem interromper a apresentação; o impacto sample/frame-exact permanece `UNKNOWN_NONBLOCKING`.

## G3 handoff e limites preservados

| Classe | Estado | Tratamento futuro |
|---|---|---|
| SEMANTIC | SceneKey, SceneTick, routing vars, cursor/choice history, identidades lógicas de recursos, ownership de slots, key/counter/index ABC | serializar apenas o necessário em G3, sem salvar endereços |
| DERIVED | ActivityRecords PCM, paths físicos resolvidos, textura ASS, posição reconstruída de decoder | regenerar/recriar a partir dos recursos e estado semântico |
| PHYSICAL | `SDL_Texture*`, AV/decoder pointers, pipes SDL, mutexes, threads | nunca serializar; destruir e recriar |

`DEFERRED_G3`: resposta de escolha já resolvida após seek, routing vars, Current Scene, cursor de evento, persistência de slots/ABC, contrato de reconstrução de mídia e formato SaveFile. Nenhum snapshot foi implementado aqui.

`UNKNOWN_NONBLOCKING`: origem de sceneBase original; clique de “Pular para o final” após SkipFRAME e botão SKIP separado; PlayMovie numeric !=0; guarda/destrutor individual original de EndRoll; seis intros BGM ausentes e eventual fallback original; seek sample-exact do BGM e frame-exact do WMV; cleanup original isolado de PlayBgm/SE; cauda legada `+1000 ms` de Voice; duas vozes simultâneas na mesma ABC key; trio 009 sem key; variação entre capturas MAK; hardware SOMCON; tratamento de colisão casefold em recursos de áudio/vídeo fora do corpus v1.02.

## Fechamento

Os gates G2.1 parser, G2.2 timeline, G2.3A Voice/SE, G2.3B BGM/EndBGM, G2.4 PCM/ABC, G2.5 Movie/EndRoll, G2.6 eventos restantes e G2.7 matriz transversal passaram nos contratos delimitados acima. Nenhum header aceito está sem runtime definido. As alterações locais de Android, `build-win/` e `docs/school-days-routing-validation/evidence/` não pertencem ao commit G2.7.

**G2.7 — PASS**

**G2 — EVENT RUNTIME COMPLETION — CLOSED**
**School Days HQ v1.02 ORS runtime baseline closed.**
