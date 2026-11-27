#!/bin/zsh
# Abre a interface JavaFX de teste da ponte. Requer build.sh ja executado.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(/usr/libexec/java_home -v 21)}"
export JAVA_HOME

# O motor procura assets/fonts no diretorio de trabalho.
CORE_LIB="${KOTONOHA_CORE:-$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib}"
BUILD_BIN="$(dirname "$(dirname "$CORE_LIB")")/bin"
GAME_HOST="$BUILD_BIN/KotonohaGameHost"
[[ -f "$GAME_HOST" ]] && export KOTONOHA_DETACHED_HOST="$GAME_HOST"
export KOTONOHA_DETACHED_LOG="$APP/game-host.log"
[[ -d "$BUILD_BIN" ]] && cd "$BUILD_BIN"

exec "$JAVA_HOME/bin/java" \
  --module-path "$APP/lib" --add-modules javafx.controls \
  -Dkotonoha.coreLib="$CORE_LIB" \
  -cp "$APP/out" me.hirameki.kotonoha.bridgetest.BridgeTestApp "$@"
