# Kotonoha — Project Master

> Documento de estado atual.
>
> Este arquivo descreve como o projeto funciona AGORA.
> Histórico de alterações pertence ao `CHANGELOG.md`.

---

## Projeto

Engine base: **Kotonoha Novel Engine**

Repositório upstream: `Ruaneri-Portela/Kotonoha`

Base inicial: `5f3db07bc8c9e5aadda222e25a6424ff1846a2ac`

Branch local: `school-days-android`

Projeto Android: `School Days Android via Kotonoha`

Diretório principal: `E:\SchoolDays-Android`

---

## Objetivo

Adaptar, corrigir e expandir a Kotonoha Novel Engine para permitir a execução funcional de School Days no Android.

A engine poderá receber:
- correções;
- alterações arquiteturais;
- melhorias de compatibilidade;
- novas funcionalidades;
- melhorias específicas para Android;
- melhorias necessárias para School Days;
- correções úteis ao projeto upstream.

---

## Plataforma atual

Host:
- Windows 10/11 x64

Target:
- Android
- ARM64
- aparelho real: Poco X7 Pro

Toolchain:
- Android SDK API 34
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.14.5
- Android Studio JBR
- vcpkg
- Clang/NDK

ABI ativa: `arm64-v8a`

---

## Estado do build Android

Status: **VALIDADO**

Pipeline funcional:

Windows → Gradle → CMake → vcpkg → Android NDK/Clang ARM64 → libKotonoha.so → APK → Poco X7 Pro

O APK compila, instala e executa no dispositivo real.

---

## Correções já existentes no build

### vcpkg / Android

Configurado:
- `VCPKG_TARGET_TRIPLET=arm64-android`
- `VCPKG_HOST_TRIPLET=x64-windows`

`pkgconf` é utilizado como dependência host.

### libass / pkg-config

O uso do imported target `PkgConfig::LIBASS` provocava resolução incorreta de `-lc++` para biblioteca host do NDK.

A integração foi alterada para utilizar diretamente:
- `LIBASS_INCLUDE_DIRS`
- `LIBASS_LIBRARY_DIRS`
- `LIBASS_CFLAGS_OTHER`
- `LIBASS_LDFLAGS_OTHER`
- `LIBASS_LIBRARIES`

Status: **VALIDADO**

---

## Instalação por ADB

Em alguns testes, instalação ADB em modo streaming provocou:

`INSTALL_PARSE_FAILED_NO_CERTIFICATES`

Workaround validado:

`adb install --no-streaming -r app-debug.apk`

---

# School Days

## Scripts

Os scripts School Days são carregados a partir de:

`assets\00`

Atualmente existem scripts ORS PT-BR utilizados nos testes.

Status: **VALIDADO**

---

## Assets do jogo

Durante o desenvolvimento, os recursos School Days são armazenados no diretório privado da aplicação:

`/data/data/me.hirameki.kotonoha/files/assets/SchoolDays/`

Estrutura atual:

SchoolDays
- Event00
- Movie00
- Voice00
- Se00
- BGM

O botão School Days utiliza:

`-p assets/SchoolDays/`
`-l assets/00`

Portanto a execução atual pode ocorrer 100% local no dispositivo Android, sem servidor HTTP.

Status: **VALIDADO**

---

## Recursos funcionando

- ORS: **VALIDADO**
- Legendas PT-BR: **VALIDADO**
- Voz: **VALIDADO**
- Sound Effects: **VALIDADO**
- BGM: **VALIDADO**
- WMV: **VALIDADO**

Vídeos WMV3 são decodificados por software quando não existe hardware decoder compatível.

---

# Fontes e estilos

Foi integrado o pacote de assets fornecido pelo desenvolvedor da Kotonoha.

A estrutura própria do projeto foi preservada.

Arquivos importados incluem:
- `assets/fonts/`
- `assets/styles.skot`

Fontes disponíveis incluem:
- ConcertOne-Regular
- NotoSans-Regular
- NotoSansJP-Regular
- YoungSerif-Regular

