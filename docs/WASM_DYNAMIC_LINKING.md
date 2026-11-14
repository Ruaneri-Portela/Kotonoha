# Dynamic WASM linking (experimental) / WASM dinâmico (experimental)

Enable `KOTONOHA_WASM_DYNAMIC_LIBS=ON` to link every game as an
Emscripten MAIN_MODULE with two load-time SIDE_MODULE libraries:
`libkotonoha.wasm` and `libkotonoha_imgui.wasm`.
The standard static mode remains the default.

Build with the custom `wasm32-emscripten-dylink` PIC triplet in
`vcpkg-overlays/triplets`, enable manifest features `sdl-vcpkg;imgui-vcpkg`
and set `KOTONOHA_SDL_FROM_VCPKG=ON`,
`KOTONOHA_IMGUI_FROM_VCPKG=ON`, and
`KOTONOHA_WASM_DYNAMIC_LIBS=ON`. CMake 3.24+ is required.

The generated `Game.js`, `Game.wasm`, `libkotonoha.wasm` and
`libkotonoha_imgui.wasm` must be served from each game's output directory.
Browser compatibility (Asyncify, Fetch, media, SDL) is unverified until
the CI workflow succeeds. Repeating module files at different URLs does not
by itself result in shared HTTP cache entries.

## Game-port isolation / Isolamento dos jogos

Game source and CMake files must not directly refer to Emscripten,
FFmpeg or libass. The engine provides `Kotonoha::BackgroundWorker` and
`kotonoha_preload_asset` as platform-neutral interfaces. CI runs
`tools/audit_game_dependencies.py` to guard these boundaries.

PT-BR: O modo experimental separa o executável do jogo das bibliotecas
Kotonoha e ImGui. A opção não interfere nos builds estáticos existentes.
As bibliotecas nativas e as decisões específicas do Emscripten ficam na engine.
