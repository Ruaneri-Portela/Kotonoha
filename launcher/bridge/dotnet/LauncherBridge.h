// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// P/Invoke consumers compile their native side against the same shared C ABI
// as the other frontends. This forwarding header keeps the bridge directory
// self-contained while avoiding a second copy of the declarations.
#include "../../include/KotonohaLauncher/LauncherBridge.h"