A integração corrigiu problemas de:
- glifos;
- caracteres PT-BR;
- aparência da legenda;
- fallback inadequado de fontes.

Status: **VALIDADO**

---

## Debug visual

Durante desenvolvimento podem permanecer ativos:
- FPS;
- timestamp/timeline;
- Basic GUI/debug interface.

Esses recursos são úteis para diagnóstico.

---

# Arquitetura relevante

## Main loop

`src/Main.cpp`

O loop:
1. limpa o renderer;
2. executa a Gameplay atual;
3. desenha overlays;
4. apresenta o frame.

Problema conhecido:
o renderer pode ser limpo antes que a nova cena tenha um frame disponível.

---

## Gameplay

`src/Gameplay.cpp`

Responsável por:
- eventos;
- timeline;
- canvas;
- vídeo;
- imagem;
- áudio;
- prompt;
- reset;
- seek.

---

## Canvas

Arquivos:
- `include/Kotonoha/components/Canvas.hpp`
- `src/components/Canvas.cpp`

Responsável pela composição visual.

Possui mecanismo de último frame via `dirtyTexture`.

`KOTONOHA_SCENE_DRAW_LAST` é usado por Image e Video para indicar que o último frame deve ser preservado.

Problema atual:
durante certos resets/trocas de Gameplay, `dirtyTexture` pode ser destruída antes que a próxima cena produza seu primeiro frame.

Resultado:
**flash/apagão preto entre cenas**

Correção `KTN-0001` implementada.

A transição agora transfere a propriedade de `dirtyTexture` do Canvas da cena anterior para o Canvas da cena seguinte antes do reset da origem.

Em cenas já visitadas, o Canvas de destino é resetado antes de receber a textura retida para evitar que um `Reset()` posterior destrua o frame preservado.

Estado: **IMPLEMENTADO — aguardando validação visual no Poco X7 Pro**.

---

## Video

`src/components/Video.cpp`

Vídeo pode retornar:
- DRAW
- DRAW_LAST
- WAITING
- COMPLETE

School Days utiliza grande quantidade de pequenos WMV.

---

## Image

`src/components/Image.cpp`

Imagens também podem retornar `KOTONOHA_SCENE_DRAW_LAST`.

---

# Escolhas

School Days usa eventos `SetSELECT`.

Kotonoha já possui:
- detecção de SetSELECT;
- criação de Prompt;
- UI das opções;
- seleção por input;
- armazenamento do índice em `promptId`.

Ainda precisa ser validado se `promptId` está conectado ao branching narrativo/seleção da próxima rota.

Status: **NÃO VALIDADO**

---

# Estados utilizados no projeto

### HIPÓTESE
Ainda não comprovada.

### IMPLEMENTADO
Código alterado, mas ainda não validado em execução real.

### VALIDADO
Confirmado através de build e teste real.

---

# Documentação oficial

Este projeto utiliza:
- `MASTER.md`
- `CHANGELOG.md`
- `DECISIONS.md`
- `TODO.md`

Nenhuma alteração importante deve existir apenas em conversa.
Toda mudança deve ser registrada.

### KTN-0001 — retenção do último frame

**R1 — resultado de teste:** SEM EFEITO VISUAL.

A primeira tentativa transferia `dirtyTexture` entre objetos `Gameplay`, mas o apagão continuou inalterado no Poco X7 Pro. Isso mostrou que a causa principal não estava apenas no ownership entre Gameplays.

**R2 — IMPLEMENTADO, aguardando validação.**

Foi identificado um problema dentro de `Canvas::RenderCanvas`: ao receber `KOTONOHA_SCENE_DRAW_LAST`, o Canvas transferia `item.target` para `dirtyTexture`, zerava `item.target` e não desenhava a nova `dirtyTexture` na janela naquele mesmo frame. Como `Main.cpp` limpa o renderer antes de cada iteração, esse intervalo podia expor um frame preto.

R2 apresenta `dirtyTexture` imediatamente no mesmo frame de `DRAW_LAST`.
---

## KTN-0001-R3 — Primeira renderização da próxima Gameplay

