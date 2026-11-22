// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if !TARGET_OS_IPHONE
#include "KotonohaMacLauncherBridge.h"

@protocol KotonohaMacLauncherPresenterProtocol <NSObject>
+ (void)presentWithOnLaunch:(void (^)(NSString *))onLaunch
                    onQuit:(void (^)(void))onQuit;
+ (void)dismiss;
@end

static Class<KotonohaMacLauncherPresenterProtocol> presenterClass() {
  return (Class<KotonohaMacLauncherPresenterProtocol>)NSClassFromString(
      @"KotonohaMacLauncherPresenter");
}

static void onMain(dispatch_block_t block) {
  if ([NSThread isMainThread])
    block();
  else
    dispatch_async(dispatch_get_main_queue(), block);
}

extern "C" void KotonohaMacPresentNativeLauncher(
    KotonohaMacLaunchCallback onLaunch, KotonohaMacQuitCallback onQuit,
    void *userData) {
  onMain(^{
    Class<KotonohaMacLauncherPresenterProtocol> cls = presenterClass();
    if (!cls || ![cls respondsToSelector:@selector(presentWithOnLaunch:onQuit:)]) {
      NSLog(@"[KotonohaLauncher] macOS SwiftUI presenter is unavailable");
      return;
    }
    [cls presentWithOnLaunch:^(NSString *path) {
      if (onLaunch)
        onLaunch(path.UTF8String ?: "", userData);
    } onQuit:^{
      if (onQuit)
        onQuit(userData);
    }];
  });
}

extern "C" void KotonohaMacDismissNativeLauncher(void) {
  onMain(^{
    Class<KotonohaMacLauncherPresenterProtocol> cls = presenterClass();
    if (cls && [cls respondsToSelector:@selector(dismiss)])
      [cls dismiss];
  });
}
#endif
