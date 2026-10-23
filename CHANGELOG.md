# Changelog

Histórico cronológico de alterações do projeto Kotonoha / School Days Android.

Formato dos IDs: `KTN-XXXX`

---

## KTN-0000 — Documentation baseline

**Tipo:** Documentation / Baseline

**Estado:** VALIDADO

### Objetivo

Criar um ponto inicial rastreável antes das futuras modificações estruturais da Kotonoha Engine.

### Estado registrado

Foram documentados:
- build Android funcional;
- toolchain ARM64;
- integração vcpkg;
- correção libass/pkg-config;
- instalação no Poco X7 Pro;
- execução dos ORS;
- execução local dos recursos School Days;
- vídeo;
- áudio;
- legendas;
- fontes corrigidas;
- styles.skot;
- estado do sistema de escolhas;
- problema conhecido de retenção do último frame.

### Arquivos adicionados

- `MASTER.md`
- `CHANGELOG.md`
- `DECISIONS.md`
- `TODO.md`

### Resultado

O projeto passa a utilizar documentação persistente e controle de mudanças semelhante a commits locais.

---

## KTN-0001 — Preserve last frame between Gameplay scenes

**Tipo:** Fix / Canvas / Scene lifecycle

**Estado:** IMPLEMENTADO — aguardando validação no Poco

### Problema

Durante a troca entre Gameplays/cenas, o renderer podia apresentar um frame preto antes que a próxima cena tivesse conteúdo visual disponível.

### Causa

`Canvas` já retinha o último frame através de `dirtyTexture` quando um componente retornava `KOTONOHA_SCENE_DRAW_LAST`, mas a textura pertencia ao Canvas da Gameplay anterior e podia ser destruída durante `Reset()`.

### Alteração

Foi criado `Canvas::TransferLastFrameTo(Canvas*)`.

A propriedade de `dirtyTexture` e `dirtyPlace` passa do Canvas de origem para o Canvas de destino antes do reset da origem.

A lógica foi aplicada às transições para frente e para trás.

### Arquivos alterados

- `include/Kotonoha/components/Canvas.hpp`
- `src/components/Canvas.cpp`
- `src/Kotonoha.cpp`

### Validação

- Build Android ARM64: concluído.
- Teste visual no Poco X7 Pro: pendente.
---

## KTN-0001-R2 — Present retained frame immediately

**Tipo:** Fix / Canvas rendering

**Estado:** IMPLEMENTADO — aguardando validação no Poco

### Resultado da R1

A correção anterior de transferência de `dirtyTexture` entre Gameplays compilou e executou, porém não produziu mudança visual: o apagão preto permaneceu.

### Nova causa identificada

Em `Canvas::RenderCanvas`, `KOTONOHA_SCENE_DRAW_LAST` fazia:

1. mover `item.target` para `dirtyTexture`;
2. definir `item.target = nullptr`;
3. pular a apresentação daquele target porque o bloco seguinte só desenhava quando `item.target != nullptr`.

Como `Main.cpp` já havia limpado o framebuffer para preto no início da iteração, o último frame só aparecia na iteração seguinte.

### Alteração

Após transferir o target para `dirtyTexture`, R2 restaura o render target da janela e desenha `dirtyTexture` imediatamente antes de continuar.

### Arquivo alterado

- `src/components/Canvas.cpp`

### Validação

- Build Android ARM64: concluído.
- Validação visual no Poco: pendente.
---

## KTN-0001-R3 — Prime next Gameplay before first render

**Tipo:** Fix / Gameplay / Event scheduling

**Estado:** IMPLEMENTADO — aguardando validação no Poco

### Contexto

O desenvolvedor informou que o comportamento ideal é, ao terminar uma cena, preparar e renderizar imediatamente a primeira exibição da próxima cena, sem apresentar um quadro intermediário vazio.

### Diagnóstico

A thread `EventsThread` processa os `EventManager` em intervalos de aproximadamente 50 ms.

O construtor de `Event` tenta chamar `EventManager()` imediatamente, mas nesse momento o relógio da Gameplay ainda não foi iniciado. O próprio `EventManager()` retorna cedo quando `tm->started` é falso.

Na primeira chamada de `Gameplay::Main()`:
1. o relógio é iniciado;
2. `RenderCanvas()` podia ser chamado imediatamente;
3. o worker ainda podia levar até ~50 ms para registrar Image/Video/Audio.

Isso explica uma janela curta de tela preta na troca de Gameplay.

### Alteração

- `Event` agora mantém referência não-proprietária aos parâmetros de sua tarefa.
- foi criado `Event::ProcessNow()`;
- `ProcessNow()` serializa com `taskLock` para não concorrer com o worker;
- `Gameplay::Main()` chama `ProcessNow()` em `firstFocus`, antes do primeiro `RenderCanvas()`.

