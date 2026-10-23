# G2.2 — timeline de 24 Hz e marcadores da Current Scene

**Escopo:** núcleo temporal e agendamento ORS. Evidência original: `Research_engine/13_COMPLETE_RUNTIME_RESEARCH/next_skipframe/reports/G1_7_RESULT.md`, no diretório local do jogo. Este gate não conecta o controle nativo de skip à UI.

## Contrato implementado

`Kotonoha_SceneTick` é o tempo canônico, relativo à Current Scene, para os eventos ORS. O parser calcula `tick = (minutos * 60 + segundos) * 24 + frame`. Portanto `00:00:23 = 23`, `00:01:00 = 24`, `01:02:09 = 1497` e `01:09:00 = 1656`. A conversão para microssegundos ou milissegundos é sempre feita a partir da posição absoluta do tick, sem somar durações arredondadas. O relógio SDL e as APIs existentes de mídia continuam em milissegundos; `SchoolDaysSceneTime.h` é o adaptador único entre esses domínios. O milissegundo escolhido para ativar um tick é o primeiro inteiro **igual ou posterior** ao seu instante racional.

`startTick` e `endTick` são os campos canônicos do evento. Os campos legados `start` e `end` ainda guardam o número empacotado `MM*60000 + SS*1000 + FF*10`, pois `FadeRuntime` de F1.0 ainda o consome. Eles não governam o agendamento nem o fim da cena. O parser ordena pelo tick canônico e mantém a ordem textual quando dois eventos começam no mesmo tick; não há ajuste artificial de `+1` no timestamp ORS. Um campo de frame maior que 23 é carregado aritmeticamente, como ocorre em um `PrintText` distribuído de `01-00-E01`.

`SchoolDaysSceneTimeline` mantém `nextTick` e `skipFrameTick` separados. `AdvanceTo(currentTick)` libera, na ordem do ORS, todos os eventos ainda não processados até o tick atual, inclusive quando o relógio atravessa vários ticks em uma única atualização. A varredura é limitada a `Next`. `CheckEnd` só fica verdadeiro depois de o scheduler alcançar e processar o tick de `Next`; `SkipFRAME` não encerra nem suprime os eventos posteriores. O gatilho continua chegando à transação de cena existente por `Gameplay::Main`, sem alteração de `SchoolDaysSceneTransaction`, `PresentedFrame` ou `FadeRuntime`.

O alvo conhecido de **“Pular para o final”** está disponível somente como helper de núcleo, sem botão conectado: se `currentTick < skipFrameTick < nextTick`, o alvo é `max(0, skipFrameTick - 24)`; se `skipFrameTick == nextTick` e o relógio ainda não chegou lá, o alvo é `nextTick`. Para `currentTick >= skipFrameTick` com marcadores diferentes, o helper devolve ausência de alvo: a ação original após esse ponto continua **UNKNOWN**. O outro botão `SKIP` também não faz parte deste gate.

`ResetToTick(target)` reposiciona o cursor puro: eventos até `target` ficam consumidos e os posteriores voltam a ser elegíveis. O caminho de `Event::Reset`, usado quando a mídia existente é recriada após seek para trás, usa `RebuildActiveAt(target)`: eventos cuja janela contém o alvo podem ser registrados novamente; os já encerrados continuam consumidos. `Restart` libera todos os eventos para uma cena reiniciada. Esses métodos não reconstruem efeitos complexos de mídia ou escolhas passadas. A origem do `sceneBase` absoluto do EXE original é separada e permanece **UNKNOWN**; o runtime deste gate usa somente ticks relativos.

## Antes e depois

Antes, `start/end` usavam uma codificação numérica legada, interpretada em partes do runtime como milissegundos; `SkipFRAME` e `Next` sobrescreviam o mesmo `lastTime`, e `CheckEnd` comparava o relógio com esse valor. Agora o scheduler e o fim de cena usam ticks de 24 Hz; `lastTime` é apenas uma duração em milissegundos derivada de `Next` para os controles que já a exibem. Os componentes de mídia recebem tempos em milissegundos convertidos no ponto de registro. A janela existente de preparo de mídia com até dez segundos de antecedência foi mantida por `eventPrepared`, separada do cursor `eventTouched` de despacho por tick. Assim, registrar um recurso futuro não torna `Next` elegível antecipadamente.

## Arquivos e validação

Alterados: `CMakeLists.txt`, `include/Kotonoha/parsers/Ors.h`, `src/parsers/Ors.c`, `include/Kotonoha/components/Events.hpp` e `src/components/Events.cpp`. Novos: `include/Kotonoha/SchoolDaysSceneTime.h`, `include/Kotonoha/SchoolDaysSceneTimeline.hpp` e `tests/SchoolDaysSceneTimelineTest.cpp`.

O teste G2.2 cobre conversões, A03 (`SkipFRAME=1497`, `PlayVoice=1567`, `Next=1656`), marcadores iguais, alvo `1240 → 1473`, ausência de alvo pós-`SkipFRAME`, saltos de vários ticks, ordem textual e reset/seek. Foram executados o build desktop Release de `Kotonoha` e cinco testes Release (parser G2.1, timeline G2.2 e três testes de routing), todos aprovados. O audit do corpus real em `assets/` reproduziu 1.857 ORS, 86.539 linhas, 86.536 eventos aceitos, 281 `MoveSom`, 149 vozes com campos opcionais vazios, três linhas malformadas/no-op e zero headers desconhecidos.
