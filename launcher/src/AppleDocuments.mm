// Apple's Files-visible Documents directory is the only game library on iOS.
#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if TARGET_OS_IPHONE
#include <string>

extern "C" const char *KotonohaIOSDocumentsGamesPath() {
  static const std::string folder = [] {
    @autoreleasepool {
      NSArray<NSURL *> *directories =
          [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                 inDomains:NSUserDomainMask];
      NSURL *documents = directories.firstObject;
      if (!documents)
        return std::string{};
      NSURL *games = [documents URLByAppendingPathComponent:@"games"
                                                isDirectory:YES];
      NSError *error = nil;
      if (![[NSFileManager defaultManager] createDirectoryAtURL:games
                                    withIntermediateDirectories:YES
                                                     attributes:nil
                                                          error:&error]) {
        NSLog(@"[KotonohaLauncher] Failed to create Documents/games: %@",
              error);
      }
      return std::string(games.fileSystemRepresentation ?: "");
    }
  }();
  return folder.c_str();
}
#endif
#endif
