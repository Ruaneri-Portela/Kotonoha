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
# Produce the Apple desktop icon from the WebApp K on every build.
brand="${TARGET_BUILD_DIR}/KotonohaBrand"
python3 "${root}/tools/generate_brand_assets.py" --kind desktop --out "${brand}"
icon="${brand}/generic/icon.icns"
app="${TARGET_BUILD_DIR}/${WRAPPER_NAME}"
resources="${app}/Contents/Resources"
plist="${app}/Contents/Info.plist"

[[ -f "${icon}" ]] || fatal "Shared icon not found: ${icon}"
[[ -f "${plist}" ]] || fatal "Processed Info.plist not found: ${plist}"
[[ -d "${resources}" ]] || fatal "macOS resources folder not found: ${resources}"
[[ -x /usr/libexec/PlistBuddy ]] || fatal 'Apple PlistBuddy not available (build on macOS with Xcode)'

# The icon also remains inside the global assets/ directory. CFBundleIconFile
# resolves a sibling of Contents/Resources, so a top-level copy is required.
/usr/bin/ditto "${icon}" "${resources}/icon.icns"
# Keep engine assets in the bundle consistent with the application icon.
mkdir -p "${resources}/assets"
/usr/bin/ditto "${brand}/generic/icon.icns" "${resources}/assets/icon.icns"
/usr/bin/ditto "${brand}/generic/icon.ico" "${resources}/assets/icon.ico"
/usr/bin/ditto "${brand}/generic/icon.png" "${resources}/assets/brand-icon.png"
/usr/bin/ditto "${brand}/splash/SPLASH.PNG" "${resources}/assets/SPLASH.PNG"
if /usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "${plist}" >/dev/null 2>&1; then
  /usr/libexec/PlistBuddy -c 'Set :CFBundleIconFile icon.icns' "${plist}"
else
  /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string icon.icns' "${plist}"
fi
/usr/bin/plutil -lint "${plist}" >/dev/null
note "Configured ${WRAPPER_NAME} icon from assets/icon.icns (shared Info.plist source unchanged)"
