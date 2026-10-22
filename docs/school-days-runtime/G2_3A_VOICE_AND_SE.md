# G2.3A — PlayVoice e PlaySe

Escopo: ativação de áudio por eventos ORS de School Days HQ v1.02. A evidência de origem para `PlayVoice.NUMERIC` e `PlaySe.NUMERIC` está no relatório local `Research_engine/13_COMPLETE_RUNTIME_RESEARCH/event_payloads/reports/G1_8_RESULT.md`. Este gate não implementa BGM, EndBGM, A/B/C ou a UI de Options.

## Fluxo e unidade temporal

O scheduler G2.2 entrega eventos pelo `startTick` relativo de 24 Hz. `SchoolDaysSceneTime.h` converte `startTick` e `endTick` em milissegundos absolutos para o backend existente. O preparo antecipado de recursos continua separado da ativação: `Audio::AddMedia(..., enabledAtRegistration=false)` carrega o recurso e cria um pipe mudo; o evento só habilita o pipe quando `AdvanceTo(currentTick)` o despacha. O gate usa `SDL_AtomicInt` para comunicar essa habilitação à thread de áudio, sem fazer o callback alterar o estado lógico do evento. As demais chamadas de `AddMedia` mantêm o default habilitado.

## PlayVoice

O setting `schoolDaysMenVoiceEnabled` vive no `Kotonoha_Game`, que persiste entre Current Scenes. O padrão é **true**, compatível com a reprodução de voz anterior. `Kotonoha_IsMenVoiceEnabled` e `Kotonoha_SetMenVoiceEnabled` são a API de core; nenhuma tela de Options foi conectada. No START, NUMERIC `0` ignora o setting. Qualquer valor não zero exige `MenVoice=true`, conforme o teste do original. Se estiver desabilitado, o pipe preparado é removido e a voz não começa. Os 149 casos com NUMERIC e KEY vazios continuam como `0` e `""` pelo parser G2.1; a tentativa de carregar áudio depende somente de PATH. Falha de carga devolve ponteiro nulo e não bloqueia a timeline.

`character_short` permanece como o armazenamento legado da **chave textual genérica de associação gráfica**; `SchoolDaysVoiceAnimationKey` a expõe sem whitelist. `mak`, `x01` e a string vazia são tratados como chaves válidas. A antiga aproximação `contagem de vozes → sufixo .A/.B/.C` foi removida do case `PLAY_VOICE`, assim como seu registro de imagem `id=1`. A infraestrutura genérica de `Image` não foi modificada. O sinal PCM e o scheduler A/B/C ficam para G2.4.

O `+1000 ms` após o END de voz já existia no backend e foi preservado; este gate não afirma que corresponda à política original. A coexistência de múltiplas vozes no canal físico `Voice` também não foi alterada.

## PlaySe

NUMERIC é validado como índice de slot lógico **0..8**. `SchoolDaysSeSlots` conserva o evento ocupante e o ponteiro transitório de mídia por slot, separando essa identidade do canal físico `Se`. Na ativação, um novo `PlaySe` substitui somente o evento do mesmo slot: o pipe anterior é removido antes de habilitar o novo. Slots diferentes permanecem independentes. Um índice fora de 0..8 é ignorado com diagnóstico, sem acesso fora do array. Um recurso ausente ainda substitui logicamente o ocupante anterior, mas não cria áudio.

No reset/seek para trás, todos os pipes antigos são removidos. `ActiveEventsAt` encontra o último `PlaySe` que começou em cada slot; somente esse evento é elegível para recriação se sua janela ainda estiver aberta. Isso impede a ressurreição de um SE anterior que já havia sido substituído. Eventos encerrados antes do alvo não recomeçam. O cursor da timeline G2.2 e os marcadores `Next`/`SkipFRAME` não são alterados.

O backend anterior tratava END como limite de saída de áudio (`Kotonoha_timeGetFromEvent`), mas retinha o pipe até substituição/reset. Essa política foi preservada. O significado original detalhado de EOF, loop e liberação em END ainda é **UNKNOWN** neste escopo; não foi inventado um novo cleanup.

## Validação

`tests/SchoolDaysAudioEventTest.cpp` verifica MenVoice ligado/desligado, NUMERIC zero/não zero, chave vazia/`mak`/`x01`, slots 0/2/3/8, substituição do mesmo slot, coexistência entre slots, índice inválido, reconstrução no seek e ordem `SE A → Voice → SE B` ao atravessar vários ticks. O build desktop Release e os seis testes CTest passaram. O audit read-only de `assets/` manteve 1.857 ORS, 86.539 linhas, 86.536 eventos aceitos e zero headers desconhecidos. Não foi feito smoke test gráfico/sonoro; a validação de áudio neste gate é estrutural e determinística.
