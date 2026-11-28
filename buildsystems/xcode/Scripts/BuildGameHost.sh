#!/usr/bin/env bash
# Builds the universal detached game host with the Xcode toolchain into
# BUILT_PRODUCTS_DIR. The launcher's embed phase copies it into the .app.
set -euo pipefail

: "${SRCROOT:?Xcode SRCROOT is required}"
: "${BUILT_PRODUCTS_DIR:?Xcode BUILT_PRODUCTS_DIR is required}"

source_file="${SRCROOT}/../../launcher/host/GameHost.cpp"
output="${BUILT_PRODUCTS_DIR}/KotonohaGameHost"

# Xcode builds the app universal (ARCHS = "arm64 x86_64" in Common.xcconfig),
# but a plain `xcrun clang++` invocation defaults to the host architecture.
# Compile one slice per requested arch and join them so the detached helper
# matches KotonohaLauncherCore and the main executable.
archs="${ARCHS:-$(uname -m)}"
if [[ -z "${archs// /}" ]]; then
  archs="$(uname -m)"
fi

read -r -a arch_list <<< "${archs}"
slices=()
for arch in "${arch_list[@]}"; do
  slice="${output}.${arch}"
  xcrun clang++ -std=c++17 -arch "${arch}" \
    -I"${SRCROOT}/../../launcher/include" \
    -F"${BUILT_PRODUCTS_DIR}" \
    -framework KotonohaLauncherCore \
    -Wl,-rpath,@executable_path/../Frameworks \
    -o "${slice}" "${source_file}"
  slices+=("${slice}")
done

rm -f "${output}"
if [[ "${#slices[@]}" -eq 1 ]]; then
  mv "${slices[0]}" "${output}"
else
  xcrun lipo -create "${slices[@]}" -output "${output}"
  rm -f "${slices[@]}"
fi

printf '[KotonohaGameHost] built %s (%s)\n' "${output}" "${archs}"
