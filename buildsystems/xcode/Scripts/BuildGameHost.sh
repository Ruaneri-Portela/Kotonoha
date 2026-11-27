#!/usr/bin/env bash
# Builds the universal detached game host with the Xcode toolchain into
# BUILT_PRODUCTS_DIR. The launcher's embed phase copies it into the .app.
set -euo pipefail

: "${SRCROOT:?Xcode SRCROOT is required}"
: "${BUILT_PRODUCTS_DIR:?Xcode BUILT_PRODUCTS_DIR is required}"

source_file="${SRCROOT}/../../launcher/host/GameHost.cpp"
output="${BUILT_PRODUCTS_DIR}/KotonohaGameHost"

xcrun clang++ -std=c++17 \
  -I"${SRCROOT}/../../launcher/include" \
  -F"${BUILT_PRODUCTS_DIR}" \
  -framework KotonohaLauncherCore \
  -Wl,-rpath,@executable_path/../Frameworks \
  -o "${output}" "${source_file}"

printf '[KotonohaGameHost] built %s\n' "${output}"
