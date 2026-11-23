# Portable desktop outputs, SDL entrypoints and ZIP packaging

**English (US)** | [Português (Brasil)](BUILD_PACKAGES.pt-BR.md)

## Shared SDL3 lifecycle

Game ports use `Kotonoha/core/SDLCallbackApp.hpp`. Each port defines a
class with static `init`, `event`, `iterate` and `quit` methods; the
`KOTONOHA_SDL_CALLBACK_APP(MyPort)` macro exports the four SDL3 callback
symbols. Do not write a second `SDL_AppInit` or `main` function. Port-
specific launcher transitions, source selection and quit semantics stay in
the port class. See `games/example/src/main.cpp` for the smallest example.

Own SDL heap resources with `Kotonoha/core/SDL.hpp` RAII aliases:
`WindowPtr`, `RendererPtr`, `TexturePtr`, `SurfacePtr`, `IOStreamPtr`,
`AudioStreamPtr`, `FontPtr`, `SDLMemoryPtr<T>`, `SDLArrayPtr<T>`,
`AudioDeviceHandle`, `GamepadPtr` and `JoystickPtr`. Hand borrowed
pointers to SDL through `.get()`, and transfer ownership by `std::move`.
**Do not manually SDL_Destroy*/SDL_free** an object already owned by an
RAII handle. The migration of legacy raw owner fields is incremental; do
not convert borrowed, external or shared references to owning handles.

## Build outputs and runtime libraries

After building each configured port:

| Host | Game output | Shared dependencies |
|---|---|---|
| Windows | `build/outputs/<Game>/` | DLLs **next to** the executable |
| Linux | `build/outputs/<Game>/` | `libs/*.so*`, `$ORIGIN/libs` |
| macOS non-bundle | `build/outputs/<Game>/` | `libs/*.dylib`, `@loader_path/libs` |
| macOS bundle | `build/outputs/<Game>.app` | `Contents/Frameworks` |

The CMake POST_BUILD packaging step discovers linked runtime libraries,
includes the shared libraries built by Kotonoha's CMake (even without
vcpkg), and resolves vcpkg dependencies. On macOS it additionally searches
Homebrew and relocates/resigns dylibs as needed; `.app` files retain their
existing `fixup_bundle` packaging. On Linux install `patchelf` to repair
indirect dependency RPATHs; if unavailable the build warns and retains
original shared-library RPATHs. System DLLs/frameworks are not bundled.

## ZIP packages (CPack)

From a fully configured and compiled desktop build:

```sh
cmake -S . -B build -DKOTONOHA_BUILD_EXAMPLE=ON -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --parallel 4
cmake --build build --target package
```

CPack writes one ZIP for each enabled game under `build/packages/`,
with a name such as `Kotonoha-0.1.0-feature-...-Linux-x86_64-Pmail.zip`.
Each ZIP uses the same output directory as the game and contains the
required executable, resources and deployed shared libraries. For a
single game component:

```sh
cpack --config build/CPackConfig.cmake -G ZIP \
  -D CPACK_COMPONENTS_ALL=Kotonoha
```

Retail assets are **excluded by default on both macOS `.app` and non-bundle ZIPs** to avoid distributing
copyrighted game data; only the free `Kotonoha` example can include its
sample assets. To explicitly package *your own* local data, configure
`-DKOTONOHA_CPACK_INCLUDE_GAME_ASSETS=ON`. Never redistribute commercial
game files without the necessary rights. Each ZIP includes baseline license notices; audit the licenses of actually shipped dynamic libraries separately. Android APK/AAB is still built
through Gradle.

## WebAssembly memory

Web builds now use a 128 MiB initial heap with growth to 1 GiB by
default, plus a bounded 48 MiB HTTP byte-range LRU cache and 1 MiB remote
pages. Adjust with:

```sh
cmake -S . -B build-web \
  -DKOTONOHA_WASM_INITIAL_MEMORY=134217728 \
  -DKOTONOHA_WASM_MAX_MEMORY=1073741824 \
  -DKOTONOHA_WASM_FETCH_CACHE_MIB=48
```

A streaming GPK/BIN still requires the HTTP server to honor `Range`
with `206 Partial Content`; increasing WASM memory cannot replace this.
Smaller starting memory does **not** guarantee every game's peak usage.
Test on real iOS/Android browsers with each game's legitimate data.
