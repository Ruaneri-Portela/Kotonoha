# TODO

Backlog ativo do projeto Kotonoha / School Days Android.

Prioridades:
- P0 — bloqueador funcional;
- P1 — problema importante;
- P2 — melhoria;
- P3 — acabamento.

---

# P0

## KTN-0001 — Corrigir retenção do último frame

Estado: `PENDENTE`

Problema:
durante troca entre Gameplays/cenas pode ocorrer tela preta.

Hipótese confirmada pelo desenvolvedor:
o último frame da cena anterior deveria permanecer enquanto a próxima cena prepara seu primeiro frame.

Áreas envolvidas:
- `src/Main.cpp`
- `src/Kotonoha.cpp`
- `src/Gameplay.cpp`
- `src/components/Canvas.cpp`
- `include/Kotonoha/components/Canvas.hpp`

Próximo passo:
implementar uma correção mínima e validar no Poco.

---

## KTN-0002 — Validar branching das escolhas

Estado: `PENDENTE`

School Days utiliza `SetSELECT`.

Kotonoha já possui Prompt e `promptId`.

Precisamos testar:
1. opções aparecem;
2. touch funciona;
3. índice correto é armazenado;
4. seleção realmente modifica a rota;
5. próxima cena correta é carregada.

---

# P1

## Investigar sincronização em transições

Estado: `PENDENTE`

Observar:
- vídeo;
- voice;
- SE;
- BGM;
- timeline.

Especialmente durante troca de cenas.

---

## Revisar lifecycle do Canvas

Estado: `PENDENTE`

Investigar:
- `dirtyTexture`;
- `holdTexture`;
- ownership das texturas;
- Reset;
- transferência entre Gameplays.

---

# P2

## Debug UI

Estado: `PENDENTE`

Definir posteriormente quais elementos permanecerão disponíveis em build de desenvolvimento:
- FPS;
- timestamp;
- Basic GUI.

---

## Assets Android

Estado: `PENDENTE`

Criar posteriormente fluxo final para instalação/cópia dos assets sem necessidade de ADB manual.

---

# Concluído

- Build Android ARM64 — `VALIDADO`
- ORS School Days — `VALIDADO`
- Vídeo WMV local — `VALIDADO`
- Voice / SE / BGM — `VALIDADO`
- Fontes corrigidas — `VALIDADO`
- styles.skot — `VALIDADO`

---

## KTN-0001-R2 — Validar apresentação imediata de DRAW_LAST

Estado: `IMPLEMENTADO — AGUARDANDO TESTE`

Teste necessário no Poco:
- reproduzir a mesma troca de cena que exibia o apagão;
- confirmar se o frame preto desapareceu ou foi reduzido;
- observar se o último frame permanece estável até a entrada da próxima cena.
---

## KTN-0001-R3 — Validar priming síncrono da próxima Gameplay

Estado: `IMPLEMENTADO — AGUARDANDO TESTE`

Teste no Poco:
- reproduzir os primeiros ~16 segundos de School Days;
- observar cada troca de cena;
- confirmar se o preto desapareceu;
- verificar se a próxima cena entra diretamente;
- confirmar que áudio, vídeo e legenda continuam sincronizados.

Se R3 resolver:
- validar a causa;
- avaliar remoção/simplificação dos experimentos R1/R2;
- transformar o handoff em implementação definitiva.

Se R3 não resolver:
- instrumentar a primeira chamada de `RenderCanvas()` e os estados DRAW/WAITING/NULL da nova Gameplay.
---

## KTN-0001-R4 — Validar handoff entre vídeos

Estado: `IMPLEMENTADO — AGUARDANDO TESTE`

Teste no Poco:
- executar os primeiros ~16 segundos;
- observar especificamente cada término/início de WMV;
- confirmar se o último quadro do vídeo anterior permanece até o primeiro quadro válido do seguinte;
- confirmar ausência de flash preto;
- verificar sincronização de áudio, legenda e timeline.

Se não houver mudança:
- capturar logcat com os marcadores `[KTN-0001-R4]`;
- registrar start/end/videoTime/currentTime das duas mídias na transição;
- verificar possível gap real na timeline ORS ou frame preto embutido no próprio WMV.
---

## KTN-0001-DIAG2 — Capturar transição real de vídeo

Estado: `DIAGNÓSTICO EM ANDAMENTO`

Procedimento:
- usar build DIAG2;
- reproduzir os primeiros ~16 segundos;
- capturar aproximadamente 25 segundos de logcat;
- analisar REGISTER / VIDEO_STATUS / VIDEO_COMPLETE / CANVAS.

Próxima ação depois da captura:
- identificar os dois WMVs envolvidos em uma transição com flash;
- comparar `end` do vídeo A com `start` do vídeo B;
- verificar estados e texturas;
- se necessário, inspecionar o último frame de A e o primeiro frame de B diretamente nos arquivos WMV.

Não criar R5 antes dessa análise.
---

## KTN-0001-R5 — Validar retenção da última textura do vídeo

Estado: `IMPLEMENTADO — AGUARDANDO TESTE`

Teste:
- executar os primeiros ~16 segundos;
- observar especialmente a passagem `00-00-A00-001.WMV` → `00-00-A00-003.WMV`;
- confirmar se o flash preto desapareceu;
- confirmar continuidade de áudio/legenda/timeline.

DIAG2 permanece ativo para confirmar os estados após R5.