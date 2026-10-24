#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#include <string>

extern "C" void KotonohaMobileSetupShared(const char *resourcesDir,
                                          const char *documentsDir,
                                          const char *platformName,
                                          bool redirect);

namespace Kotonoha::Mobile {

static std::string GetBundleResourcesPath() {
  @autoreleasepool {
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *resourcePath = [bundle resourcePath];

    return resourcePath != nil ? std::string([resourcePath UTF8String])
                               : std::string(".");
  }
}

static std::string GetDocumentsPath() {
  @autoreleasepool {
    NSFileManager *fileManager = [NSFileManager defaultManager];

    NSURL *documentsUrl = [fileManager URLForDirectory:NSDocumentDirectory
                                              inDomain:NSUserDomainMask
                                     appropriateForURL:nil
                                                create:YES
                                                 error:nil];

    return documentsUrl != nil ? std::string([[documentsUrl path] UTF8String])
                               : std::string(".");
  }
}

} // namespace Kotonoha::Mobile

extern "C" void KotonohaMobileSetup(void) {
  const std::string resourcesPath = Kotonoha::Mobile::GetBundleResourcesPath();

  const std::string documentsPath = Kotonoha::Mobile::GetDocumentsPath();

#if TARGET_OS_OSX
  KotonohaMobileSetupShared(resourcesPath.c_str(), documentsPath.c_str(),
                            "macOS", false);
#else
  KotonohaMobileSetupShared(resourcesPath.c_str(), documentsPath.c_str(), "iOS",
                            true);
#endif
}

#endif