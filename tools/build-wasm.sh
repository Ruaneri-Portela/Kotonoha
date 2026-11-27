#!/usr/bin/env bash
# Build WebAssembly do Kotonoha com o Emscripten do MSYS2 UCRT64.
#
# Pacote: pacman -S mingw-w64-ucrt-x86_64-emscripten
#   runtime Emscripten : C:/msys64/ucrt64/lib/emscripten
#   LLVM / wasm-ld     : C:/msys64/ucrt64/opt/emscripten-llvm/bin
#   Python e Ninja     : C:/msys64/ucrt64/bin
#
# Uso (shell MSYS2 UCRT64, na raiz do repositorio):
#   ./tools/build-wasm.sh                       # configura e compila o alvo Kotonoha
#   KOTONOHA_WASM_TARGET=RealLive ./tools/build-wasm.sh
#   KOTONOHA_WASM_CONFIGURE_ONLY=1 ./tools/build-wasm.sh   # so configurar
# Argumentos extras sao repassados ao cmake (ex.: -DKOTONOHA_BUILD_ALL=ON).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${KOTONOHA_WASM_BUILD_DIR:-$ROOT/build-wasm}"
TARGET="${KOTONOHA_WASM_TARGET:-Kotonoha}"
JOBS="${KOTONOHA_WASM_JOBS:-}"

EM_ROOT="${EMSCRIPTEN_ROOT:-/c/msys64/ucrt64/lib/emscripten}"
LLVM_ROOT="${LLVM_ROOT:-/c/msys64/ucrt64/opt/emscripten-llvm/bin}"
UCRT_BIN="${UCRT_BIN:-/c/msys64/ucrt64/bin}"
CMAKE_BIN="${KOTONOHA_CMAKE_BIN:-/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin}"

# O VCPKG_ROOT do Windows usa '\\'; normalize para o formato MSYS.
if [[ -z "${VCPKG_ROOT:-}" ]]; then
  VCPKG_ROOT="/c/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg"
fi
if command -v cygpath >/dev/null 2>&1; then
  VCPKG_ROOT="$(cygpath -u "$VCPKG_ROOT")"
  EM_ROOT="$(cygpath -u "$EM_ROOT")"
  LLVM_ROOT="$(cygpath -u "$LLVM_ROOT")"
  UCRT_BIN="$(cygpath -u "$UCRT_BIN")"
  CMAKE_BIN="$(cygpath -u "$CMAKE_BIN")"
fi

export PATH="$CMAKE_BIN:$EM_ROOT:$LLVM_ROOT:$UCRT_BIN:$PATH"
export EMSCRIPTEN_ROOT="$EM_ROOT"

VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
EMSCRIPTEN_TOOLCHAIN="$EM_ROOT/cmake/Modules/Platform/Emscripten.cmake"

if [[ ! -f "$VCPKG_TOOLCHAIN" ]]; then
  echo "[Kotonoha] vcpkg.cmake nao encontrado: $VCPKG_TOOLCHAIN" >&2
  exit 1
fi
if [[ ! -f "$EMSCRIPTEN_TOOLCHAIN" ]]; then
  echo "[Kotonoha] Emscripten.cmake nao encontrado: $EMSCRIPTEN_TOOLCHAIN" >&2
  exit 1
fi
if ! command -v emcmake >/dev/null 2>&1; then
  echo "[Kotonoha] emcmake nao encontrado no PATH" >&2
  exit 1
fi

echo "[Kotonoha] EMSCRIPTEN_ROOT=$EMSCRIPTEN_ROOT"
echo "[Kotonoha] VCPKG_ROOT=$VCPKG_ROOT"
echo "[Kotonoha] build dir: $BUILD_DIR"

emcmake cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSCRIPTEN_TOOLCHAIN" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_PORTS="$ROOT/vcpkg-overlays/ports" \
  '-DVCPKG_MANIFEST_FEATURES=sdl-vcpkg;imgui-vcpkg' \
  -DKOTONOHA_SDL_PROVIDER=VCPKG \
  -DKOTONOHA_IMGUI_PROVIDER=VCPKG \
  -DKOTONOHA_BUILD_EXAMPLE=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  "$@"

if [[ "${KOTONOHA_WASM_CONFIGURE_ONLY:-0}" == "1" ]]; then
  echo "[Kotonoha] configuracao concluida (KOTONOHA_WASM_CONFIGURE_ONLY=1)"
  exit 0
fi

BUILD_ARGS=(--build "$BUILD_DIR" --target "$TARGET")
if [[ -n "$JOBS" ]]; then
  BUILD_ARGS+=(--parallel "$JOBS")
fi
cmake "${BUILD_ARGS[@]}"

echo "[Kotonoha] build-wasm concluido: $BUILD_DIR/outputs/$TARGET"
ls -lh "$BUILD_DIR/outputs/$TARGET"
