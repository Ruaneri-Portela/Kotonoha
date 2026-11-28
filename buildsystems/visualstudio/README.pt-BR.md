# Solução Visual Studio

[English (US)](README.md) | **Português (Brasil)**

`Kotonoha.sln` é uma solução nativa do Visual Studio versionada, análoga a
`buildsystems/xcode/Kotonoha.xcodeproj`. Ela compila as fontes **diretamente**:
sem invocar CMake e sem gerar arquivos de projeto durante a compilação.

## Estrutura

| Área | Projetos | Conteúdo |
|---|---|---|
| `lib` | `KotonohaEngine` (DLL) | `lib/src/**/*.cpp`; SDL3, SDL3_ttf e Dear ImGui vêm do vcpkg |
| `modules\vms` | 12 `VM_*` | uma biblioteca estática por diretório `modules/vms/*` |
| `modules\formats` | 14 `Format_*` | uma biblioteca estática por diretório `modules/formats/*/*` |
| `modules\providers` | 13 `Provider_*` | uma biblioteca estática por diretório `modules/providers/*` |
| `games` | 16 `Game_*Lib` + 16 `Game_*` | uma biblioteca estática por port (`games/*/src`, exceto `main.cpp`) e um executável compilando apenas `main.cpp` |
| `launcher` | `KotonohaLauncherCore`, `KotonohaGameHost`, `KotonohaLauncher` | DLL do núcleo + bridges JNI/C ABI linkadas às bibliotecas estáticas dos jogos, executável host destacado e executável SDL do launcher unificado |

Total: 75 projetos (16 jogos, 39 módulos, 1 DLL do motor e 3 projetos do launcher). `KotonohaEngine` e
`KotonohaLauncherCore` são DLLs;
`GenerateVisualStudioExports.ps1` reproduz o `WINDOWS_EXPORT_ALL_SYMBOLS` do
CMake para elas. Cada biblioteca de jogo tem seu próprio `IntDir`, portanto
fontes com o mesmo nome em ports diferentes (`Source.cpp`, `Archive.cpp`,
`Game.cpp`, `Script.cpp`) nunca geram objetos com nomes colidentes. `Directory.Build.props` centraliza diretórios de include,
definições de pré-processador, seleção C++17/C++20, caminhos de saída e o
link com as bibliotecas do vcpkg. `Directory.Build.targets` copia as DLLs de
runtime do vcpkg para junto de cada executável.
`generated/Kotonoha/core/BuildVersion.hpp` espelha a versão do Git usada pelo
CMake e pelo Xcode.

## Requisitos

- Visual Studio 18 (2026, toolset v145, Windows SDK 10.0) com a carga de trabalho
  **Desenvolvimento para desktop com C++**, incluindo as ferramentas MSVC para
  x86, x64 e ARM64. Usuários do Visual Studio 2022 podem redirecionar a solução
  para v143.
- vcpkg: `VCPKG_ROOT` deve apontar para um checkout com `vcpkg.exe`. O manifesto
  `vcpkg.json`, os ports de overlay `vcpkg-overlays/ports` e as features
  `sdl-vcpkg` e `imgui-vcpkg` são usados nas três plataformas.

## Compilação

Na raiz do repositório (apenas PowerShell; macOS/Linux usam Python/sh):

```powershell
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode All
```

Os modos também podem ser executados separadamente:

```powershell
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Generate
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Version
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Install -Architectures x64
powershell -ExecutionPolicy Bypass -File tools\PrepareVisualStudio.ps1 -Mode Build -Architectures x86,x64,arm64 -Configuration Release
```

`Install` coloca as dependências em
`out/build/windows-<arch>-msvc-vcpkg/vcpkg_installed/<triplet>/` (ignorado pelo
Git), compartilhando exatamente a mesma raiz do vcpkg por arquitetura com os
presets MSVC do CMake (`windows-x86-msvc-vcpkg`, `windows-x64-msvc-vcpkg`,
`windows-arm64-msvc-vcpkg`), da mesma forma que o Xcode compartilha
`out/build/macos-<arch>-vcpkg/vcpkg_installed/`. `Build` chama `MSBuild.exe` com `Debug|Release` × `Win32|x64|ARM64` e
grava os artefatos em `out/<Platform>/<Configuration>/`.

Também é possível abrir `Kotonoha.sln` diretamente no Visual Studio e escolher
qualquer uma das seis plataformas da solução. Para compilar toda a matriz
MSBuild junto com os presets CMake do MSVC, use
`powershell -ExecutionPolicy Bypass -File tools\BuildWindows.ps1` (veja
[BUILD_WINDOWS.md](../../docs/BUILD_WINDOWS.pt-BR.md#driver-de-build-windows)).

## Validação

```sh
python3 tools/ValidateBuildMetadata.py
python3 tools/check_visual_studio_sources.py
```

O segundo script verifica, sem MSBuild, que toda fonte em `lib/src`,
`modules/*/src`, `games/*/src` e `launcher/core` é alcançada por algum projeto,
que cada GUID aparece na solução e que as seis combinações
configuração/plataforma existem.

`tools/PrepareVisualStudio.ps1 -Mode Generate` regenera a solução e os projetos
versionados após adições ou remoções de fontes; commite os arquivos gerados
juntamente com a alteração.
