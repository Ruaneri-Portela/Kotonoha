#!/bin/zsh
# Build do exemplo de integracao JNI. Nao ha shim: os simbolos JNI estao
# exportados pelo proprio libKotonohaLauncherCore.dylib do CMake.
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
cd "$APP"
export KOTONOHA_REPO="$(cd "$APP/../../.." && pwd)"

JAVA_HOME="${JAVA_HOME:-$(/usr/libexec/java_home -v 21)}"
export JAVA_HOME
JH="$JAVA_HOME"
CORE="$KOTONOHA_REPO/out/build/macos-arm64-vcpkg/lib/libKotonohaLauncherCore.dylib"
JFX_VERSION="21.0.2"
JFX_BASE="https://repo1.maven.org/maven2/org/openjfx"

if [[ ! -f "$CORE" ]]; then
  echo "[build] Core nao encontrado: $CORE" >&2
  echo "[build] Compile primeiro: cmake --build out/build/macos-arm64-vcpkg --target KotonohaLauncherCore" >&2
  exit 1
fi

mkdir -p lib out

# JavaFX 21 (macOS arm64) sem Maven/Gradle; jars ficam em lib/ (ignorados).
for module in javafx-base javafx-graphics javafx-controls javafx-media; do
  jar="lib/$module-$JFX_VERSION-mac-aarch64.jar"
  if [[ ! -s "$jar" ]]; then
    echo "[build] baixando $jar"
    curl -fsSL "$JFX_BASE/$module/$JFX_VERSION/$module-$JFX_VERSION-mac-aarch64.jar" -o "$jar"
  fi
done

echo "[build] compilando classes Java contra a C ABI/JNI do core"
"$JH/bin/javac" --module-path lib --add-modules javafx.controls \
  -d out $(find src -name '*.java' -print)

echo "[build] OK: $APP/out (core: $CORE)"
