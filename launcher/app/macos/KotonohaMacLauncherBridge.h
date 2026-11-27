// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <KotonohaLauncher/LauncherBridge.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the PID of the detached game host, or 0 when it could not start.
typedef int (*KotonohaMacLaunchCallback)(
    const KotonohaGameLaunchRequest *request, void *userData);
typedef void (*KotonohaMacQuitCallback)(void *userData);

void KotonohaMacPresentNativeLauncher(KotonohaMacLaunchCallback onLaunch,
                                      KotonohaMacQuitCallback onQuit,
                                      void *userData);
void KotonohaMacDismissNativeLauncher(void);

#ifdef __cplusplus
}
#endif
