#!/usr/bin/env bash
# Embed SDL3 and SDL3_ttf into the app after Xcode's Copy Bundle Resources phase.
# Frameworks must have been built for the target SDK and configuration beforehand.
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
[[ -d "${repo}/games/${KOTONOHA_GAME_DIR}/game-assets" ]] || fatal "Game resources not found: ${repo}/games/${KOTONOHA_GAME_DIR}/game-assets"
[[ -f "${SRCROOT}/Info.plist" ]] || fatal "Shared Info.plist not found: ${SRCROOT}/Info.plist"

# Xcode's Resources build phase copies both folder references, keeping their names.
for name in assets game-assets; do
  [[ -d "${resources_destination}/${name}" ]] || fatal "Missing ${resources_destination}/${name}. Check Copy Bundle Resources."
  note "Verified ${name}/ in app resources"
done

find_framework() {
  local name="$1"
  local custom="$2"
  local base="$3"
  local binary="$4"
  local candidate
  local -a locations=()

  # Explicit overrides must be exact .framework directories (one per SDK).
  if [[ -n "${custom}" ]]; then
    locations+=("${custom}")
  else
    locations+=("${base}/${CONFIGURATION}${platform_suffix}/${name}.framework")
    # On macOS EFFECTIVE_PLATFORM_NAME is normally empty.
    if [[ "${platform_suffix}" == "" ]]; then
      locations+=("${base}/${CONFIGURATION}/${name}.framework")
    fi
  fi

  for candidate in "${locations[@]}"; do
    if [[ -d "${candidate}" && -f "${candidate}/${binary}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  fatal "${name}.framework with a real ${binary} binary was not found. Build ${name} for ${CONFIGURATION}${platform_suffix} first. Checked: ${locations[*]} (SDKROOT=${SDKROOT:-unset}). A .tbd stub is not a redistributable binary."
}

install_framework() {
  local name="$1"
  local source="$2"
  local dest="${framework_destination}/${name}.framework"
  [[ "${source}" != "${dest}" ]] || fatal "Source and destination are the same: ${source}"

  mkdir -p "${framework_destination}"
  rm -rf "${dest}"
  if command -v ditto >/dev/null 2>&1; then
    ditto "${source}" "${dest}"
  else
    # Used only for testing without Apple's ditto; Xcode on macOS uses ditto.
    cp -a "${source}" "${dest}"
  fi
  [[ -f "${dest}/${name}" ]] || fatal "Framework executable missing after copying ${name}: ${dest}"
  note "Embedded ${name}.framework -> ${dest}"

  if [[ "${CODE_SIGNING_ALLOWED:-NO}" == "YES" ]]; then
    command -v codesign >/dev/null 2>&1 || fatal 'codesign not available for signing frameworks'
    local identity="${EXPANDED_CODE_SIGN_IDENTITY:-}"
    if [[ -z "${identity}" && "${PLATFORM_NAME:-}" == "iphoneos" ]]; then
      fatal "No valid code-signing identity for iOS device: ${name}. Configure Signing & Capabilities."
    fi
    [[ -n "${identity}" ]] || identity='-'
    codesign --force --sign "${identity}" --timestamp=none "${dest}"
    codesign --verify "${dest}" || fatal "Signature verification failed for ${dest}"
    note "Signed ${name}.framework"
  fi
}

sdl_base="${repo}/lib/submodules/SDL/Xcode/SDL/build"
ttf_base="${repo}/lib/submodules/SDL_ttf/Xcode/build"

sdl=$(find_framework SDL3 "${KOTONOHA_SDL3_FRAMEWORK_PATH:-}" "${sdl_base}" SDL3)
ttf=$(find_framework SDL3_ttf "${KOTONOHA_SDL3_TTF_FRAMEWORK_PATH:-}" "${ttf_base}" SDL3_ttf)

install_framework SDL3 "${sdl}"
install_framework SDL3_ttf "${ttf}"
note "Both SDL frameworks and both asset directories are present for ${TARGET_NAME:-app}"
