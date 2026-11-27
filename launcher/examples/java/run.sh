#!/usr/bin/env bash
# Abre a interface JavaFX de teste da ponte. Requer build.sh ja executado.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"
if [[ -z "${JAVA_HOME:-}" ]]; then
  if command -v /usr/libexec/java_home >/dev/null 2>&1; then
    JAVA_HOME="$(/usr/libexec/java_home -v 21)"
  else
    JAVA_HOME="$(dirname "$(dirname "$(command -v java)")")"
  fi
fi
export JAVA_HOME

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) OS=win; CORE_NAME=KotonohaLauncherCore.dll ;;
  Linux*)               OS=linux; CORE_NAME=libKotonohaLauncherCore.so ;;
  *)                    OS=mac; CORE_NAME=libKotonohaLauncherCore.dylib ;;
esac
if [[ -n "${KOTONOHA_NATIVE_DIR:-}" ]]; then
  CORE_LIB="${KOTONOHA_CORE:-$KOTONOHA_NATIVE_DIR/$CORE_NAME}"
else
  CORE_LIB="${KOTONOHA_CORE:-$KOTONOHA_REPO/out/build/$([ "$OS" = win ] && echo windows-x64-msvc-vcpkg || echo macos-arm64-vcpkg)/lib/$CORE_NAME}"
fi

NATIVE_DIR="$(dirname "$CORE_LIB")"
if [[ "$OS" == win ]]; then
  # O Windows procura DLLs dependentes no PATH, nao no diretorio do core.
  NATIVE_MSYS="$(cygpath -u "$NATIVE_DIR" 2>/dev/null || printf '%s' "$NATIVE_DIR")"
  export PATH="$NATIVE_MSYS:$PATH"
fi
BUILD_BIN="$(dirname "$NATIVE_DIR")/bin"
GAME_HOST="$BUILD_BIN/KotonohaGameHost"
[[ -f "$GAME_HOST" ]] && export KOTONOHA_DETACHED_HOST="$GAME_HOST"
export KOTONOHA_DETACHED_LOG="$APP/game-host.log"
[[ -d "$BUILD_BIN" ]] && cd "$BUILD_BIN"

exec "$JAVA_HOME/bin/java" \
  --module-path "$APP/lib" --add-modules javafx.controls \
  -Dkotonoha.coreLib="$CORE_LIB" \
  -cp "$APP/out" me.hirameki.kotonoha.bridgetest.BridgeTestApp "$@"
