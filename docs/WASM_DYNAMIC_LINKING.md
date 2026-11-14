# Kotonoha WASM dynamic linking — build contract

## Architecture

- **Game.js / Game.wasm** is the Emscripten `MAIN_MODULE=1`.
- **libkotonoha.wasm** is the engine `SIDE_MODULE=1`.
- **All vcpkg packages are static** (`VCPKG_LIBRARY_LINKAGE=static`): SDL3,
  SDL3_ttf, ImGui, FFmpeg, libass, zlib, BZip2, etc.
- **Important:** those static libraries are linked **only into the main
  module**, which exports their symbols for the engine side module. Linking
  SDL3 into both WASM modules can create multiple independent SDL states;
  therefore the engine side module imports external symbols instead of
  embedding static third-party libraries.
- Game-specific libraries and providers remain regular static CMake archives
  until explicitly migrated to first-party side modules.

## Build

Use CMake 3.31+, the vcpkg CMake toolchain and its Emscripten chainload:

```sh
emcmake cmake -S . -B build-wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/toolchains/emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/vcpkg-overlays/triplets" \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-overlays/ports" \
  "-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg" \
  -DKOTONOHA_SDL_FROM_VCPKG=ON \
  -DKOTONOHA_IMGUI_FROM_VCPKG=ON \
  -DKOTONOHA_WASM_DYNAMIC_LIBS=ON \
  -DKOTONOHA_BUILD_ALL=ON

python3 tools/check_wasm_link_architecture.py build-wasm
cmake --build build-wasm --parallel 4
python3 tools/check_wasm_dylink.py build-wasm/outputs
```

The overlay triplet uses static libraries and enables PIC. The build also
normalizes repeated references to the *same physical static archive* within
each game link command; it does not suppress duplicate-symbol diagnostics.

## Testing and limits

CI checks the generated main/side link ownership, archive normalizer,
WASM dylink metadata, Asyncify exports and browser smoke tests. This
feature is **experimental until the entire workflow succeeds** on an
Emscripten/vcpkg toolchain and browser run; configuring CMake or linking
the engine alone is not sufficient to claim complete compatibility.

## PT-BR

O vcpkg permanece **estático**. SDL3, ImGui, FFmpeg, libass e demais
dependências ficam no módulo principal do jogo (MAIN_MODULE), e não
duplicados dentro da engine (SIDE_MODULE). A engine importa símbolos do
módulo principal. Os jogos não devem acessar APIs específicas do
Emscripten, FFmpeg ou libass diretamente; as abstrações ficam na engine.
Os testes da CI são necessários para confirmar a execução real no navegador.
