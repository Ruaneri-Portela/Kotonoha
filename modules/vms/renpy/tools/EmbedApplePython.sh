#!/usr/bin/env bash
# Shared RenPy runtime: package the prepared CPython 3.15 standard library and
# extension frameworks, following tools/PrepareApple.py in the repository.
# The main Python.framework comes from the XCFramework in the Xcode Embed phase.
set -euo pipefail

note() { printf '[Kotonoha/RenPy/Python] %s\n' "$*"; }
fatal() { printf 'error: [Kotonoha/RenPy/Python] %s\n' "$*" >&2; exit 1; }

: "${SRCROOT:?Missing SRCROOT}"
: "${TARGET_BUILD_DIR:?Missing TARGET_BUILD_DIR}"
: "${FRAMEWORKS_FOLDER_PATH:?Missing FRAMEWORKS_FOLDER_PATH}"

repo="${KOTONOHA_ROOT:-${SRCROOT}/../..}"
frameworks="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"
platform="${PLATFORM_NAME:-}"

[[ "${TARGET_NAME:-}" == DokiDoki-* || "${TARGET_NAME:-}" == KatawaShoujo-* || "${TARGET_NAME:-}" == KotonohaLauncher-* ]] || fatal 'Script may only run in DDLC/Katawa application targets'

install_framework() {
  local source="$1"
  local dest="${frameworks}/$(basename "$source")"
  [[ -f "$source/$(basename "$source" .framework)" ]] || fatal "Not a compiled framework: $source"
  mkdir -p "$frameworks"
  rm -rf "$dest"
  /usr/bin/ditto "$source" "$dest"
  if [[ "${CODE_SIGNING_ALLOWED:-NO}" == YES ]]; then
    local identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"
    if [[ -z "$identity" && "$platform" == iphoneos ]]; then
      fatal "Missing iOS device signing identity for $dest"
    fi
    [[ -n "$identity" ]] || identity='-'
    /usr/bin/codesign --force --sign "$identity" --timestamp=none "$dest"
    /usr/bin/codesign --verify "$dest" || fatal "Invalid signature for $dest"
  fi
}

case "$platform" in
  macosx)
    # The Python XCFramework build phase is authoritative. The user's
    # preparation script also creates build/macOS-deps/Frameworks/Python.framework.
    [[ -f "${frameworks}/Python.framework/Python" ]] || {
      prepared="${repo}/build/macOS-deps/Frameworks/Python.framework"
      if [[ ! -f "${prepared}/Python" ]]; then
        [[ -f "${repo}/tools/PrepareApple.py" ]] || fatal 'tools/PrepareApple.py is missing'
        /usr/bin/env python3 "${repo}/tools/PrepareApple.py" --platform macos
      fi
      [[ -f "${prepared}/Python" ]] || fatal "Compiled macOS Python.framework missing: $prepared"
      install_framework "$prepared"
    }
    # Xcode embedding the framework binary is not enough: the CPython
    # stdlib (especially encodings) must also live inside the .app.
    stdlib="${frameworks}/Python.framework/Versions/Current/lib/python3.15/encodings/__init__.py"
    if [[ ! -f "$stdlib" ]]; then
      prepared="${repo}/build/macOS-deps/Frameworks/Python.framework"
      if [[ ! -f "${prepared}/Versions/Current/lib/python3.15/encodings/__init__.py" ]]; then
        /usr/bin/env python3 "${repo}/tools/PrepareApple.py" --platform macos
      fi
      [[ -f "${prepared}/Versions/Current/lib/python3.15/encodings/__init__.py" ]] ||
        fatal "Prepared macOS CPython 3.15 standard library is missing"
      install_framework "$prepared"
    fi
    [[ -f "$stdlib" ]] || fatal "CPython 3.15 encodings missing from macOS application bundle"
    note 'Python.framework and its CPython 3.15 standard library are bundled'
    ;;
  iphoneos|iphonesimulator)
    prepared="${repo}/build/iOS-deps/python/native/${platform}"
    if [[ ! -d "${prepared}/python/lib/python3.15" ]]; then
      [[ -f "${repo}/tools/PrepareApple.py" ]] || fatal 'tools/PrepareApple.py is missing'
      /usr/bin/env python3 "${repo}/tools/PrepareApple.py" --platform ios
    fi
    [[ -f "${prepared}/python/lib/python3.15/encodings/__init__.py" ]] || fatal "Prepared CPython 3.15 stdlib missing: $prepared"
    : "${UNLOCALIZED_RESOURCES_FOLDER_PATH:?Missing UNLOCALIZED_RESOURCES_FOLDER_PATH}"
    destination="${TARGET_BUILD_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/python"
    mkdir -p "$(dirname "$destination")"
    rm -rf "$destination"
    /usr/bin/ditto "${prepared}/python" "$destination"
    note "Installed CPython 3.15 stdlib and .fwork markers -> $destination"
    if [[ -d "${prepared}/Frameworks" ]]; then
      shopt -s nullglob
      extension_frameworks=("${prepared}/Frameworks/"*.framework)
      for framework in "${extension_frameworks[@]}"; do
        install_framework "$framework"
        note "Installed Python extension $(basename "$framework")"
      done
      shopt -u nullglob
    fi
    [[ -f "${frameworks}/Python.framework/Python" ]] || fatal 'Python.framework missing from Xcode Embed Frameworks phase'
    [[ -f "${destination}/lib/python3.15/encodings/__init__.py" ]] ||
      fatal 'CPython 3.15 encodings missing from iOS application resources'
    ;;
  *) fatal "Unsupported SDK platform: $platform" ;;
esac
