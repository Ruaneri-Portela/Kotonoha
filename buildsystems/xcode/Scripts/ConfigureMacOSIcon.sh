#!/usr/bin/env bash
# Install the shared macOS app icon without changing the common source Info.plist.
set -euo pipefail

note() { printf '[Kotonoha/Icon] %s\n' "$*"; }
fatal() { printf 'error: [Kotonoha/Icon] %s\n' "$*" >&2; exit 1; }

: "${SRCROOT:?Missing SRCROOT}"
: "${TARGET_BUILD_DIR:?Missing TARGET_BUILD_DIR}"
: "${WRAPPER_NAME:?Missing WRAPPER_NAME}"

[[ "${PLATFORM_NAME:-}" == "macosx" ]] || fatal "ConfigureMacOSIcon.sh is macOS-only (PLATFORM_NAME=${PLATFORM_NAME:-unset})"

root="${KOTONOHA_ROOT:-${SRCROOT}/../..}"
# design/generated/ is the versioned source of truth for the visual identity.
# Regenerate it in place so the artwork stays visible in the repository; never
# write it to TARGET_BUILD_DIR or treat it as a generic engine asset.
brand="${root}/design/generated"
python3 "${root}/tools/generate_brand_assets.py" --kind desktop --out "${brand}"
icon="${brand}/generic/icon.icns"
app="${TARGET_BUILD_DIR}/${WRAPPER_NAME}"
resources="${app}/Contents/Resources"
plist="${app}/Contents/Info.plist"

[[ -f "${icon}" ]] || fatal "Shared icon not found: ${icon}"
[[ -f "${plist}" ]] || fatal "Processed Info.plist not found: ${plist}"
[[ -d "${resources}" ]] || fatal "macOS resources folder not found: ${resources}"
[[ -x /usr/libexec/PlistBuddy ]] || fatal 'Apple PlistBuddy not available (build on macOS with Xcode)'

# CFBundleIconFile resolves a sibling of Contents/Resources, so this is the
# ONLY place the macOS app icon is installed. Icons are never copied into
# assets/ as generic game/engine assets.
/usr/bin/ditto "${icon}" "${resources}/icon.icns"
# The universal splash is the single engine asset imported from design/.
mkdir -p "${resources}/assets"
/usr/bin/ditto "${brand}/splash/SPLASH.PNG" "${resources}/assets/SPLASH.PNG"
if /usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "${plist}" >/dev/null 2>&1; then
  /usr/libexec/PlistBuddy -c 'Set :CFBundleIconFile icon.icns' "${plist}"
else
  /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string icon.icns' "${plist}"
fi
/usr/bin/plutil -lint "${plist}" >/dev/null
note "Configured ${WRAPPER_NAME} icon from design/generated/generic/icon.icns (shared Info.plist source unchanged)"
