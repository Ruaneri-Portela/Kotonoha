# Tarefas futuras de arquitetura — Kotonoha v3

[English (US)](MAINTENANCE_BACKLOG.md) | **Português (Brasil)**

A correção anterior dos caminhos de arte do Windows L4 **não** autorizava
consolidar árvores CMake, migrar callbacks/RAII do SDL nem alterar saves.
A sincronização local com upstream, autorizada separadamente, incorporou
parte dos recursos genéricos (estado abaixo). As mudanças restantes ainda
exigem revisão e build/testes próprios. Nunca alterar os 29 GPKs originais
ou `LxH.GPK`, executar o EXE retail ou usar saves reais.

## Auditoria da sincronização upstream/dev — 2026-11-15

A branch local `sync/upstream-dev-20261115-021254` incorpora a arquitetura
upstream. Integração do núcleo não equivale à validação de todos os jogos,
variantes e plataformas. O inventário anterior é preservado abaixo.

| Etapa | Estado no código sincronizado | Evidência e verificação pendente |
| --- | --- | --- |
| 9 — CMake duplicado | **PARCIAL** — auxiliares de preload/host/plataformas incorporados (`cb05a02`, `43657a0`, `4c83f57`) | Matriz de jogos upstream na raiz; consumidores legados em `cmake/` ainda existem. Não apagar arquivos nem declarar equivalência sem prova. |
| 10 — CMake divergente | **PARCIAL** — outputs, empacotamento e seleção upstream integrados (`cb05a02`, `43657a0`) | Manifesto duas DLLs, DEV/DIST e arte Windows preservados. Paridade de plataformas e consolidação de módulos continuam sem validação. |
| 11 — FileResolver / AssetProvider | **NÚCLEO INTEGRADO; JOGO PENDENTE** — `4214a85`, `c27e32b`, `bc1d473`, `7e7d65f`, `2d3a371`, `0efdc35` | Filesystem V2, Fetch e overlays estão no núcleo. Ainda não houve teste com os 29 GPKs reais, os sete finais LxH e prioridade de idiomas estrita; não ativar Fetch remoto implicitamente. |
| 12 — SDLCallbackApp | **INTEGRADO** — `151766b`, `e2b0e64`, `1185f77`, `428016b`, `50098c5` | School Days implementa `init/event/iterate/quit` e um único `KOTONOHA_SDL_CALLBACK_APP`. Launcher único e exceção de settings L4 permanecem; investigar o timeout de `SchoolDaysLauncherTest`. |
| 13 — RAII do SDL | **NÚCLEO INTEGRADO; POSSE NO JOGO PENDENTE** — `8642f19` | Handles de `SDL.hpp` disponíveis; não presumir que texturas/fontes manuais dos apresentadores de School Days já foram migradas. Auditar cada dono e fechamento/reabertura. |
| 14 — StoragePaths | **NÚCLEO INTEGRADO; EXCEÇÃO L4 RETIDA** — `4214a85`, `c27e32b` | Storage compartilhado isolado por aplicativo; `games/school-days/src/main.cpp` usa `resourceDirectory()/settings.json` no Windows L4. Preservar `Config.DAT` separado e testar apenas cópias descartáveis. |

Decisões e resultados de build ficam nos relatórios ignorados
`build/_sync-decisoes.md` e `build/_sync-levantamento.md`.

## Etapa futura 9 — consolidar módulos CMake idênticos

Os arquivos abaixo existem em `cmake/` e `buildsystems/cmake/` com SHA-256
idêntico no checkout auditado. Nenhum arquivo deve ser apagado. Os
consumidores são referências textuais, incluindo ramos de plataforma inativos.

