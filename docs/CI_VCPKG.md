# vcpkg CI / CI com vcpkg

## EN-US

Three GitHub Actions workflows produce artifacts:

- **WASM:** `wasm32-emscripten`; all browser game ports, browser smoke tests, uploaded output directory.
- **Linux:** `x64-linux-dynamic`; all enabled game ports, native tests, CPack ZIP packages and output artifacts.
- **Windows:** `x64-windows`; all enabled game ports, native tests, CPack ZIP packages and output artifacts.

Enable the vcpkg features **sdl-vcpkg** and **imgui-vcpkg**, together with the CMake options **KOTONOHA_SDL_FROM_VCPKG=ON** and **KOTONOHA_IMGUI_FROM_VCPKG=ON**, to import SDL3, SDL3_ttf and ImGui (SDL3/SDL3 Renderer backends) from vcpkg. The three CI workflows do not initialize any Git submodule. By default, local builds still use the SDL submodules.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_SDL_FROM_VCPKG=ON \
  -DKOTONOHA_IMGUI_FROM_VCPKG=ON \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build
```

Use `x64-windows` for Windows. For WebAssembly use `wasm32-emscripten` and Emscripten's CMake chainload toolchain. Selecting the CMake option without the vcpkg toolchain is a configuration error; there is no silent fallback.

Commercial game files are excluded from CI artifacts.

## PT-BR

Os três workflows geram artefatos para WASM (`wasm32-emscripten`), Linux x64 com bibliotecas dinâmicas (`x64-linux-dynamic`) e Windows x64 (`x64-windows`). Linux e Windows executam testes nativos e geram ZIPs pelo CPack; o WASM executa testes de integração no navegador.

Para utilizar SDL3/SDL3_ttf e ImGui do vcpkg, habilite as features `sdl-vcpkg;imgui-vcpkg` e as opções `KOTONOHA_SDL_FROM_VCPKG=ON` e `KOTONOHA_IMGUI_FROM_VCPKG=ON`. Os workflows não baixam nenhum submódulo. O padrão dos builds locais permanece com submódulos. Os dados comerciais dos jogos não são distribuídos nos artefatos.
