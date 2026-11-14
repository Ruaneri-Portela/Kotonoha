# Dynamic Kotonoha WASM + static vcpkg / Kotonoha dinâmico + vcpkg estático

## Linking contract

- **Game.js / Game.wasm**: Emscripten MAIN_MODULE=1, the game executable.
- **libkotonoha.wasm**: first-party KotonohaEngine SIDE_MODULE=1.
- **vcpkg dependencies (ImGui, SDL3, FFmpeg, libass, etc.)**: static archives
  compiled with position-independent code (`-fPIC`) for linking into
  first-party modules or the main executable. **No libkotonoha_imgui.wasm.**
- The game's output directory includes Game.js, Game.wasm and
  libkotonoha.wasm. The side module is deployed automatically after linking.

The PIC vcpkg triplet `wasm32-emscripten` sets
`VCPKG_LIBRARY_LINKAGE=static` and `VCPKG_CXX_FLAGS=-fPIC`.
It does **not** build the vcpkg ports as shared libraries. Use
`-DVCPKG_TARGET_TRIPLET=wasm32-emscripten`,
`-DVCPKG_OVERLAY_TRIPLETS=<repo>/vcpkg-overlays/triplets`,
`"-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg"`,
`-DKOTONOHA_SDL_FROM_VCPKG=ON`, `-DKOTONOHA_IMGUI_FROM_VCPKG=ON`,
`-DKOTONOHA_WASM_DYNAMIC_LIBS=ON`.

CMake 3.31+ is required so the linker may de-duplicate transitive static archives
(SDL3, PlutoVG, etc.) using CMP0156, CMP0179 and REORDER_FREELY.

The feature is experimental; successful Emscripten compile/link and
Chromium/WebKit execution have **not** been demonstrated. Asyncify through
side-module imports, SDL linkage, media and Fetch require integration tests.

## Game-port isolation

Port C++ and CMake must not directly reference Emscripten, FFmpeg or libass
APIs. `Kotonoha::BackgroundWorker`, `kotonoha_preload_asset` and
`tools/audit_game_dependencies.py` support this rule.

## PT-BR

O vcpkg continua **100% estático**; o triplet apenas adiciona `-fPIC`
para permitir incorporar os arquivos `.a` nos módulos. Somente as
bibliotecas próprias do Kotonoha podem usar SIDE_MODULE. Inicialmente,
`KotonohaEngine` é o módulo dinâmico gerado. O ImGui do vcpkg é vinculado
estaticamente à engine, nunca distribuído como módulo WASM separado.
