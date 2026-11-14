# vcpkg CI / CI com vcpkg

## EN-US

Three GitHub Actions workflows produce artifacts:

- **WASM:** `wasm32-emscripten`; all browser game ports, browser smoke tests, uploaded output directory.
- **Linux:** `x64-linux-dynamic`; all enabled game ports, native tests, CPack ZIP packages and output artifacts.
- **Windows:** `x64-windows`; all enabled game ports, native tests, CPack ZIP packages and output artifacts.

Enable the vcpkg feature **sdl-vcpkg** together with the CMake option **KOTONOHA_SDL_FROM_VCPKG=ON** to import SDL3 and SDL3_ttf from vcpkg. By default, local builds still use the SDL submodules.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
  -DVCPKG_MANIFEST_FEATURES=sdl-vcpkg \
  -DKOTONOHA_SDL_FROM_VCPKG=ON \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build
```

Use `x64-windows` for Windows. For WebAssembly use `wasm32-emscripten` and Emscripten's CMake chainload toolchain. Selecting the CMake option without the vcpkg toolchain is a configuration error; there is no silent fallback.

Commercial game files are excluded from CI artifacts.

## PT-BR

Os três workflows geram artefatos para WASM (`wasm32-emscripten`), Linux x64 com bibliotecas dinâmicas (`x64-linux-dynamic`) e Windows x64 (`x64-windows`). Linux e Windows executam testes nativos e geram ZIPs pelo CPack; o WASM executa testes de integração no navegador.

Para utilizar SDL3/SDL3_ttf do vcpkg em vez dos submódulos, habilite **as duas opções**: `VCPKG_MANIFEST_FEATURES=sdl-vcpkg` e `KOTONOHA_SDL_FROM_VCPKG=ON`. O padrão dos builds locais permanece com submódulos. Os dados comerciais dos jogos não são distribuídos nos artefatos.
