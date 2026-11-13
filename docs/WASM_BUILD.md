# Building for WebAssembly (Wasm)

This guide provides instructions for compiling the Kotonoha engine for WebAssembly using Emscripten.

## Prerequisites

- **Emscripten SDK (emsdk)**: Must be installed and activated in your current shell environment.
- **vcpkg**: Required for dependency management.

## Build Steps

### 1. Configure the Build
Create a build directory named `build-web` and configure the project using CMake. You must specify the `wasm32-emscripten` triplet and point to the `ffmpeg` overlay ports.

```bash
cmake -S . -B build-web \
  -DCMAKE_TOOLCHAIN_FILE=<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays/ports \
  -DKOTONOHA_BUILD_ALL=ON \
  -DCMAKE_BUILD_TYPE=Release
```

*Replace `<path_to_vcpkg>` with your actual vcpkg installation path.*

### 2. Compile
Run the build command:

```bash
cmake --build build-web
```

## Important Notes

### Doki Doki Literature Club (DDLC)
Due to the complexity of embedding the Python interpreter within an Emscripten environment, **DDLC is intentionally excluded from the WebAssembly build**.

To prevent build errors for games that depend on Ren'Py/Python modules, the engine uses a stub implementation (`PythonDummy.cpp`) to satisfy the build requirements while skipping the actual Python execution.

### Dependencies
The build uses custom `vcpkg` overlay ports (specifically for `ffmpeg`) to ensure compatibility with the Emscripten target.
