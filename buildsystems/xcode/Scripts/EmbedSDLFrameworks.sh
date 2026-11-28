#!/usr/bin/env bash
# Embed the SDL3 and SDL3_ttf frameworks built by the SDL Xcode subprojects
# into the app, verify the Xcode-signed Kotonoha frameworks, and copy the
# remaining @rpath dylibs from the active vcpkg install root.
#
# SDL3 / SDL3_ttf / ImGui come from vcpkg. The vcpkg copy loop is required so
# the launcher does not fail at launch with "Library not loaded" for
# json-c / ffmpeg / ass and their dependencies.
set -euo pipefail

note() { printf '[Kotonoha/Bundle] %s\n' "$*"; }
fatal() { printf 'error: [Kotonoha/Bundle] %s\n' "$*" >&2; exit 1; }

: "${SRCROOT:?Xcode SRCROOT is required}"
: "${CONFIGURATION:?Xcode CONFIGURATION is required}"
: "${TARGET_BUILD_DIR:?Xcode TARGET_BUILD_DIR is required}"
: "${FRAMEWORKS_FOLDER_PATH:?Xcode FRAMEWORKS_FOLDER_PATH is required}"
: "${UNLOCALIZED_RESOURCES_FOLDER_PATH:?Xcode UNLOCALIZED_RESOURCES_FOLDER_PATH is required}"
: "${KOTONOHA_GAME_DIR:?KOTONOHA_GAME_DIR must be defined per app target}"

repo="${KOTONOHA_ROOT:-${SRCROOT}/../..}"
framework_destination="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"
resources_destination="${TARGET_BUILD_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}"
platform_suffix="${EFFECTIVE_PLATFORM_NAME:-}"

[[ -d "${repo}/assets" ]] || fatal "Shared resources not found: ${repo}/assets"
if [[ "${KOTONOHA_GAME_DIR}" != "launcher" ]]; then
  [[ -d "${repo}/games/${KOTONOHA_GAME_DIR}/game-assets" ]] || fatal "Game resources not found: ${repo}/games/${KOTONOHA_GAME_DIR}/game-assets"
fi
[[ -f "${SRCROOT}/Info.plist" ]] || fatal "Shared Info.plist not found: ${SRCROOT}/Info.plist"

# Xcode's Resources build phase copies both folder references, keeping their names.
for name in assets game-assets; do
  if [[ "${name}" == "game-assets" && "${KOTONOHA_GAME_DIR}" == "launcher" ]]; then
    mkdir -p "${resources_destination}/game-assets"
    if [[ -f "${repo}/games/school-days/game-assets/school-days-hq.ktnroute" ]]; then
      cp -f "${repo}/games/school-days/game-assets/school-days-hq.ktnroute" \
        "${resources_destination}/game-assets/school-days-hq.ktnroute"
      note "Bundled school-days-hq.ktnroute into launcher game-assets/"
    fi
  fi
  [[ -d "${resources_destination}/${name}" ]] || fatal "Missing ${resources_destination}/${name}. Check Copy Bundle Resources."
  note "Verified ${name}/ in app resources"
done

# Copy every @rpath dependency from the active vcpkg root into the app bundle
# so @executable_path/Frameworks can resolve json-c, ffmpeg, ass, etc. at launch.
vcpkg_lib="${KOTONOHA_VCPKG_LIB:-}"
# Xcode UI builds use ARCHS="arm64 x86_64" with ONLY_ACTIVE_ARCH=YES, so the
# xcconfig [arch=...] conditions do not select a triplet. Resolve it from the
# architectures actually present in the produced binary instead.
executable="${TARGET_BUILD_DIR}/${EXECUTABLE_PATH:-}"
product_archs=""
vcpkg_lib_secondary=""
# Only macOS has separate arm64/x86_64 vcpkg roots; iOS keeps the SDK root
# selected by the xcconfig.
if [[ "${PLATFORM_NAME:-}" == "macosx" ||
      "${SDKROOT:-}" == *MacOSX* ]]; then
  if [[ -n "${executable}" && -f "${executable}" ]]; then
    product_archs="$(lipo -archs "${executable}" 2>/dev/null || true)"
  fi
  if [[ "${product_archs}" == *x86_64* && "${product_archs}" != *arm64* ]]; then
    vcpkg_lib="${repo}/out/build/macos-amd64-vcpkg/vcpkg_installed/macos-amd64-vcpkg/lib"
  elif [[ "${product_archs}" == *arm64* && "${product_archs}" != *x86_64* ]]; then
    vcpkg_lib="${repo}/out/build/macos-arm64-vcpkg/vcpkg_installed/macos-arm64-vcpkg/lib"
  elif [[ "${product_archs}" == *x86_64* && "${product_archs}" == *arm64* ]]; then
    vcpkg_lib_secondary="${repo}/out/build/macos-amd64-vcpkg/vcpkg_installed/macos-amd64-vcpkg/lib"
  fi
