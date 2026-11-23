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
    NSString *startScenario, NSString *rendererBackend,
    NSString *upscalerBackend, NSString *upscalerPreset,
    NSString *upscalerFactor);

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
    [cls presentWithOnLaunch:^(
             NSString *gamePath, NSString *engineSlug, NSString *saveProfileId,
             BOOL splitSavesByGame, NSString *customSaveRoot,
             NSString *assetOverlayPath, NSString *translationPath,
             NSString *subtitleStylePath, NSString *customDocumentsDir,
             NSString *ktrfPath, NSString *scriptsDir, NSString *startScenario,
             NSString *rendererBackend, NSString *upscalerBackend,
             NSString *upscalerPreset, NSString *upscalerFactor) {
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
      req.rendererBackend = rendererBackend.UTF8String ?: "";
      req.upscalerBackend = upscalerBackend.UTF8String ?: "";
      req.upscalerPreset = upscalerPreset.UTF8String ?: "";
      req.upscalerFactor = upscalerFactor.UTF8String ?: "";
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

void KotonohaIOSPresentError(const char *title, const char *message) {
  NSString *nsTitle = (title && title[0] != '\0')
                          ? [NSString stringWithUTF8String:title]
                          : @"Kotonoha Error";
  NSString *nsMessage = (message && message[0] != '\0')
                            ? [NSString stringWithUTF8String:message]
                            : @"Unknown error";
  runOnMain(^{
    UIWindow *keyWindow = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
      if (![scene isKindOfClass:[UIWindowScene class]])
        continue;
      UIWindowScene *windowScene = (UIWindowScene *)scene;
      for (UIWindow *window in windowScene.windows) {
        if (window.isKeyWindow) {
          keyWindow = window;
          break;
        }
      }
      if (!keyWindow)
        keyWindow = windowScene.windows.firstObject;
      if (keyWindow)
        break;
    }
    UIViewController *top = keyWindow.rootViewController;
    while (top.presentedViewController) {
      top = top.presentedViewController;
    }
    if (!top) {
      NSLog(@"[KotonohaLauncher] Error (%@): %@", nsTitle, nsMessage);
      return;
    }
    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:nsTitle
                         message:nsMessage
                  preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:nil]];
    [top presentViewController:alert animated:YES completion:nil];
  });
}

} // extern "C"

#endif
#endif
