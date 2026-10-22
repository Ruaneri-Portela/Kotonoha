# G2.3B — PlayBgm e EndBGM

**Resultado:** implementação de `PlayBgm`/`EndBGM` no domínio de `SceneTick` a 24 Hz. A base é G1.9 (`Research_engine/13_COMPLETE_RUNTIME_RESEARCH/ending_events/reports/G1_9_RESULT.md`): o EXE original usa o mesmo tipo-base de áudio, mas slots distintos (`host+0x2BC` para `PlayBgm`, `host+0x2B4` para `EndBGM`). `EndBGM` toca `<PATH>.ogg` uma vez; não é um comando de parar o BGM normal.

## Contrato implementado

- `SchoolDaysBgmResources` deriva `<PATH>_int.ogg` e `<PATH>_loop.ogg` para `PlayBgm`, ou `<PATH>.ogg` para `EndBGM`. O `PATH` ORS permanece opaco, inclusive para recursos em `Se03`.
- `ResolveSchoolDaysBgmAsset` obtém o nome efetivo de cada componente no diretório extraído. Isto conserva a grafia lógica do ORS e encontra `.OGG`/diretórios maiúsculos em Android. Se não houver correspondência, mantém o path solicitado para o loader falhar sem substituição implícita.
- `SchoolDaysBgmSlots` conserva ownership lógico independente para normal e ending. A mídia é preparada desabilitada até o evento chegar ao `START`. Um evento novo substitui só o ocupante do mesmo slot.
- `Audio::AddIntroLoopMedia` cria um pipe com dois decoders. No EOF real do intro, o callback passa ao loop; no EOF do loop, busca o início e o repete. Isso reproduz o contrato externo do par de recursos. O teste usa WAV sintético curto para verificar a troca e a repetição no mesmo caminho FFmpeg, sem depender de um dispositivo físico.
- `EndBGM` cria um decoder único sem loop e seu slot é limpo em `END`. O EOF físico anterior não recria a mídia. O `END` de `PlayBgm` também remove o pipe nesta implementação como limpeza segura da cena; o cleanup isolado desse evento no original ainda não foi provado.
- `Reset` remove todos os pipes físicos antes de reconstruir a timeline. Apenas o último evento semanticamente ativo de cada slot pode ser reconstruído. Eventos encerrados ou substituídos não reaparecem. No meio de um `PlayBgm`, a reconstrução reinicia no intro; seek sample-exact intro/loop continua fora do contrato fechado.
- `START` e `END` são convertidos de `SceneTick` pelo adapter central `Kotonoha_SceneTickToMillisecondsCeil`; não há timer de duração de arquivo nem conversão incremental de 41 ms.

## Audit dos assets

Comando reproduzível: `python tools/audit_school_days_bgm_assets.py assets docs/school-days-runtime/G2_3B_ASSET_AUDIT.json`. O relatório agrega cada recurso lógico com path real, contagem e exemplos de cenas; o script percorre todas as linhas ORS. Foram examinados **1.857 ORS** e **3.043 referências de recurso**: 1.488 loops de `PlayBgm`, 1.102 intros presentes, 386 intros ausentes e 67 recursos de `EndBGM` presentes. Todas as 2.657 referências presentes diferem em maiúsculas/minúsculas da grafia ORS; não há colisões de nomes equivalentes ignorando case. As 386 ausências são somente seis paths lógicos repetidos: `sdbgm20` (218), `sdbgm29` (69), `sdbgm28` (41), `sdbgm33` (26), `sdbgm18` (20) e `sdbgm14` (12), todos sob `BGM/SD_BGM/`. Quando falta `_int`, o port não toca o `_loop` sozinho: G1.9 não provou esse fallback no original. A timeline prossegue e o loader registra a ausência.

## Validação e limites

- Build desktop Release: `cmake --build build-win --config Release --target Kotonoha SchoolDaysBgmEventTest --parallel 6`.
- `ctest --test-dir build-win -C Release --output-on-failure -R '^(OrsParserFidelityTest|SchoolDaysSceneTimelineTest|SchoolDaysAudioEventTest|SchoolDaysBgmEventTest|SchoolDaysHandoffRouterTest|SchoolDaysFullRouterTest|SchoolDaysDevCheckpointTest)$'`: sete testes aprovados.
- `SchoolDaysBgmEventTest` cobre resolução intro/loop e one-shot, grafia real de arquivo, slots independentes e replacement, rebuild lógico, conversão de 24 Hz, preparo sem ativação, falha segura de recurso, limpeza de pipes, intro→loop no EOF, repetição do loop e EOF one-shot. O teste físico usa backend SDL dummy e PCM WAV sintético; não houve smoke auditivo com OGG real nesta rodada.

**Ainda não demonstrado no jogo original:** coexistência audível simultânea dos dois slots, política exata do decoder no elo intro→loop, cleanup individual de `PlayBgm` no `END`, fallback original para `_int` ausente e posição sample-exact após seek. A separação lógica não presume que `EndBGM` pare `PlayBgm`; o mixer atual aceita pipes distintos no canal `BGM`.

Arquivos do gate: `include/Kotonoha/SchoolDaysBgmEventState.hpp`, `include/Kotonoha/SchoolDaysBgmAssetResolver.hpp`, `include/Kotonoha/components/{Audio.hpp,Events.hpp}`, `include/Kotonoha/renders/AudioRender.h`, `src/components/{Audio.cpp,Events.cpp}`, `src/renders/AudioRender.c`, `tests/SchoolDaysBgmEventTest.cpp`, `tools/audit_school_days_bgm_assets.py`, `CMakeLists.txt` e o relatório JSON deste diretório. Voz, SE, EndRoll e Android launcher não foram modificados pelo gate.
