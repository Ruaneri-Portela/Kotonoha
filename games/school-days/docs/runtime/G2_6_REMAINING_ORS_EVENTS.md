# G2.6 — inventário inicial dos eventos ORS restantes

Matriz levantada antes das alterações deste gate. `S` indica suporte explícito, `I` indica suporte indireto pelo componente, `—` indica lacuna. O resultado final e a validação serão acrescentados depois da implementação.

| Tipo | Parsed | Prepared | Activated | Ended | Reset | Seek/Rebuild | Tested |
|---|---|---|---|---|---|---|---|
| CREATE_BG | S | S (`Image::Register`) | I (janela no `Image::Render`) | I | S | I | parcial (G2.4) |
| PLAY_SE | S | S | S | S | S | S | S (G2.3A) |
| PLAY_MOVIE | S | S | S | S | S | S | S (G2.5) |
| WHITE_FADE | S | S | I (`Fade::Render`) | I | S | I | F1.0 |
| BLACK_FADE | S | S | I (`Fade::Render`) | I | S | I | F1.0 |
| PLAY_BGM | S | S | S | S | S | S | S (G2.3B) |
| PRINT_TEXT | S | S (ASS no construtor) | I (ASS clock) | I | parcial (textura) | I | parser somente |
| PLAY_VOICE | S | S | S | S | S | S | S (G2.3A/G2.4) |
| SkipFRAME | S | metadata | marcador | n/a | S | S | S (G2.2) |
| SetSELECT | S | S (`Prompt` no construtor) | I (janela) | I (timeout) | — | — | parser/routing |
| END_BGM | S | S | S | S | S | S | S (G2.3B) |
| END_ROLL | S | S | S | S | S | S | S (G2.5) |
| Next | S | metadata | fim de cena | n/a | S | S | S (G2.2) |
| MOVE_SOM | S | — | — | — | cursor | cursor | parser somente |

As lacunas deste gate são a ativação/ownership de CreateBG, o reset da escolha, a explicitação de MoveSom, e testes de PrintText/fade/marcadores. `SchoolDaysKtrfAppController` ainda observa o resultado do mesmo Prompt em todo frame, potencialmente submetendo a escolha mais de uma vez; o gate tornará o commit idempotente sem alterar o roteador KTRF.

## Contrato implementado

`CREATE_BG` mantém `BGS` como campo estrutural. O resolver procura `PATH.PNG` por componente, sem depender da comparação de case do NTFS; uma colisão de nomes equivalentes é recusada, não resolvida arbitrariamente. O PNG e seus grupos A/B/C são registrados na preparação, mas a base só é ativada no dispatch do `startTick`. `Image` mantém uma única identidade de base ativa. A troca de base torna os grupos antigos inelegíveis para novos binds e encerra os binds existentes no update seguinte. O desenho de uma base substituta limpa apenas a textura da camada com alpha zero e restaura a cor de desenho do renderer; não introduz overlay preto. No `endTick`, a base encerra sua janela e deixa o último frame no Canvas conforme o contrato de apresentação existente.

`PRINT_TEXT` continua usando a track ASS já presente. Speaker e texto do ORS passam sem transformação de conteúdo. `Start` e `Duration` são derivados dos `SceneTick` absolutos por `Kotonoha_SceneTickToMillisecondsCeil`; a posição da track é consultada pelo relógio da cena. O reset destrói a textura de legenda antiga em `Gameplay::Reset`; a track permanece e reavalia o tempo, permitindo seek antes, dentro e depois da janela, sem duplicar eventos. A prioridade de mesmo tick é a ordem textual do ORS, preservada pelo parser e pelo scheduler.

`SetSELECT` continua no `Prompt` existente, com janela em 24 Hz convertida pelo adapter. O parser mantém o tratamento local de `null`/`NULL`. `Reset` descarta o Prompt anterior, desregistra seu Canvas item e prepara um novo Prompt sem resposta. O resultado é entregue ao roteador uma única vez; a marca de entrega volta ao estado inicial no reset. Uma escolha já respondida não é reconstruída automaticamente ao seek: essa informação pertence ao snapshot de G3. `SkipFRAME` no mesmo tick continua um marcador independente, e `Next` permanece a única autoridade de fim da Current Scene.

`BlackFade` e `WhiteFade` continuam no `FadeRuntime` de F1.0: `q=floor(255*(tick-start)/(end-start))`, alpha OUT=`q`, IN=`255-q`. O adapter legado usa `relativeTick+1` nos dois extremos; essa base se cancela na diferença e coincide com o relógio `floor(ms*24/1000)+1`. Nenhum ajuste de `+1` voltou ao parser. A ponte legada foi mantida porque sua remoção não é necessária para este gate. CreateBG, EOF de mídia e `Next` não registram fades.

`MoveSom` possui lifecycle explícito de janela e é no-op intencional do hardware SOMCON opcional. O valor 1–5 continua no payload, sem criar áudio, imagem, hardware ou estado físico de snapshot. Um salto de ticks pode iniciar e encerrar o evento na mesma atualização sem bloquear a timeline.

| Tipo | Classificação | Reset/seek |
|---|---|---|
| CreateBG | VISUAL_WINDOW | base limpa e recurso ativo reativado pelo scheduler |
| PlayMovie, EndRoll | MEDIA_WINDOW | contrato G2.5 |
| PlayVoice | AUDIO_WINDOW | contratos G2.3A/G2.4 |
| PlaySe, PlayBgm, EndBGM | AUDIO_SLOT_WINDOW | contratos G2.3A/G2.3B |
| PrintText | TEXT_WINDOW | ASS reavalia tempo da cena; textura anterior descartada |
| BlackFade, WhiteFade | FADE_WINDOW | FadeRuntime existente é reiniciado e registrado de novo |
| SetSELECT | INTERACTION_TRIGGER/WINDOW | Prompt anterior descartado; resposta não inferida |
| MoveSom | LEGACY_NOOP_WINDOW | apenas cursor/janela, sem estado de periférico |
| SkipFRAME | MARKER | metadata independente |
| Next | SCENE_END_MARKER | única autoridade ORS de fim da cena |

## Validação

Build desktop Release e 10/10 testes CTest passaram. `SchoolDaysRemainingOrsTest` cobre ordem textual, janelas de texto/imagem, SetSELECT com `null` no mesmo tick de SkipFRAME, idempotência do resultado, os dois fades, os cinco valores MoveSom, saltos de tick, reset de cursor e um teste de pixels com CreateBG real da A01. Os testes anteriores de parser, timeline, áudio, BGM, A/B/C, vídeo e routing passaram sem alterações semânticas nesses subsistemas.

O audit read-only está em `G2_6_ORS_CORPUS_AUDIT.json`: 1.857 arquivos, 86.539 linhas, 86.536 eventos aceitos, 3 linhas malformadas/no-op, 281 MoveSom, zero headers desconhecidos. A distribuição por EventType está no mesmo JSON.

## Limites preservados

Sem snapshot G3, o seek não pode restaurar a resposta de uma escolha já resolvida; ele prepara o Prompt sem resposta. Layout/fonte original de legenda e escolhas, CMAP/UI, Save/Load, hardware SOMCON e os UNKNOWNs de mídia documentados nos gates anteriores continuam fora do escopo. O teste de pixels usa o renderer de software e dois PNGs reais; não substitui validação visual interativa da cena inteira.
