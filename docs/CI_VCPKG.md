# GitHub Actions and vcpkg CI

The build workflows in `.github/workflows/` are manually triggered using **Actions → Run workflow** on the `dev` branch. They publish compiled outputs without bundling commercial game data.

| Workflow | Runner and targets | Outputs and checks |
| --- | --- | --- |
| [`linux.yml`](../.github/workflows/linux.yml) | **One** `ubuntu-24.04` job; Linux `x64-linux-dynamic` **then** WASM `wasm32-emscripten` (Emscripten 3.1.73) | Separate Linux/WASM ZIPs and outputs; native synthetic tests, WebAssembly export checks, browser and HTTP smoke tests |
| [`windows.yml`](../.github/workflows/windows.yml) | Windows x64, `x64-windows` | Native game ZIPs, outputs, synthetic tests |
| [`macos.yml`](../.github/workflows/macos.yml) | macOS native architecture matrix, dynamic vcpkg triplets | App/ZIP artifacts and synthetic tests |
| [`android.yml`](../.github/workflows/android.yml) | Android ARM64, NDK and Gradle | Debug APK variants and ZIP, APK structure checks |

The previous standalone `wasm.yml` was retired. Linux and WASM compile sequentially on a **single GitHub-hosted runner** rather than launching separate build VMs. Each target keeps its own CMake build directory and artifacts.

**Cache policy:** build-tree/incremental compilation caches are **not restored or saved** in any platform workflow. Binary archives of **vcpkg dependencies** remain cached by target triplet so unchanged third-party packages need not be rebuilt. This is *not* a cache of compiled Kotonoha game/engine objects. The Emscripten setup may cache its SDK installation; the Android Gradle setup disables its build cache. Published GitHub Actions artifacts are retained separately from caches.

In Linux, Windows, macOS and WASM, select the vcpkg feature `sdl-vcpkg` and `KOTONOHA_SDL_PROVIDER=VCPKG`. Dear ImGui is always compiled from the pinned Git submodule, automatically initialized by CMake when missing. Android uses SDL and ImGui submodules for native integration and vcpkg for other dependencies. The workflows no longer install ImGui through vcpkg.

Example Linux configure:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_EXAMPLE=ON
cmake --build build
```

For WASM, use `wasm32-emscripten` and the Emscripten CMake chainload toolchain. Choosing vcpkg-only SDL/ImGui without the vcpkg toolchain is an error, not an automatic fallback.

Source/contract workflows (such as `port-contracts.yml` and `reallive-render-regressions.yml`) may run on pushes and pull requests. A successful source/synthetic check does **not** imply full-platform compilation or retail gameplay parity. The `xcode.yml` workflow checks Xcode project configuration; it does not build or sign every Apple target.
