#if defined(ANDROID)

#include <SDL3/SDL_system.h>

#include <string>

extern "C" void Kotonoha_MobileSetupShared(const char* dataDir,
	const char* platformName);

extern "C" void Kotonoha_MobileSetup(void) {
	const char* internalStoragePath = SDL_GetAndroidInternalStoragePath();
	const std::string appRoot =
		internalStoragePath != nullptr ? internalStoragePath : ".";

	Kotonoha_MobileSetupShared(appRoot.c_str(), "Android");
}

#endif
