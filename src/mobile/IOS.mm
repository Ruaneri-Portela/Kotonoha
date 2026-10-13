#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <string>


extern "C" void Kotonoha_MobileSetupShared(
    const char* resourcesDir,
    const char* documentsDir,
    const char* platformName,
    bool redirect
);


static std::string Kotonoha_Apple_GetBundleResourcesPath()
{
    @autoreleasepool {
        NSBundle* bundle = [NSBundle mainBundle];
        NSString* resourcePath = [bundle resourcePath];

        return resourcePath != nil
            ? std::string([resourcePath UTF8String])
            : std::string(".");
    }
}


static std::string Kotonoha_Apple_GetDocumentsPath()
{
    @autoreleasepool {
        NSFileManager* fileManager =
            [NSFileManager defaultManager];

        NSURL* documentsUrl = [fileManager URLForDirectory:NSDocumentDirectory
                                                  inDomain:NSUserDomainMask
                                         appropriateForURL:nil
                                                    create:YES
                                                     error:nil];

        return documentsUrl != nil
            ? std::string([[documentsUrl path] UTF8String])
            : std::string(".");
    }
}


extern "C" void Kotonoha_MobileSetup(void)
{
    const std::string resourcesPath =
        Kotonoha_Apple_GetBundleResourcesPath();

    const std::string documentsPath =
        Kotonoha_Apple_GetDocumentsPath();

#if TARGET_OS_OSX
    Kotonoha_MobileSetupShared(
        resourcesPath.c_str(),
        documentsPath.c_str(),
        "macOS",
        false
    );
#else
    Kotonoha_MobileSetupShared(
        resourcesPath.c_str(),
        documentsPath.c_str(),
        "iOS",
        true
    );
#endif
}

#endif