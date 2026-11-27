// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if !TARGET_OS_IPHONE
#include "KotonohaMacLauncherBridge.h"

typedef NSInteger (^KotonohaMacLaunchBlock)(
    NSString *gamePath, NSString *engineSlug, NSString *saveProfileId,
    BOOL splitSavesByGame, NSString *customSaveRoot, NSString *assetOverlayPath,
    NSString *translationPath, NSString *subtitleStylePath,
    NSString *customDocumentsDir, NSString *ktrfPath, NSString *scriptsDir,
    NSString *startScenario, NSString *rendererBackend,
    NSString *upscalerBackend, NSString *upscalerPreset,
    NSString *upscalerFactor);

@protocol KotonohaMacLauncherPresenterProtocol <NSObject>
+ (void)presentWithOnLaunch:(KotonohaMacLaunchBlock)onLaunch
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

extern "C" void
KotonohaMacPresentNativeLauncher(KotonohaMacLaunchCallback onLaunch,
                                 KotonohaMacQuitCallback onQuit,
                                 void *userData) {
  onMain(^{
    Class<KotonohaMacLauncherPresenterProtocol> cls = presenterClass();
    if (!cls ||
        ![cls respondsToSelector:@selector(presentWithOnLaunch:onQuit:)]) {
      NSLog(@"[KotonohaLauncher] macOS SwiftUI presenter is unavailable");
      return;
    }
    [cls
        presentWithOnLaunch:^NSInteger(
            NSString *gamePath, NSString *engineSlug, NSString *saveProfileId,
            BOOL splitSavesByGame, NSString *customSaveRoot,
            NSString *assetOverlayPath, NSString *translationPath,
            NSString *subtitleStylePath, NSString *customDocumentsDir,
            NSString *ktrfPath, NSString *scriptsDir, NSString *startScenario,
            NSString *rendererBackend, NSString *upscalerBackend,
            NSString *upscalerPreset, NSString *upscalerFactor) {
          if (!onLaunch)
            return (NSInteger)0;
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
          req.rendererBackend = rendererBackend.UTF8String ?: "";
          req.upscalerBackend = upscalerBackend.UTF8String ?: "";
          req.upscalerPreset = upscalerPreset.UTF8String ?: "";
          req.upscalerFactor = upscalerFactor.UTF8String ?: "";
          return (NSInteger)onLaunch(&req, userData);
        }
        onQuit:^{
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
