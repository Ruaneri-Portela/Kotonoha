# Kotonoha Novel Engine α

**Kotonoha Novel Engine** is a free and open-source C++ visual novel engine for `.ORS` scripts.
It is a modern reimplementation inspired by the FORIS engine used in the *Days* series by 0verflow, including *School Days*.

> ⚠️ **Status: Alpha.** Features are still being implemented and defects may occur.

**Português brasileiro:** role até a seção [Português (Brasil)](#português-brasil).

---

## English

### Overview

Kotonoha parses and executes `.ORS` visual novel scripts while providing modern multimedia playback, image rendering, subtitles, and interactive choices. It aims to be both a compatible way to run `.ORS`-based novels and a flexible foundation for creating new interactive stories without requiring programming experience.

### Features

- `.ORS` script parser and execution system
- Audio playback
- Video playback through FFmpeg
- Image display and media assets
- Subtitle rendering through libass
- A/B choice prompts that affect the story
- Missing-asset handling and retry support
- Loading overlay while media is unavailable or stalled
- Debug/development interface using ImGui

### Technology

| Component | Purpose |
|---|---|
| C++ | Engine implementation |
| SDL3 | Windowing, graphics, input, and audio |
| SDL3_ttf | Text rendering |
| FFmpeg | Audio and video decoding |
| libass | Subtitle rendering |
| ImGui | Debug and development interface |
| CMake | Cross-platform build system |
| VCPKG | Dependency management |
| pkg-config | Alternative discovery of system libraries |

### Platform Support

Kotonoha targets:

- **Windows**
- **Linux**
- **macOS**
- **Android**
- **iOS**

The canonical build is **CMake**. On Windows it integrates with VCPKG; on Linux and macOS it can also discover system libraries through **pkg-config**. Visual Studio consumes the same CMake targets through thin wrapper projects, Xcode should be generated from CMake, and Android Studio continues to use the Gradle/CMake project.

### Repository Layout

```text
.
├── CMakeLists.txt          # Cross-platform CMake build
├── vcpkg.json              # VCPKG dependency manifest
├── Kotonoha.sln            # Visual Studio CMake wrapper solution
├── Kotonoha.vcxproj        # Wrapper for the libKotonoha target
├── SchoolDays.vcxproj      # Wrapper for the game executable target
├── android-project/        # Android Studio / Gradle project
├── src/                    # Engine components, parsers, renders and debug
├── include/Kotonoha/       # Engine and runtime subsystem headers
├── games/school-days/      # School Days HQ application/runtime/profile
└── external/               # Git submodules
```

The application is packaged as `libKotonoha` plus the thin `SchoolDays` SDL
entry executable. `KotonohaEngine` remains the reusable low-level core;
`libKotonoha` adds scene/runtime systems and profile adapters. Generic
components, parsers, renderers and debug sources live in `src/` and
`include/Kotonoha/`; School Days routing, persistence and asset policy remain
under `games/school-days/`. See `docs/architecture/ENGINE_GAME_SPLIT.md` for
the source ownership rules.

### Dependencies

The following dependencies are integrated through Git submodules:

- SDL3
- SDL3_ttf

Additional dependencies may be provided by VCPKG, system packages, Homebrew, or prebuilt binaries:

- FFmpeg
- libass
- pkgconf / pkg-config, when using the pkg-config discovery path

### Getting Started

Clone the repository with submodules:

```bash
git clone --recursive [https://github.com/Ruaneri-Portela/Kotonoha.git](https://github.com/Ruaneri-Portela/Kotonoha.git)
cd Kotonoha
```

If you already cloned the repository without submodules:

```bash
git submodule update --init --recursive
```

### Build with CMake

#### Windows — CMake + VCPKG

Install:

- Git
- CMake
- Visual Studio 2022 with C++ desktop development workload
- VCPKG

Configure and build:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --target Kotonoha SchoolDays
```

The resulting artifacts are typically located under:

```text
build/Release/libKotonoha.dll
build/Release/SchoolDays.exe
```

#### Linux — System Libraries via pkg-config

Debian/Ubuntu example:

```bash
sudo apt update
sudo apt install \
  build-essential cmake git pkg-config \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libass-dev
```

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

On Arch Linux, the equivalent package names are generally:

```bash
sudo pacman -S base-devel cmake git pkgconf ffmpeg libass
```

#### macOS — Homebrew / System Libraries

```bash
brew install cmake ffmpeg libass pkg-config
```

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

### IDE Integration

#### Visual Studio — Windows

Open:

```text
Kotonoha.sln
```

The solution exposes **Kotonoha** and **SchoolDays** as separate projects.
They wrap the canonical CMake targets, so there is no second source list to
keep synchronized. `KotonohaEngine` is built as a dependency of `Kotonoha`.
Set `VCPKG_ROOT` (or `KOTONOHA_VCPKG_ROOT`) before building on Windows.

Ensure FFmpeg and libass are available through VCPKG, prebuilt binaries, or configured include/library paths.

#### Xcode — macOS

Generate an Xcode project from the canonical CMake build:

```bash
cmake -S . -B build/xcode -G Xcode
cmake --build build/xcode --target Kotonoha SchoolDays
```

Install dependencies with Homebrew:

```bash
brew install ffmpeg libass
```

The tracked `Xcode/` project is a legacy snapshot and is not the authoritative
source list after the engine/game split.

#### Android Studio — Android

The Android project is located at:

```text
android-project/
```

Recommended development environment:

- Android Studio
- JDK 21
- Android SDK 34
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.14.5
- VCPKG

Set the required environment variables before configuring the project:

```bash
export ANDROID_SDK_ROOT=/path/to/android-sdk
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=/path/to/android-sdk/ndk/27.0.12077973
export VCPKG_ROOT=/path/to/vcpkg
```

Build a debug APK:

```bash
cd android-project
./gradlew :app:assembleDebug
```

If the Gradle wrapper JAR is unavailable, use a local Gradle 8.14.5 installation or regenerate the wrapper.

The APK is generated at:

```text
android-project/app/build/outputs/apk/debug/
```

> **Windows host note:** if the Android Gradle configuration contains a macOS-specific `darwin-x86_64` `CMAKE_SYSROOT` argument, remove it on Windows and allow the Android NDK toolchain to select the host sysroot. Manifest-mode VCPKG builds may also require `pkgconf` as a host dependency. If `PkgConfig::LIBASS` resolves host C++ runtime flags during cross-compilation, use the include/library variables returned by `pkg-config` instead of the imported target.

### Asset Loading

Use `-i` or `--ignore-missing-assets` to skip missing media assets at startup.

Assets that are still being fetched are retried automatically. Playback pauses and shows a loading overlay only while an asset is waiting to open/retry or an FFmpeg read remains stalled for at least 500 ms. Playback resumes automatically once reading continues.

On exit, in-flight FFmpeg reads are cancelled before audio resources are released.

For HTTP image assets, ABC overlays are requested using deterministic names:

```text
<image-stem><character_short>.<a|b|c>.png
```

Example:

```text
face.png + hero => facehero.a.png
face.png + hero => facehero.b.png
face.png + hero => facehero.c.png
```

### Creating a Novel

Create `.ORS` scripts describing scenes, dialogue, media calls, subtitles, and A/B choices. The engine maps these commands to its multimedia and interaction systems.

Example workflow:

1. Place scripts and media assets in the expected project directory.
2. Launch the Kotonoha executable with the path to your novel/script data.
3. Use A/B choices to branch the story.
4. Use `-i` during development if some media files have not yet been added.

### MCP Control Server

Launch with `--mcp [port]` (default `8765`) or `KOTONOHA_MCP_PORT=<port>` to expose a [Model Context Protocol](https://modelcontextprotocol.io) server at `http://127.0.0.1:<port>/mcp` (streamable HTTP, JSON-RPC over POST, loopback only, non-local `Origin` rejected). Tools run on the main thread right before the frame is presented:

`get_state`, `screenshot`, `get_buffers` (audio/video played/cached/EOF in seconds), `get_logs`, `key`, `mouse`, `playback`, `scene`, `load_script`, `quit`.

Example client config: `{"mcpServers": {"kotonoha": {"url": "http://127.0.0.1:8765/mcp"}}}`. The implementation is `src/debug/McpServer.cpp`; games that present their own frame call `Debug::Mcp::Service()` before `SDL_RenderPresent`.

### Contributing

Contributions are welcome. Before opening a pull request:

1. Fork the repository.
2. Create a feature branch.
3. Keep changes focused and documented.
4. Test on at least one desktop platform; Android changes should be tested through the Android project when possible.

### License

This project is licensed under the MIT License. See [LICENSE](LICENSE.md).

---

## Português (Brasil)

### Visão geral

O **Kotonoha Novel Engine** é um motor de visual novels gratuito e de código aberto, escrito em C++, para executar scripts `.ORS`. Ele é uma reimplementação moderna inspirada no motor FORIS, usado na série *Days* da 0verflow, incluindo *School Days*.

O projeto interpreta e executa scripts `.ORS`, oferecendo reprodução multimídia moderna, exibição de imagens, legendas e escolhas interativas. Ele pode ser usado tanto para rodar novels baseadas em `.ORS` quanto como base para criar novas histórias interativas sem exigir experiência prévia em programação.

### Funcionalidades

- Parser e sistema de execução de scripts `.ORS`
- Reprodução de áudio
- Reprodução de vídeo via FFmpeg
- Exibição de imagens e assets de mídia
- Renderização de legendas via libass
- Prompts de escolha A/B que afetam a história
- Tratamento e nova tentativa para assets ausentes
- Tela de carregamento enquanto a mídia está indisponível ou travada
- Interface de depuração e desenvolvimento com ImGui

### Tecnologias

| Componente | Finalidade |
|---|---|
| C++ | Implementação do motor |
| SDL3 | Janelas, gráficos, entrada e áudio |
| SDL3_ttf | Renderização de texto |
| FFmpeg | Decodificação de áudio e vídeo |
| libass | Renderização de legendas |
| ImGui | Interface de depuração e desenvolvimento |
| CMake | Sistema de build multiplataforma |
| VCPKG | Gerenciamento de dependências |
| pkg-config | Descoberta alternativa de bibliotecas do sistema |

### Plataformas Suportadas

O Kotonoha suporta:

- **Windows**
- **Linux**
- **macOS**
- **Android**

O build canônico é **CMake**. No Windows ele integra com VCPKG; no Linux e macOS também pode descobrir bibliotecas do sistema via **pkg-config**. O Visual Studio consome os mesmos targets do CMake através de projetos-wrapper finos, o Xcode deve ser gerado pelo CMake e o Android Studio continua usando Gradle/CMake.

### Estrutura do Repositório

```text
.
├── CMakeLists.txt          # Build multiplataforma com CMake
├── vcpkg.json              # Manifesto de dependências do VCPKG
├── Kotonoha.sln            # Solution-wrapper do CMake para Visual Studio
├── Kotonoha.vcxproj        # Wrapper do target libKotonoha
├── SchoolDays.vcxproj      # Wrapper do target executável do jogo
├── android-project/        # Projeto Android Studio / Gradle
├── src/                    # Implementação reutilizável do motor
├── include/Kotonoha/       # Headers reutilizáveis do motor
├── games/school-days/      # Aplicação/runtime/perfil de School Days HQ
└── external/               # Submódulos Git
```

O aplicativo é empacotado como `libKotonoha` mais o executável SDL fino
`SchoolDays`. `KotonohaEngine` continua sendo o núcleo reutilizável de baixo
nível; `libKotonoha` adiciona cenas e sistemas de runtime. Componentes, parsers,
renders e debug genéricos ficam em `src/` e `include/Kotonoha/`; roteamento,
persistência e política de assets de School Days continuam em
`games/school-days/`. Veja `docs/architecture/ENGINE_GAME_SPLIT.md`.

### Dependências

As seguintes dependências são integradas via submódulos Git:

- SDL3
- SDL3_ttf

Outras dependências podem ser fornecidas pelo VCPKG, pacotes do sistema, Homebrew ou binários pré-compilados:

- FFmpeg
- libass
- pkgconf / pkg-config, ao usar o caminho de descoberta via pkg-config

### Como Começar

Clone o repositório com os submódulos:

```bash
git clone --recursive [https://github.com/Ruaneri-Portela/Kotonoha.git](https://github.com/Ruaneri-Portela/Kotonoha.git)
cd Kotonoha
```

Se você já clonou o repositório sem os submódulos:

```bash
git submodule update --init --recursive
```

### Compilação com CMake

#### Windows — CMake + VCPKG

Instale:

- Git
- CMake
- Visual Studio 2022 com a carga de trabalho “Desenvolvimento para desktop com C++”
- VCPKG

Configure e compile:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --target Kotonoha SchoolDays
```

Os artefatos normalmente serão gerados em:

```text
build/Release/libKotonoha.dll
build/Release/SchoolDays.exe
```

#### Linux — Bibliotecas do Sistema via pkg-config

Exemplo para Debian/Ubuntu:

```bash
sudo apt update
sudo apt install \
  build-essential cmake git pkg-config \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libass-dev
```

Compile:

```bash
cmake -S . -B build
cmake --build build -j
```

No Arch Linux, os pacotes equivalentes geralmente são:

```bash
sudo pacman -S base-devel cmake git pkgconf ffmpeg libass
```

#### macOS — Homebrew / Bibliotecas do Sistema

```bash
brew install cmake ffmpeg libass pkg-config
```

Compile:

```bash
cmake -S . -B build
cmake --build build -j
```

### Integração com IDEs

#### Visual Studio — Windows

Abra:

```text
Kotonoha.sln
```

Essa solution expõe **Kotonoha** e **SchoolDays** como projetos separados.
Eles são wrappers dos targets canônicos do CMake, sem uma segunda lista de
fontes para manter. `KotonohaEngine` é compilado como dependência de `Kotonoha`.
Defina `VCPKG_ROOT` (ou `KOTONOHA_VCPKG_ROOT`) antes de compilar no Windows.

Garanta que FFmpeg e libass estejam disponíveis via VCPKG, binários pré-compilados ou caminhos configurados de include e biblioteca.

#### Xcode — macOS

Gere o projeto do Xcode a partir do CMake canônico:

```bash
cmake -S . -B build/xcode -G Xcode
cmake --build build/xcode --target Kotonoha SchoolDays
```

Instale as dependências com Homebrew:

```bash
brew install ffmpeg libass
```

O projeto rastreado em `Xcode/` é um snapshot legado e não é a lista de fontes
autoritativa depois da separação motor/jogo.

#### Android Studio — Android

O projeto Android está localizado em:

```text
android-project/
```

Ambiente recomendado:

- Android Studio
- JDK 21
- Android SDK 34
- Android NDK 27.0.12077973
- CMake 3.22.1
- Gradle 8.14.5
- VCPKG

Configure as variáveis de ambiente antes de configurar o projeto:

```bash
export ANDROID_SDK_ROOT=/caminho/do/android-sdk
export ANDROID_HOME=/caminho/do/android-sdk
export ANDROID_NDK_HOME=/caminho/do/android-sdk/ndk/27.0.12077973
export VCPKG_ROOT=/caminho/do/vcpkg
```

Gere um APK de debug:

```bash
cd android-project
./gradlew :app:assembleDebug
```

Se o JAR do Gradle Wrapper não estiver disponível, use uma instalação local do Gradle 8.14.5 ou gere novamente o wrapper.

O APK é gerado em:

```text
android-project/app/build/outputs/apk/debug/
```

> **Nota para host Windows:** se a configuração Android do Gradle contiver o argumento `CMAKE_SYSROOT` específico do macOS, `darwin-x86_64`, remova-o no Windows e permita que o toolchain do Android NDK selecione o sysroot do host. Builds do VCPKG em modo manifesto também podem exigir `pkgconf` como dependência de host. Se `PkgConfig::LIBASS` resolver flags da runtime C++ do host durante a cross-compilation, use as variáveis de include/biblioteca retornadas pelo `pkg-config` em vez do target importado.

### Carregamento de Assets

Use `-i` para ignorar assets de mídia ausentes na inicialização.

Assets que ainda estão sendo baixados são tentados novamente automaticamente. A reprodução pausa e exibe a tela de carregamento somente enquanto um asset aguarda abertura/nova tentativa ou uma leitura do FFmpeg permanece parada por pelo menos 500 ms. A reprodução volta automaticamente quando a leitura continuar.

Ao sair, leituras FFmpeg em andamento são canceladas antes da liberação dos recursos de áudio.

Para imagens servidas por HTTP, os overlays ABC são solicitados com nomes determinísticos:

```text
<nome-base><character_short>.<a|b|c>.png
```

Exemplo:

```text
face.png + hero => facehero.a.png
face.png + hero => facehero.b.png
face.png + hero => facehero.c.png
```

### Criando uma Novel

Crie scripts `.ORS` descrevendo cenas, diálogos, chamadas de mídia, legendas e escolhas A/B. O motor mapeia esses comandos para seus sistemas multimídia e de interação.

Fluxo básico:

1. Coloque os scripts e assets de mídia no diretório esperado do projeto.
2. Execute o binário do Kotonoha apontando para os dados da novel/script.
3. Use as escolhas A/B para criar ramificações na história.
4. Durante o desenvolvimento, use `-i` caso alguns arquivos de mídia ainda não tenham sido adicionados.

### Contribuindo

Contribuições são bem-vindas. Antes de abrir um pull request:

1. Faça um fork do repositório.
2. Crie uma branch para a funcionalidade.
3. Mantenha as alterações focadas e documentadas.
4. Teste em pelo menos uma plataforma desktop; alterações Android devem ser testadas pelo projeto Android quando possível.

### Licença

Este projeto é licenciado sob a MIT License. Consulte [LICENSE](LICENSE.md).
