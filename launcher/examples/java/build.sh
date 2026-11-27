#!/usr/bin/env bash
# Build do exemplo de integracao JNI. Nao ha shim: os simbolos JNI estao
# exportados pelo proprio KotonohaLauncherCore da build nativa.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
cd "$APP"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"

if [[ -z "${JAVA_HOME:-}" ]]; then
  if command -v /usr/libexec/java_home >/dev/null 2>&1; then
    JAVA_HOME="$(/usr/libexec/java_home -v 21)"
  else
    JAVA_HOME="$(dirname "$(dirname "$(command -v java)")")"
  fi
fi
export JAVA_HOME
JH="$JAVA_HOME"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) OS=win; CORE_NAME=KotonohaLauncherCore.dll ;;
  Linux*)               OS=linux; CORE_NAME=libKotonohaLauncherCore.so ;;
  *)                    OS=mac; CORE_NAME=libKotonohaLauncherCore.dylib ;;
esac
case "$(uname -m)" in
  arm64|aarch64) JFX_ARCH="mac-aarch64" ;;
  x86_64)        JFX_ARCH="$OS" ;;
  *)             JFX_ARCH="$OS" ;;
esac

if [[ -n "${KOTONOHA_NATIVE_DIR:-}" ]]; then
  CORE="${KOTONOHA_CORE:-$KOTONOHA_NATIVE_DIR/$CORE_NAME}"
else
  CORE="${KOTONOHA_CORE:-$KOTONOHA_REPO/out/build/$([ "$OS" = win ] && echo windows-x64-msvc-vcpkg || echo macos-arm64-vcpkg)/lib/$CORE_NAME}"
fi

if [[ ! -f "$CORE" ]]; then
  echo "[build] Core nao encontrado: $CORE" >&2
  echo "[build] Configure e compile o KotonohaLauncherCore da plataforma." >&2
  exit 1
fi

mkdir -p lib out

JFX_VERSION="21.0.2"
JFX_BASE="https://repo1.maven.org/maven2/org/openjfx"
# JavaFX 21 sem Maven/Gradle; jars ficam em lib/ (ignorados pelo git).
for module in javafx-base javafx-graphics javafx-controls javafx-media; do
  jar="lib/$module-$JFX_VERSION-$JFX_ARCH.jar"
  if [[ ! -s "$jar" ]]; then
    echo "[build] baixando $jar"
    curl -fsSL "$JFX_BASE/$module/$JFX_VERSION/$module-$JFX_VERSION-$JFX_ARCH.jar" -o "$jar"
  fi
done

echo "[build] compilando classes Java contra a C ABI/JNI do core"
"$JH/bin/javac" --module-path lib --add-modules javafx.controls \
  -d out $(find src -name '*.java' -print)

echo "[build] OK: $APP/out (core: $CORE)"