| Módulo | Consumidores textuais atuais |
| --- | --- |
| `CheckBundleContents.cmake` | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake`, `lib/tests/CMakeLists.txt` |
| `CheckBundledAssets.cmake` | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake` |
| `CopyAssetDirectory.cmake` | `lib/CMakeLists.txt`, `buildsystems/cmake/KotonohaApp.cmake` |
| `Info.plist.in` | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake` |
| `MSVC-UTF8.cmd` | Nenhuma referência textual direta encontrada no CMake versionado; conferir comandos gerados antes de mover |
| `MSVC.cmake` | `CMakeLists.txt`, `lib/CMakeLists.txt` |
| `PackageAssets.cmake` | `lib/CMakeLists.txt`, `lib/tests/CMakeLists.txt`, `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake` |

**Plano:** inventariar comandos gerados; redirecionar um consumidor por vez
para `buildsystems/cmake/`; validar Windows L4 e CTest após cada mudança;
preservar arquivos antigos no histórico (mover com `git mv` somente após
revisão). Outras plataformas ficam sem teste até uma sessão autorizada.

## Etapa futura 10 — reconciliar módulos CMake divergentes

| Módulo | Consumidores textuais atuais / dependências |
| --- | --- |
| `BundleApp.cmake` (diferente) | `cmake/KotonohaApp.cmake`, `buildsystems/cmake/KotonohaApp.cmake`, `lib/tests/CMakeLists.txt`, `games/doki-doki-literature-club/tests/CMakeLists.txt` |
| `KotonohaApp.cmake` (diferente) | `lib/CMakeLists.txt` inclui explicitamente a versão em `cmake/`; `buildsystems/cmake/KotonohaApp.cmake` é alternativa |
| `CopyRuntimeDependencies.cmake` (só em `cmake/`) | `cmake/KotonohaApp.cmake` |
| `PackageWindowsRuntime.cmake` (só em `cmake/`) | `cmake/KotonohaApp.cmake` |
| `CopyRuntimeLibraries.cmake` (só em `buildsystems/cmake/`) | `buildsystems/cmake/KotonohaApp.cmake` |
| `KotonohaOutputs.cmake`, `index.html.in` (só em `buildsystems/cmake/`) | `buildsystems/cmake/KotonohaApp.cmake` e configuração de saída por plataforma; reauditar includes indiretos |

**Plano:** comparar recursos de Windows duas DLLs/staging e hooks das outras
plataformas; projetar camada compatível opt-in; atualizar um consumidor por
commit; manter a implementação anterior até passar nos testes de paridade.
Sem substituição automática.

## Etapa 11 — FileResolver / AssetProvider: núcleo pronto, paridade pendente

As APIs compartilhadas substituem o shim antigo de storage e introduzem
VFS/Fetch com limites de cache. School Days ainda define a montagem própria
dos GPKs retail e do LxH. Testar cache, seek e decoder com streams sintéticos;
testes licenciados reais dependem de QA isolado expressamente autorizado.
Compilação não prova isolamento de idiomas nem ausência de fallback.

## Etapa 12 — callbacks SDL compartilhados: integração concluída

`games/school-days/src/main.cpp` expõe os quatro métodos estáticos por
`KOTONOHA_SDL_CALLBACK_APP`, sem outro `main()` ou GUI. Manter launcher e
ciclo de vida atuais. Não é preciso repetir a migração da bridge; investigar
o timeout restante de `SchoolDaysLauncherTest` em ambiente headless.

## Etapa 13 — RAII do SDL: núcleo pronto, migração do jogo pendente

Inventariar posse de objetos SDL por classe, começando pelas texturas e
fontes manuais em `SchoolDaysBackLogPresenter.cpp`. Trocar um proprietário
por vez pelos handles existentes em `Kotonoha/core/SDL.hpp`, sem destruição
duplicada; verificar fechamento/reabertura e build Windows L4 em cada etapa.

## Etapa 14 — StoragePaths: núcleo pronto, exceção Windows L4 preservada

O `StoragePaths` genérico fornece diretórios por aplicativo, enquanto
School Days Windows L4 mantém deliberadamente `settings.json` ao lado de
`SchoolDays.exe` e o contrato separado de `Config.DAT`. A migração do
núcleo já existe; **não** mover perfis reais L4. Criar smoke com
`--documents` isolado somente se dispensar assets comerciais e GUI.

## Pendências de plataforma e documentação

Em `Android/app/build.gradle`, apenas dois caminhos de origem da arte do
launcher foram corrigidos estaticamente; Android não foi compilado nem
executado. Linux, macOS, iOS e Web também não foram testados. A consolidação
de `Android/`, `Xcode/` e `buildsystems/` não está autorizada agora. Alguns
guias antigos ainda citam workspace Xcode e caminhos Gradle obsoletos;
atualizá-los em pares bilíngues apenas na revisão da plataforma apropriada,
sem reescrever notas históricas datadas ou
`games/school-days/PORT_STATUS.md`.
