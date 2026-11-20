#!/usr/bin/env bash
# Xcode aggregate build bridge for the CMake-owned unified in-process hub.
# Unlike a game executable, the hub links every game runtime and its providers.
# Keep this as one authoritative CMake build to avoid diverging Xcode link lists.
set -euo pipefail
: "${SRCROOT:?Xcode SRCROOT must be set}"
repo="$(cd "${SRCROOT}/../.." && pwd)"
config="${CONFIGURATION:-Debug}"
case "$config" in
  Debug|Release|RelWithDebInfo|MinSizeRel) ;;
  *) echo "error: Unsupported CMake build configuration: $config" >&2; exit 1 ;;
esac
build="${repo}/out/build/xcode-unified-${config}"
cmake_bin="$(command -v cmake || true)"
[[ -n "$cmake_bin" ]] || { echo 'error: CMake is required for KotonohaLauncher' >&2; exit 1; }
if command -v ninja >/dev/null 2>&1; then
  generator=Ninja
else
  generator="Unix Makefiles"
fi
"$cmake_bin" -S "$repo" -B "$build" -G "$generator" \
  -DCMAKE_BUILD_TYPE="$config" \
  -DCMAKE_OSX_ARCHITECTURES="${ARCHS:-arm64}" \
  -DKOTONOHA_BUILD_ALL=ON \
  -DKOTONOHA_BUILD_MACOS_BUNDLES=ON \
  -DBUILD_TESTING=OFF
"$cmake_bin" --build "$build" --target KotonohaLauncher --parallel "${KOTONOHA_JOBS:-4}"
app="$build/outputs/KotonohaLauncher.app"
[[ -d "$app" ]] || { echo "error: Unified launcher bundle missing: $app" >&2; exit 1; }
echo "[Kotonoha/Xcode] Unified launcher built: $app"
