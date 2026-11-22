// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>

#include <KotonohaLauncher/LauncherBridge.h>

typedef void (^KotonohaIOSLaunchBlock)(
    NSString *gamePath, NSString *engineSlug, NSString *saveProfileId,
    BOOL splitSavesByGame, NSString *customSaveRoot, NSString *assetOverlayPath,
    NSString *translationPath, NSString *subtitleStylePath,
    NSString *customDocumentsDir, NSString *ktrfPath, NSString *scriptsDir,
    NSString *startScenario);

@protocol KotonohaIOSLauncherPresenterProtocol <NSObject>
+ (void)presentWithOnLaunch:(KotonohaIOSLaunchBlock)onLaunch;
+ (void)dismiss;
+ (void)notifyOpenedURL:(NSString *)pathOrUrl;
@end

namespace {
Class<KotonohaIOSLauncherPresenterProtocol> presenterClass() {
  return (Class<KotonohaIOSLauncherPresenterProtocol>)NSClassFromString(
      @"KotonohaIOSLauncherPresenter");
}

void runOnMain(dispatch_block_t block) {
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_async(dispatch_get_main_queue(), block);
  }
}
} // namespace

extern "C" {

void KotonohaIOSPresentNativeLauncher(KotonohaIOSLaunchCallback onLaunch,
                                      KotonohaIOSQuitCallback /*onQuit*/,
                                      void *userData) {
  runOnMain(^{
    Class<KotonohaIOSLauncherPresenterProtocol> cls = presenterClass();
    if (!cls || ![cls respondsToSelector:@selector(presentWithOnLaunch:)]) {
      NSLog(@"[KotonohaLauncher] Native Swift presenter is unavailable or "
             "missing presentWithOnLaunch:");
      return;
    }
    [cls
        presentWithOnLaunch:^(
            NSString *gamePath, NSString *engineSlug, NSString *saveProfileId,
            BOOL splitSavesByGame, NSString *customSaveRoot,
            NSString *assetOverlayPath, NSString *translationPath,
            NSString *subtitleStylePath, NSString *customDocumentsDir,
            NSString *ktrfPath, NSString *scriptsDir, NSString *startScenario) {
          if (!onLaunch)
            return;
          KotonohaGameLaunchRequest req = {};
          req.gamePath = gamePath.UTF8String ?: "";
          req.engineSlug = engineSlug.UTF8String ?: "";
          req.saveProfileId = saveProfileId.UTF8String ?: "";
          req.splitSavesByGame = splitSavesByGame ? 1 : 0;
          req.customSaveRoot = customSaveRoot.UTF8String ?: "";
          req.assetOverlayPath = assetOverlayPath.UTF8String ?: "";
          req.translationPath = translationPath.UTF8String ?: "";
          req.subtitleStylePath = subtitleStylePath.UTF8String ?: "";
          req.customDocumentsDir = customDocumentsDir.UTF8String ?: "";
          req.ktrfPath = ktrfPath.UTF8String ?: "";
          req.scriptsDir = scriptsDir.UTF8String ?: "";
          req.startScenario = startScenario.UTF8String ?: "";
          onLaunch(&req, userData);
        }];
  });
}

void KotonohaIOSDismissNativeLauncher(void) {
  runOnMain(^{
    Class<KotonohaIOSLauncherPresenterProtocol> cls = presenterClass();
    if (cls && [cls respondsToSelector:@selector(dismiss)]) {
      [cls dismiss];
    }
  });
}

void KotonohaIOSNotifyOpenedURL(const char *pathOrUrl) {
  if (!pathOrUrl || pathOrUrl[0] == '\0')
    return;
  NSString *str = [NSString stringWithUTF8String:pathOrUrl];
  if (!str)
    return;
  runOnMain(^{
    Class<KotonohaIOSLauncherPresenterProtocol> cls = presenterClass();
    if (cls && [cls respondsToSelector:@selector(notifyOpenedURL:)]) {
      [cls notifyOpenedURL:str];
    }
  });
}

} // extern "C"

#endif
#endif
