#!/usr/bin/env bash
# Compatibility shim for the Xcode project. DDLC and Katawa share the Ren'Py
# CPython bundling implementation from modules/vms/renpy.
set -euo pipefail
: "${SRCROOT:?Missing SRCROOT}"
repo="${KOTONOHA_ROOT:-${SRCROOT}/../..}"
exec /bin/bash "${repo}/modules/vms/renpy/tools/EmbedApplePython.sh"