### Arquivos

- `include/Kotonoha/components/Events.hpp`
- `src/components/Events.cpp`
- `src/Gameplay.cpp`

### Experimentos anteriores

- KTN-0001-R1: sem efeito visual.
- KTN-0001-R2: sem efeito visual.
- KTN-0001-R3: aguardando teste.

### Validação

- Build Android ARM64: concluído.
- Teste visual no Poco: pendente.
---

## KTN-0001-R4 — Hold previous frame until next video is drawable

**Tipo:** Fix / Video / Canvas

**Estado:** IMPLEMENTADO — aguardando validação no Poco

### Resultado dos testes anteriores

- R1: sem efeito visual.
- R2: sem efeito visual.
- R3: sem efeito visual.

O relato de teste refinou o problema: o flash preto ocorre na troca entre vídeos consecutivos.

### Diagnóstico

`EventManager` pode registrar `PLAY_MOVIE` antecipadamente, e `Kotonoha_VideoRenderInit()` já decodifica um primeiro `AVFrame`.

Porém `Kotonoha_VideoRenderProcess()` retornava `DRAW` quando `currentTime < videoTime` sem exigir que `instance->texture` já existisse.

No primeiro frame de um novo vídeo, isso permite `DRAW` com textura SDL ainda nula.

Ao mesmo tempo, depois de `DRAW_LAST`, o Canvas pode criar um novo target para o próximo vídeo. Apresentar esse target antes de existir conteúdo válido pode expor preto.

### Alteração

1. Se o primeiro frame decodificado ainda está antes do seu PTS e não existe `SDL_Texture`, VideoRender retorna `WAITING`.
2. Canvas não apresenta `item.target` em `WAITING`.
3. `dirtyTexture` permanece visível até existir um frame realmente desenhável.

### Arquivos alterados

- `src/renders/VideoRender.c`
- `src/components/Canvas.cpp`

### Validação

- Build Android ARM64: concluído.
- Teste visual no Poco: pendente.
---

## KTN-0001-DIAG2 — Instrument video-to-video handoff

**Tipo:** Diagnostic / Video / Canvas

**Estado:** IMPLEMENTADO — aguardando captura

### Resultado de R4

R4 não produziu mudança visual. O flash preto permanece entre vídeos consecutivos.

### Decisão de investigação

Nenhum novo workaround será aplicado antes de medir a transição real.

### Instrumentação

O build diagnóstico registra:

- `[KTN-DIAG2][REGISTER]`
  - vídeo;
  - path;
  - start/end;
  - primeiro videoTime;
  - texture/frame.

- `[KTN-DIAG2][VIDEO_STATUS]`
  - mudança entre NULL/WAITING/DRAW/DRAW_LAST/COMPLETE;
  - timeline;
  - videoTime;
  - texture;
  - AVFrame.

- `[KTN-DIAG2][VIDEO_COMPLETE]`
  - instante exato de término.

- `[KTN-DIAG2][CANVAS]`
  - zIndex;
  - status retornado;
  - target;
  - dirtyTexture.

### Objetivo

Distinguir entre:

1. gap real na timeline entre dois PLAY_MOVIE;
2. primeiro frame seguinte ainda não apresentável;
3. target/dirtyTexture incorreto no Canvas;
4. frame preto existente no próprio WMV;
5. outra condição ainda não observada.
---

## KTN-0001-R5 — Hold last valid video texture through decoder gap

**Tipo:** Fix / Video timing / Frame retention

**Estado:** IMPLEMENTADO — aguardando validação no Poco

### Evidência DIAG2

A captura demonstrou uma transição em que:

- o vídeo atual ainda estava dentro do intervalo ORS;
- o decoder ficou sem um frame novo;
- a textura SDL anterior continuava válida;
- o estado mudou para `WAITING` e depois `NULL`;
- `dirtyTexture` estava nula;
- o próximo vídeo ainda aguardava seu primeiro PTS.

Nesse intervalo o loop principal podia apresentar apenas o clear preto.

### Alteração

`Kotonoha_VideoRenderProcess()` agora diferencia:

- **sem frame novo + textura anterior válida** → `DRAW`;
- **sem frame novo + nenhuma textura válida** → `WAITING`.

A mesma regra é usada quando existe `AVFrame` alocado, mas com `pict_type == AV_PICTURE_TYPE_NONE`.

Também foram removidos logs R4 por frame; DIAG2 permanece para validação.

### Arquivos

- `src/renders/VideoRender.c`
- `src/components/Canvas.cpp`

### Experimentos anteriores

- R1: sem efeito visual.
- R2: sem efeito visual.
- R3: sem efeito visual.
- R4: sem efeito visual.
- DIAG2: causa temporal localizada.
- R5: aguardando teste visual.