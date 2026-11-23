# Kotonoha Visual Studio (Win32 / x64 / ARM64)

**Português (Brasil)** | [English (US)](README.md)

`Kotonoha.sln` é uma solução nativa do Visual Studio **versionada**, estruturada
como o projeto Xcode versionado: um projeto por target de aplicação, com os
fontes de jogo/provider/VM compilados diretamente no executável. O CMake continua
sendo a fonte da verdade para as listas de fontes e dependências vcpkg.

## Estrutura da solução

| Target | Tipo | Observação |
|---|---|---|
| `KotonohaEngine`, `KotonohaImGui` | bibliotecas estáticas | código compartilhado |
| `KotonohaExample` + 15 ports de jogos | aplicações | uma por slug de jogo |
| `KotonohaLauncher` | aplicação | launcher unificado multi-engine |
| `SDL3`, `SDL3_ttf` | projetos nativos importados | de `lib/submodules/` |

O projeto nativo do SDL3 suporta Win32, x64 e ARM64. O `.vcxproj` do SDL3_ttf
upstream define apenas as configurações Win32/x64; portanto, em **ARM64 a solução
usa as bibliotecas SDL3/SDL3_ttf do vcpkg**, e o projeto nativo do SDL3_ttf é
ignorado nessa configuração.

## Arquiteturas

Win32 (x86), x64 (amd64) e ARM64 são suportados, cada um com as configurações
`Debug` e `Release`. A matriz authoritative — lista de projetos, contagem de
fontes, provedor SDL por arquitetura e comandos de build — está em
[Alvos, plataformas e arquiteturas do Visual Studio](../../docs/VISUAL_STUDIO_TARGETS.pt-BR.md).

## Pré-requisitos

- Visual Studio 2022 ou superior com o workload "Desktop development with C++"
  e o Windows SDK. O toolset é o padrão instalado (`$(DefaultPlatformToolset)`).
- Submódulos Git inicializados (`lib/submodules/SDL`, `SDL_ttf`, `imgui`).
- Dependências do manifesto vcpkg instaladas para o triplet escolhido, por
  exemplo:
  ```sh
  cmake --preset windows-x64
  ```
  Os presets do CMake criam uma árvore `vcpkg_installed` completa em
  `out/build/<preset>/vcpkg_installed/`.

## Build (IDE)

Abra `buildsystems/visualstudio/Kotonoha.sln`, selecione a plataforma/configuração
na barra de ferramentas e compile. `SDL3.dll` / `SDL3_ttf.dll` e as DLLs de runtime
do vcpkg são copiadas automaticamente para junto de cada executável.

## Build (linha de comando)

```sh
msbuild buildsystems/visualstudio/Kotonoha.sln /t:Build /p:Configuration=Debug /p:Platform=x64 ^
  /p:KotonohaVcpkgInstalled=<repo>/out/build/windows-x64/vcpkg_installed/
```

Substitua `x64` por `Win32` ou `ARM64` (e o diretório do triplet por
`x86-windows` / `arm64-windows`) para as outras arquiteturas.

`KotonohaVcpkgInstalled` é uma propriedade MSBuild sobrescrevível. O valor padrão
é `<repo>/vcpkg_installed/`, que pode estar incompleto; aponte-o para a árvore
gerada pelo preset CMake correspondente.

## Regenerar os arquivos versionados

```sh
# 1. Atualiza as listas de fontes a partir de um build CMake com gerador "Visual Studio"
cmake -S . -B out/build/vsprobe-x64 -G "Visual Studio 18 2026" -A x64
python3 buildsystems/visualstudio/Scripts/generate_visualstudio.py --export-from-cmake out/build/vsprobe-x64

# 2. Reescreve Kotonoha.sln, Directory.Build.props e todos os .vcxproj
python3 buildsystems/visualstudio/Scripts/generate_visualstudio.py

# Verificação somente-leitura
python3 buildsystems/visualstudio/Scripts/generate_visualstudio.py --check
```

`Scripts/GenerateBuildVersion.py` atualiza `generated/Kotonoha/core/BuildVersion.hpp`
a partir do Git a cada build, preservando o timestamp quando os metadados não mudam.

## Validação e limitações

`python3 tools/ValidateBuildMetadata.py` valida os metadados CMake/Xcode e o
manifesto do Visual Studio em conjunto. Dados comerciais de jogos nunca são
versionados; os targets compilam, mas precisam de assets locais para executar.

Um launcher/biblioteca nativo WinUI 3 está planejado como projeto importado
x64/ARM64 — veja o [plano de implementação](../../docs/WINUI_NATIVE_APP_PLAN.pt-BR.md).

Para outras plataformas veja [CMake](../cmake/README.md), [Xcode](../xcode/README.md),
[Android](../android/README.md) e os guias de build Windows em `docs/`.