O diagnóstico foi refinado após retorno do desenvolvedor da engine e revisão do fluxo de eventos.

`EventManager` roda em uma thread de background em ciclos de aproximadamente 50 ms. A chamada síncrona feita pelo construtor ocorre antes do relógio da Gameplay iniciar e, portanto, retorna sem registrar mídia. Na primeira entrada de uma nova Gameplay, `Gameplay::Main()` inicia o relógio e podia chamar `RenderCanvas()` antes do próximo ciclo do worker.

Isso cria uma janela em que a nova cena já é a cena ativa, mas ainda não possui Image/Video/Audio registrados para a primeira exibição.

R3 adiciona `Event::ProcessNow()` e faz `Gameplay::Main()` processar os eventos síncronamente, após iniciar o relógio e antes da primeira renderização.

Estado: **IMPLEMENTADO — aguardando validação visual no Poco X7 Pro**.
---

## KTN-0001-R4 — Handoff entre vídeos consecutivos

O problema foi refinado após testes R1, R2 e R3 sem mudança visual.

Observação de teste: o flash preto ocorre especificamente na passagem de um vídeo para o vídeo seguinte, inclusive nos primeiros segundos do jogo.

A arquitetura de vídeo já faz preload: `EventManager` registra eventos próximos antecipadamente e `Kotonoha_VideoRenderInit` decodifica um primeiro `AVFrame` durante o registro. Portanto o problema não é tratado apenas como atraso de abertura do arquivo.

Foi identificado um caso em `Kotonoha_VideoRenderProcess`: quando `currentTime < videoTime`, o renderer retornava `KOTONOHA_SCENE_DRAW` mesmo quando `instance->texture` ainda era `NULL`. Isso podia declarar um primeiro frame como desenhável antes de existir uma textura SDL pronta.

Também foi alterado o Canvas para que `KOTONOHA_SCENE_WAITING` não apresente um target recém-criado sobre `dirtyTexture`.

Comportamento esperado: manter o último quadro do vídeo anterior até o próximo vídeo possuir um frame realmente desenhável.

Estado: **IMPLEMENTADO — aguardando validação visual no Poco X7 Pro**.
---

## KTN-0001-DIAG2 — Diagnóstico objetivo da troca de vídeos

Após os testes:

- R1: sem mudança visual;
- R2: sem mudança visual;
- R3: sem mudança visual;
- R4: sem mudança visual.

O problema permanece reproduzível e foi refinado como um flash preto entre vídeos consecutivos.

A partir deste ponto, novas correções ficam suspensas até captura de evidência temporal.

DIAG2 instrumenta:

- registro de cada vídeo com path/start/end;
- mudanças de estado do VideoRender;
- disponibilidade de `SDL_Texture` e `AVFrame`;
- timeline atual;
- mudanças de estado das camadas do Canvas;
- presença de `dirtyTexture` durante o handoff.

Estado: **DIAGNÓSTICO EM ANDAMENTO**.
---

## KTN-0001-R5 — Manter último frame válido quando o decoder esgota antes do evento

DIAG2 isolou uma transição real do School Days.

No vídeo `00-00-A00-001.WMV`:

- evento ORS: `start=4050`, `end=11050`;
- a camada estava em `DRAW`;
- em `timeline=10856`, antes do fim do evento, mudou para `WAITING` e depois `NULL`;
- a `SDL_Texture` anterior ainda era válida;
- `dirtyTexture` do Canvas era nula;
- o vídeo seguinte começava em `11000`, mas seu primeiro frame tinha PTS inicial de aproximadamente `41 ms`.

Resultado: existia um intervalo em que nenhuma camada de vídeo era desenhada e o renderer principal continuava limpando a janela para preto.

R5 altera a semântica do VideoRender:

- se não há um novo `AVFrame`, mas já existe uma `SDL_Texture` válida, retorna `DRAW`;
- a última textura permanece visível;
- `WAITING` é usado somente quando ainda não existe nenhuma textura válida para apresentar.

Estado: **IMPLEMENTADO — aguardando validação visual no Poco X7 Pro**.