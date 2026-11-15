# Tarefas futuras de arquitetura — Kotonoha v3

[English (US)](MAINTENANCE_BACKLOG.md) | **Português (Brasil)**

A correção dos caminhos de arte do Windows L4 **não** autoriza consolidar
árvores CMake, migrar callbacks/RAII do SDL nem alterar caminhos de saves.
Cada item abaixo exige plano próprio, revisável, com marco de build/testes.
Nunca alterar os 29 GPKs originais ou `LxH.GPK`, executar o EXE retail ou
usar saves reais.

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

## Etapa futura 12 — callbacks SDL compartilhados (plano independente)

Inventariar as quatro funções `SDL_App*` de
`games/school-days/src/main.cpp` e seu ciclo de vida; adaptar para
`KOTONOHA_SDL_CALLBACK_APP`, preservando launcher, defines DEV/DIST e
passagem para a Activity Android. Compilar Windows L4 e testar regressões
do launcher/runtime antes de abordar outras plataformas.

## Etapa futura 13 — RAII do SDL (plano independente)

Inventariar posse de objetos SDL por classe, começando pelas texturas e
fontes manuais em `SchoolDaysBackLogPresenter.cpp`. Trocar um proprietário
por vez pelos handles existentes em `Kotonoha/core/SDL.hpp`, sem destruição
duplicada; verificar fechamento/reabertura e build Windows L4 em cada etapa.

## Etapa futura 14 — StoragePaths (plano independente)

Documentar antes o contrato de Windows L4 de `settings.json` ao lado de
`SchoolDays.exe`, `Config.DAT`, e QA isolado com `--documents`/`--config`.
Introduzir compatibilidade com `StoragePaths` só com migração explícita e
testes que impeçam mover ou sobrescrever perfis dos jogadores inesperadamente.

## Pendências de plataforma e documentação

Em `Android/app/build.gradle`, apenas dois caminhos de origem da arte do
launcher foram corrigidos estaticamente; Android não foi compilado nem
executado. Linux, macOS, iOS e Web também não foram testados. A consolidação
de `Android/`, `Xcode/` e `buildsystems/` não está autorizada agora. Alguns
guias antigos ainda citam workspace Xcode e caminhos Gradle obsoletos;
atualizá-los em pares bilíngues apenas na revisão da plataforma apropriada,
sem reescrever notas históricas datadas ou
`games/school-days/PORT_STATUS.md`.
