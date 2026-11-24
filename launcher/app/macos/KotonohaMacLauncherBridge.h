// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <KotonohaLauncher/LauncherBridge.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KotonohaMacLaunchCallback)(
    const KotonohaGameLaunchRequest *request, void *userData);
typedef void (*KotonohaMacQuitCallback)(void *userData);

void KotonohaMacPresentNativeLauncher(KotonohaMacLaunchCallback onLaunch,
                                      KotonohaMacQuitCallback onQuit,
                                      void *userData);
void KotonohaMacDismissNativeLauncher(void);

#ifdef __cplusplus
}
#endif