fi
if [[ -n "${vcpkg_lib}" && -d "${vcpkg_lib}" ]]; then
  note "Using vcpkg libraries for ${product_archs:-active arch}: ${vcpkg_lib}"
  copied=0
  while :; do
    changed=0
    while IFS= read -r dep; do
      [[ "${dep}" == @rpath/* ]] || continue
      name="${dep#@rpath/}"
      # Framework binaries (e.g. Python.framework/Python) are embedded by
      # Xcode's own phase; never remove or replace them here.
      [[ "${name}" == */* ]] && continue
      dest="${framework_destination}/${name}"
      src="${vcpkg_lib}/${name}"
      # Reuse an existing dylib with the same slices; replace it only when a
      # previous build for another architecture left an incompatible copy.
      if [[ -e "${dest}" ]]; then
        src_archs="$(lipo -archs "${src}" 2>/dev/null || true)"
        dest_archs="$(lipo -archs "${dest}" 2>/dev/null || true)"
        # A universal destination already contains the source slice; do not
        # replace it on every pass or the copy loop never terminates.
        keep=1
        for arch in ${src_archs}; do
          [[ " ${dest_archs} " == *" ${arch} "* ]] || keep=0
        done
        [[ "${keep}" -eq 1 ]] && continue
        rm -f "${dest}"
      fi
      if [[ ! -e "${src}" ]]; then
        note "WARNING: @rpath dependency ${name} is not in ${vcpkg_lib}"
        continue
      fi
      mkdir -p "${framework_destination}"
      if [[ -n "${vcpkg_lib_secondary}" && -f "${vcpkg_lib_secondary}/${name}" ]]; then
        lipo -create "${src}" "${vcpkg_lib_secondary}/${name}" -output "${dest}" ||
          fatal "Could not join universal dependency ${name}"
      else
        ditto "${src}" "${dest}"
      fi
      if [[ "${CODE_SIGNING_ALLOWED:-NO}" == "YES" ]]; then
        command -v codesign >/dev/null 2>&1 || fatal 'codesign not available for signing embedded dylibs'
        local_identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"
        [[ -n "${local_identity}" ]] || local_identity='-'
        codesign --force --sign "${local_identity}" --timestamp=none "${dest}" || \
          fatal "Could not sign embedded dylib ${dest}"
      fi
      copied=$((copied + 1))
      changed=1
    done < <(
      {
        otool -L "${TARGET_BUILD_DIR}/${EXECUTABLE_PATH}" 2>/dev/null || true
        for fw in "${framework_destination}"/*.framework; do
          [[ -f "${fw}/$(basename "${fw%.framework}")" ]] || continue
          otool -L "${fw}/$(basename "${fw%.framework}")" 2>/dev/null || true
        done
        for dylib in "${framework_destination}"/*.dylib; do
          [[ -e "${dylib}" ]] || continue
          otool -L "${dylib}" 2>/dev/null || true
        done
      } | awk '{print $1}'
    )
    [[ "${changed}" -eq 1 ]] || break
  done
  note "Embedded ${copied} dynamic vcpkg library dependency/ dependencies"
fi
# The launcher is the only app that ships the detached game host. Other game
# bundles launch through the hub, so they do not need a second executable.
if [[ "${KOTONOHA_GAME_DIR:-}" == "launcher" ]]; then
  host_source="${TARGET_BUILD_DIR}/KotonohaGameHost"
  if [[ -f "${host_source}" ]]; then
    host_dir="$(dirname "${TARGET_BUILD_DIR}/${EXECUTABLE_PATH}")"
    mkdir -p "${host_dir}"
    ditto "${host_source}" "${host_dir}/KotonohaGameHost"
    if [[ "${CODE_SIGNING_ALLOWED:-NO}" == "YES" ]]; then
      local_identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"
      [[ -n "${local_identity}" ]] || local_identity='-'
      codesign --force --sign "${local_identity}" --timestamp=none \
        "${host_dir}/KotonohaGameHost" ||
        fatal "Could not sign embedded KotonohaGameHost"
    fi
    note "Embedded KotonohaGameHost into the launcher app"
  fi
fi

# Install iOS art from the versioned design/generated/ source of truth into
# the bundle root (the location CFBundleIcons expects) without changing the
# shared Info.plist. Never stage the design/ folder as a generic asset.
if [[ "${PLATFORM_NAME:-}" == iphoneos || "${PLATFORM_NAME:-}" == iphonesimulator ]]; then
  # The Xcode pre-action (tools/PrepareApple.py) generates the brand assets
  # once; only import the files the iOS bundle needs, and only copy the
  # splash when changed so repeated builds never stage it twice.
  brand="${repo}/design/generated"
  cp "${brand}/apple/AppIcon-120.png" "${resources_destination}/KotonohaIcon120.png"
  cp "${brand}/apple/AppIcon-180.png" "${resources_destination}/KotonohaIcon180.png"
  cp "${brand}/apple/AppIcon-76.png" "${resources_destination}/KotonohaIcon76.png"
  cp "${brand}/apple/AppIcon-1024.png" "${resources_destination}/KotonohaIcon1024.png"
  if ! cmp -s "${brand}/splash/SPLASH.PNG" "${resources_destination}/assets/SPLASH.PNG"; then
    cp "${brand}/splash/SPLASH.PNG" "${resources_destination}/assets/SPLASH.PNG"
  fi
  note "Installed iOS AppIcon sizes and black universal splash"
fi
note "Both SDL frameworks, vcpkg dylibs and both asset directories are present for ${TARGET_NAME:-app}"
