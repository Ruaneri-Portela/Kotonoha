#if defined(__APPLE__)
#import <TargetConditionals.h>

#if TARGET_OS_IOS || TARGET_OS_IPHONE

#import <Foundation/Foundation.h>

#include <string>

extern "C" void Kotonoha_MobileSetupShared(const char* dataDir,
	const char* platformName);

static std::string Kotonoha_iOS_GetDocumentsPath() {
	@autoreleasepool {
		NSArray* paths = NSSearchPathForDirectoriesInDomains(
			NSDocumentDirectory, NSUserDomainMask, YES);
		NSString* directory = [paths firstObject];
		return directory != nil
			? std::string([directory UTF8String])
			: std::string(".");
	}
}

extern "C" void Kotonoha_MobileSetup(void) {
	const std::string documentsPath = Kotonoha_iOS_GetDocumentsPath();
	Kotonoha_MobileSetupShared(documentsPath.c_str(), "iOS");
}

#endif
#endif
