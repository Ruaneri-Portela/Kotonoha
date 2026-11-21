# Dependências CMake do Kotonoha

## SDL3 e SDL3_ttf

A opção `KOTONOHA_SDL_PROVIDER` admite `AUTO` (padrão),
`SYSTEM`, `VCPKG` ou `SUBMODULES`.
A ordem de `AUTO` depende do compilador:

- **Linux e macOS:** pacotes do sistema → vcpkg configurado → submódulos.
- **Windows com MinGW:** sistema compatível com MinGW → vcpkg do triplet MinGW → submódulos.
- **Windows com MSVC:** **vcpkg nativo → submódulos**. `SYSTEM` é proibido.
  O CMake não aceita headers do MSYS2/UCRT64 nem bibliotecas de triplets
  `*-mingw-*` quando o compilador é `cl.exe`.
- **Android:** submódulos SDL para integração Java.

No MSVC, as bibliotecas sem fonte própria no projeto (FFmpeg, libass e json-c)
precisam estar instaladas com o **vcpkg e um triplet MSVC**, como
`x64-windows`. O CMake não usa o `pkg-config` do MSYS2 nesse ambiente e
interrompe a configuração se as dependências não forem compatíveis.

## ImGui: sempre o submódulo

O ImGui é compilado com os arquivos `imgui.cpp`,
`imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`,
`imgui_impl_sdl3.cpp`, `imgui_impl_sdlrenderer3.cpp` e
`imgui_stdlib.cpp` do **mesmo commit** em
`lib/submodules/imgui`. Não usa `libimgui-dev` do Debian ou a
versão instalada via vcpkg.

Ao configurar, o CMake executa automaticamente
`git submodule update --init -- lib/submodules/imgui` quando
o checkout está ausente. O Git usa a revisão fixada no repositório.
Com o submódulo presente, nenhuma consulta de rede é feita.

`-DKOTONOHA_AUTO_FETCH_SUBMODULES=OFF` desativa o fetch para
ambientes offline. Nesse caso, inicialize manualmente o submódulo
antes de configurar.

As antigas opções `KOTONOHA_IMGUI_PROVIDER` e
`KOTONOHA_IMGUI_FROM_VCPKG` não mudam a origem do ImGui.

## Debian 13 / Linux ARM64

```bash
sudo apt update
sudo apt install git cmake ninja-build g++ pkg-config \
  libsdl3-dev libsdl3-ttf-dev libass-dev libavformat-dev \
  libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  libjson-c-dev libgl-dev

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DKOTONOHA_BUILD_ALL=ON \
  -DKOTONOHA_SDL_PROVIDER=SYSTEM \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```

## Windows com MSVC (Visual Studio)

Use o terminal do Visual Studio com as ferramentas MSVC configuradas.
No PowerShell, configure um diretório de build **novo** para não reutilizar
os caminhos do MinGW do cache anterior:

```powershell
$toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"

cmake -S . -B out/build/msvc-x64-debug-clean -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  "-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg" `
  -DKOTONOHA_BUILD_ALL=ON

cmake --build out/build/msvc-x64-debug-clean --parallel 4
```

Sem a feature `sdl-vcpkg`, o SDL3 pode ser compilado a partir dos
submódulos. O ImGui permanece sempre no submódulo, independentemente
do compilador. Não reutilize `out/build/mingw-x64-debug` para MSVC,
nem misture os arquivos `.lib` do MSVC com os `.dll.a` do MinGW.

## vcpkg para SDL3

```bash
cmake -S . -B build-vcpkg -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=sdl-vcpkg \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_ALL=ON
cmake --build build-vcpkg --parallel 4
```

## Submódulos SDL

Use `-DKOTONOHA_SDL_PROVIDER=SUBMODULES`. O CMake também
pode inicializar os submódulos SDL ausentes, caso sejam os
caminhos originais do repositório.

## Organização

`lib/CMakeLists.txt` define os targets da engine;
`buildsystems/cmake/dependencies/KotonohaMedia.cmake` resolve os pacotes de mídia;
`buildsystems/cmake/dependencies/KotonohaSDL.cmake` escolhe o SDL;
`buildsystems/cmake/dependencies/KotonohaImGui.cmake` compila ImGui;
`buildsystems/cmake/dependencies/KotonohaGitSubmodules.cmake` inicializa
submódulos ausentes; e `buildsystems/cmake/games/KotonohaApp.cmake` cuida dos executáveis.
