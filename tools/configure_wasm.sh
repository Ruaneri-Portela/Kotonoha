#!/bin/sh
# Configure the `wasm` CMake preset with a valid EMSCRIPTEN_ROOT.
#
# emsdk layouts already export EMSDK/EMSCRIPTEN_ROOT via emsdk_env.sh.
# Homebrew's Emscripten installs emcc in a versioned Cellar prefix, which
# vcpkg's wasm32-emscripten triplet does not discover on its own. This
# helper derives the root from the emcc binary on PATH (resolving symlinks
# and checking both `cmake/` and `libexec/cmake/` layouts).
set -eu

valid_root() {
    [ -n "${1:-}" ] && [ -f "$1/cmake/Modules/Platform/Emscripten.cmake" ]
}

if valid_root "${EMSCRIPTEN_ROOT:-}"; then
    :
elif [ -n "${EMSDK:-}" ] && valid_root "$EMSDK/upstream/emscripten"; then
    EMSCRIPTEN_ROOT="$EMSDK/upstream/emscripten"
    export EMSCRIPTEN_ROOT
else
    emcc_path=$(command -v emcc || true)
    if [ -z "$emcc_path" ]; then
        echo "[Kotonoha] emcc not found. Activate emsdk first:" >&2
        echo "  source /path/to/emsdk/emsdk_env.sh" >&2
        exit 1
    fi
    # macOS has no readlink -f; follow symlinks with a bounded loop.
    while [ -L "$emcc_path" ]; do
        target=$(readlink "$emcc_path")
        case "$target" in
            /*) emcc_path=$target ;;
            *) emcc_path=$(dirname "$emcc_path")/$target ;;
        esac
    done
    dir=$(dirname "$emcc_path")
    while [ "$dir" != / ]; do
        if valid_root "$dir" || valid_root "$dir/libexec"; then
            if valid_root "$dir"; then
                EMSCRIPTEN_ROOT=$dir
            else
                EMSCRIPTEN_ROOT=$dir/libexec
            fi
            export EMSCRIPTEN_ROOT
            break
        fi
        dir=$(dirname "$dir")
    done
fi

if ! valid_root "${EMSCRIPTEN_ROOT:-}"; then
    echo "[Kotonoha] Emscripten.cmake not found near emcc." >&2
    echo "Set EMSCRIPTEN_ROOT to the directory containing" >&2
    echo "cmake/Modules/Platform/Emscripten.cmake." >&2
    exit 1
fi

echo "[Kotonoha] EMSCRIPTEN_ROOT=$EMSCRIPTEN_ROOT"
exec cmake --preset wasm "$@"
