# Alvos, plataformas e arquiteturas do Visual Studio

**Português (Brasil)** | [English (US)](VISUAL_STUDIO_TARGETS.md)

Esta é a matriz authoritative da solução Visual Studio versionada em
`buildsystems/visualstudio/Kotonoha.sln`. Os mesmos dados são declarados em
`buildsystems/visualstudio/ProjectManifest.json` e verificados por
`python3 tools/ValidateBuildMetadata.py`.

## Arquiteturas suportadas

| Arquitetura solicitada | Plataforma MSBuild | Triplet vcpkg | Provedor SDL3 / SDL3_ttf |
|---|---|---|---|
| x86 (32 bits) | `Win32` | `x86-windows` | projetos `.vcxproj` nativos |
| amd64 (64 bits) | `x64` | `x64-windows` | projetos `.vcxproj` nativos |
| arm64 | `ARM64` | `arm64-windows` | bibliotecas do vcpkg |

O projeto nativo do SDL3 (`lib/submodules/SDL/VisualC/SDL/SDL.vcxproj`) define
as três plataformas. O projeto nativo do SDL3_ttf
(`lib/submodules/SDL_ttf/VisualC/SDL_ttf.vcxproj`) define apenas `Win32` e `x64`;
por isso a solução mapeia as configurações `ARM64` para a configuração de
projeto `x64` **sem compilá-la**, e o ARM64 linka `SDL3.lib` / `SDL3_ttf.lib`
do vcpkg.

Todas as plataformas suportam as configurações `Debug` e `Release` (6 pares
configuração/plataforma no total).

## Matriz de projetos

`Kind` é o `ConfigurationType` do MSBuild. A contagem de fontes das aplicações
inclui o port do jogo, seus VMs, providers e codecs de formato compilados
diretamente no executável (a mesma estratégia do projeto Xcode versionado).

| Projeto | Tipo | Fontes | Slug do jogo | Observação |
|---|---|---:|---|---|
| `KotonohaEngine` | StaticLibrary | 85 | — | engine compartilhado; `Kotonoha/core/BuildVersion.hpp` gerado do Git |
| `KotonohaImGui` | StaticLibrary | 7 | — | Dear ImGui + backends SDL3 |
| `KotonohaExample` | Application | 96 | `example` | amostra redistribuível |
| `Amagami` | Application | 108 | `amagami` | |
| `ZeroNoTsukaima` | Application | 111 | `zero-no-tsukaima` | |
| `SnowRadish` | Application | 105 | `snow-radish-vacation` | |
| `SummerRadish` | Application | 104 | `summer-radish-vacation` | |
| `MEO` | Application | 104 | `meo` | |
| `DokiDoki` | Application | 104 | `doki-doki-literature-club` | incorpora CPython do vcpkg |
| `KatawaShoujo` | Application | 104 | `katawa-shoujo` | incorpora CPython do vcpkg |
| `SchoolDays` | Application | 151 | `school-days` | |
| `RealLive` | Application | 107 | `reallive` | inclui codecs vendor `libreallive/` |
| `Kirikiri` | Application | 104 | `kirikiri` | VMs KAG + TJS2 |
| `LucaSystem` | Application | 98 | `luca-system` | |
| `Pmail` | Application | 120 | `pmail` | |
| `PonscripterGame` | Application | 104 | `ponscripter` | |
| `N2SystemGame` | Application | 120 | `n2system` | |
| `ArtemisGame` | Application | 99 | `artemis` | |
| `KotonohaLauncher` | Application | 303 | — | todos os runtimes em um processo |

Projetos nativos importados (referenciados, não copiados nem modificados):

| Projeto | Caminho | Plataformas |
|---|---|---|
| `SDL3` | `lib/submodules/SDL/VisualC/SDL/SDL.vcxproj` | Win32, x64, ARM64 |
| `SDL3_ttf` | `lib/submodules/SDL_ttf/VisualC/SDL_ttf.vcxproj` | Win32, x64 |

## Comandos de build

```sh
# x86
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Debug /p:Platform=Win32 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-x86/vcpkg_installed/

# amd64
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Release /p:Platform=x64 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-x64/vcpkg_installed/

# arm64
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Release /p:Platform=ARM64 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-arm64/vcpkg_installed/
```

`KotonohaVcpkgInstalled` deve apontar para uma árvore vcpkg completa do triplet
selecionado. Os presets CMake `windows-x86`, `windows-x64` e `windows-arm64`
geram essas árvores em `out/build/<preset>/vcpkg_installed/`.

## Relação com os outros buildsystems

- O CMake é a fonte da verdade para listas de fontes e descoberta de
  dependências. Atualize com `generate_visualstudio.py --export-from-cmake <build-dir>`.
- Os alvos Xcode são a fonte da verdade para nomes de alvos/slugs de jogos; o
  manifesto do Visual Studio é comparado com eles pelo `ValidateBuildMetadata.py`.
- Nenhum dado comercial de jogos é versionado. As aplicações compilam sem
  assets de varejo, mas precisam de assets locais para executar.

Um launcher nativo WinUI está **planejado** como projeto importado x64/ARM64;
veja o [plano de implementação](WINUI_NATIVE_APP_PLAN.pt-BR.md).

Veja o [guia do Visual Studio](../buildsystems/visualstudio/README.pt-BR.md), os
[guias de build](BUILD_GUIDES.pt-BR.md) e o [cross-build Windows](BUILD_WINDOWS.pt-BR.md).
