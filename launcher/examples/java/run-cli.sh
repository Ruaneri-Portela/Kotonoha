#!/bin/zsh
# Harness headless: exercita TODA a C ABI e lanca cada jogo detectado.
# Uso: ./run-cli.sh [/Volumes/Server/data2/Games ...] [--dry-run]
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(/usr/libexec/java_home -v 21)}"
export JAVA_HOME

export KOTONOHA_LAUNCHER_LAUNCH_FRAMES="${KOTONOHA_LAUNCHER_LAUNCH_FRAMES:-1}"
unset KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN
ARGS=()
for arg in "$@"; do
  if [[ "$arg" == "--dry-run" ]]; then
    export KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN=1
  else
    ARGS+=("$arg")
  fi
done

# O motor procura assets/fonts no diretorio de trabalho.
BUILD_BIN="$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/bin"
[[ -d "$BUILD_BIN" ]] && cd "$BUILD_BIN"

exec "$JAVA_HOME/bin/java" \
  -Dkotonoha.coreLib="$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib" \
  -cp "$APP/out" me.hirameki.kotonoha.bridgetest.BridgeTestCli "${ARGS[@]}"
