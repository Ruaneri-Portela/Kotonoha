#if defined(ANDROID)

#include <SDL3/SDL_system.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

extern "C" void Kotonoha_MobileSetup(void)
{
    std::string appRoot = SDL_GetAndroidInternalStoragePath()
        ? SDL_GetAndroidInternalStoragePath()
        : ".";

    std::string dataDir = appRoot;
    std::string stdOutLog = dataDir + "/info.log";
    std::string stdErrLog = dataDir + "/error.log";

    auto agora = std::chrono::system_clock::now();
    std::time_t actualTime = std::chrono::system_clock::to_time_t(agora);
    std::tm* localTime = std::localtime(&actualTime);

    freopen(stdOutLog.c_str(), "a", stdout);
    freopen(stdErrLog.c_str(), "a", stderr);

    std::stringstream ss;
    ss << '\n'
       << "New execution: "
       << std::put_time(localTime, "%Y-%m-%d %H:%M:%S")
       << "\n-------------------------\n";

    std::cout << ss.str() << "STDOUT -> " << stdOutLog << std::endl;
    std::cerr << ss.str() << "STDERR -> " << stdErrLog << std::endl;
    std::cout << "Android appRoot -> " << appRoot << std::endl;

    if (chdir(appRoot.c_str()) != 0) {
        std::cerr << "Failed to change directory to " << appRoot << ": ";
        std::perror("");
    }
}
#endif
