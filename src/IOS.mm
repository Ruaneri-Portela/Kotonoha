#if defined(__APPLE__) && defined(TARGET_OS_IOS)

#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static std::string Kotonoha_iOS_GetDocumentsPath()
{
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *dir = [paths firstObject];
        return dir ? std::string([dir UTF8String]) : std::string(".");
    }
}

extern "C" void Kotonoha_MobileSetup(void)
{
    std::string dataDir = Kotonoha_iOS_GetDocumentsPath();
    std::string stdOutLog = dataDir + "/info.log";
    std::string stdErrLog = dataDir + "/error.log";

    auto now = std::chrono::system_clock::now();
    std::time_t actualTime = std::chrono::system_clock::to_time_t(now);
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
    std::cout << "iOS Documents -> " << dataDir << std::endl;

    if (chdir(dataDir.c_str()) != 0) {
        std::cerr << "Failed to change directory to " << dataDir << ": ";
        std::perror("");
    }
}
#endif
