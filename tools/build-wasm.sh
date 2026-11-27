#!/usr/bin/env bash
# Build WebAssembly do Kotonoha com o emsdk oficial (fora do MSYS2).
#
#   EMSDK aponta para o emsdk ativado (padrao: /e/emsdk no Windows).
#   Defina EMSDK ou carregue emsdk_env.sh antes de executar.
#
# Uso (na raiz do repositorio):
#   ./tools/build-wasm.sh                       # configura e compila o alvo Kotonoha
#   KOTONOHA_WASM_TARGET=RealLive ./tools/build-wasm.sh
#   KOTONOHA_WASM_CONFIGURE_ONLY=1 ./tools/build-wasm.sh   # so configurar
# Argumentos extras sao repassados ao cmake (ex.: -DKOTONOHA_BUILD_ALL=ON).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${KOTONOHA_WASM_BUILD_DIR:-$ROOT/build-wasm}"
TARGET="${KOTONOHA_WASM_TARGET:-Kotonoha}"
JOBS="${KOTONOHA_WASM_JOBS:-}"

# Localiza o emsdk: variavel de ambiente ou instalacao padrao no Windows.
EMSDK_DIR="${EMSDK:-}"
if [[ -z "$EMSDK_DIR" ]]; then
  for candidate in /e/emsdk /c/emsdk; do
    if [[ -f "$candidate/upstream/emscripten/emcc.py" ]]; then
      EMSDK_DIR="$candidate"
      break
    fi
  done
fi
if [[ -z "$EMSDK_DIR" || ! -f "$EMSDK_DIR/upstream/emscripten/emcc.py" ]]; then
  echo "[Kotonoha] emsdk oficial nao encontrado (defina EMSDK)." >&2
  echo "  git clone https://github.com/emscripten-core/emsdk.git E:\\emsdk" >&2
  exit 1
fi

EM_ROOT="${EMSCRIPTEN_ROOT:-$EMSDK_DIR/upstream/emscripten}"
LLVM_ROOT="${LLVM_ROOT:-$EMSDK_DIR/upstream/bin}"
CMAKE_BIN="${KOTONOHA_CMAKE_BIN:-}"

# Caminhos do Windows no formato MSYS.
if command -v cygpath >/dev/null 2>&1; then
  EMSDK_DIR="$(cygpath -u "$EMSDK_DIR")"
  EM_ROOT="$(cygpath -u "$EM_ROOT")"
  LLVM_ROOT="$(cygpath -u "$LLVM_ROOT")"
  [[ -n "$CMAKE_BIN" ]] && CMAKE_BIN="$(cygpath -u "$CMAKE_BIN")"
fi

if [[ -z "${VCPKG_ROOT:-}" ]]; then
  VCPKG_ROOT="/c/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg"
fi
if command -v cygpath >/dev/null 2>&1; then
  VCPKG_ROOT="$(cygpath -u "$VCPKG_ROOT")"
fi

export EMSDK="$EMSDK_DIR"
export EMSCRIPTEN_ROOT="$EM_ROOT"
export EM_CONFIG="$EMSDK_DIR/.emscripten"
export BINARYEN_ROOT="$EMSDK_DIR/upstream"
export PATH="$LLVM_ROOT:$EM_ROOT:$EMSDK_DIR:$CMAKE_BIN${CMAKE_BIN:+:}$PATH"

VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
EMSCRIPTEN_TOOLCHAIN="$EM_ROOT/cmake/Modules/Platform/Emscripten.cmake"

for required in "$VCPKG_TOOLCHAIN" "$EMSCRIPTEN_TOOLCHAIN"; do
  if [[ ! -f "$required" ]]; then
    echo "[Kotonoha] arquivo nao encontrado: $required" >&2
    exit 1
  fi
done
if ! command -v emcmake >/dev/null 2>&1; then
  echo "[Kotonoha] emcmake nao encontrado no PATH" >&2
  exit 1
fi

echo "[Kotonoha] EMSDK=$EMSDK EMSCRIPTEN_ROOT=$EM_ROOT VCPKG_ROOT=$VCPKG_ROOT"
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
