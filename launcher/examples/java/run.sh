#!/bin/zsh
# Abre a interface JavaFX de teste da ponte. Requer build.sh ja executado.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(/usr/libexec/java_home -v 21)}"
export JAVA_HOME

# O motor procura assets/fonts no diretorio de trabalho.
BUILD_BIN="$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/bin"
[[ -d "$BUILD_BIN" ]] && cd "$BUILD_BIN"

exec "$JAVA_HOME/bin/java" \
  --module-path "$APP/lib" --add-modules javafx.controls \
  -Dkotonoha.coreLib="$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib" \
  -cp "$APP/out" me.hirameki.kotonoha.bridgetest.BridgeTestApp "$@"
