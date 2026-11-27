#!/bin/zsh
# Harness headless: exercita TODA a C ABI e lanca cada jogo detectado.
# Uso: ./run-cli.sh [/Volumes/Server/data2/Games ...] [--dry-run] [--headless]
# Sem flags: janela real, cada jogo roda ate' o usuario fechar.
# --dry-run: valida a request sem SDL. --headless: SDL dummy + frame limit.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(/usr/libexec/java_home -v 21)}"
export JAVA_HOME

# No real launch, the window stays open until the user closes it (no frame cap).
# Use KOTONOHA_LAUNCHER_LAUNCH_FRAMES=N explicitly for a bounded test run.
unset KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN
unset KOTONOHA_LAUNCHER_LAUNCH_HEADLESS
ARGS=()
for arg in "$@"; do
  case "$arg" in
    --dry-run)
      export KOTONOHA_LAUNCHER_LAUNCH_DRY_RUN=1
      ARGS+=("--dry-run")
      ;;
    --headless)
      export KOTONOHA_LAUNCHER_LAUNCH_HEADLESS=1
      ;;
    *)
      ARGS+=("$arg")
      ;;
  esac
done

# O motor procura assets/fonts no diretorio de trabalho.
BUILD_BIN="$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/bin"
[[ -d "$BUILD_BIN" ]] && cd "$BUILD_BIN"

CORE_LIB="${KOTONOHA_CORE:-$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib}"
GAME_HOST="$(dirname "$(dirname "$CORE_LIB")")/bin/KotonohaGameHost"
[[ -f "$GAME_HOST" ]] && export KOTONOHA_DETACHED_HOST="$GAME_HOST"
export KOTONOHA_DETACHED_LOG="$APP/game-host.log"

exec "$JAVA_HOME/bin/java" \
  -Dkotonoha.coreLib="$CORE_LIB" \
  -cp "$APP/out" me.hirameki.kotonoha.bridgetest.BridgeTestCli "${ARGS[@]}"
