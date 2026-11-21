# Dependências CMake do Kotonoha

## SDL3 e SDL3_ttf

A opção `KOTONOHA_SDL_PROVIDER` admite `AUTO` (padrão),
`SYSTEM`, `VCPKG` ou `SUBMODULES`.
Em `AUTO`, busca pacotes do sistema, em seguida vcpkg (se configurado),
e por último os submódulos. Android sempre exige fontes SDL locais
para a integração Java.

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
`buildsystems/cmake/KotonohaSDL.cmake` escolhe o SDL;
`buildsystems/cmake/KotonohaImGui.cmake` compila ImGui;
`buildsystems/cmake/KotonohaGitSubmodules.cmake` inicializa
submódulos ausentes; e `KotonohaApp.cmake` cuida dos executáveis.
