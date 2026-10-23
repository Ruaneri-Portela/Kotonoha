#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace Kotonoha::Mobile {

static std::string GetSafeDirectory(const char *directory) {
  return directory != nullptr && *directory != '\0' ? std::string(directory)
                                                    : std::string(".");
}

static bool SetWorkingDirectory(const char *resourcesDir) {
  const std::string directory = GetSafeDirectory(resourcesDir);

#ifdef _WIN32
  const int result = _chdir(directory.c_str());
#else
  const int result = chdir(directory.c_str());
#endif

  if (result == 0) {
    return true;
  }

  std::cerr << "Failed to change working directory to " << directory << ": ";

  std::perror("");

  return false;
}

static bool RedirectLogs(const char *documentsDir) {
  const std::string directory = GetSafeDirectory(documentsDir);

  const std::string stdoutPath = directory + "/info.log";

  const std::string stderrPath = directory + "/error.log";

  const bool stdoutRedirected =
      std::freopen(stdoutPath.c_str(), "a", stdout) != nullptr;

  const bool stderrRedirected =
      std::freopen(stderrPath.c_str(), "a", stderr) != nullptr;

  if (!stdoutRedirected || !stderrRedirected) {
    std::cerr << "Failed to redirect logs "
              << "(stdout: " << (stdoutRedirected ? "ok" : "failed")
              << ", stderr: " << (stderrRedirected ? "ok" : "failed") << ").\n";

    return false;
  }

  return true;
}

static std::string CreateLogHeader() {
  const auto now = std::chrono::system_clock::now();

  const std::time_t actualTime = std::chrono::system_clock::to_time_t(now);

  std::tm localTime{};

#ifdef _WIN32
  const bool hasLocalTime = localtime_s(&localTime, &actualTime) == 0;
#else
  const bool hasLocalTime = localtime_r(&actualTime, &localTime) != nullptr;
#endif

  std::stringstream header;

  header << '\n' << "New execution: ";

  if (hasLocalTime) {
    header << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
  } else {
    header << "time unavailable";
  }

  header << "\n-------------------------\n";

  return header.str();
}

} // namespace Kotonoha::Mobile

extern "C" void KotonohaMobileSetupShared(const char *resourcesDir,
                                          const char *documentsDir,
                                          const char *platformName,
                                          bool redirect) {
  const std::string resourcesDirectory =
      Kotonoha::Mobile::GetSafeDirectory(resourcesDir);

  const std::string documentsDirectory =
      Kotonoha::Mobile::GetSafeDirectory(documentsDir);

  const std::string platform = platformName != nullptr && *platformName != '\0'
                                   ? platformName
                                   : "Mobile";

  // Assets são acessados relativamente a Resources:
  // assets/font.ttf
  // assets/shaders/upscale.metallib
  const bool directoryChanged =
      Kotonoha::Mobile::SetWorkingDirectory(resourcesDirectory.c_str());

  // Mantém stdout/stderr no terminal, console do Xcode ou logcat.
  if (!redirect) {
    std::cout << platform << " resources directory -> " << resourcesDirectory
              << '\n';

    std::cout << platform << " documents directory -> " << documentsDirectory
              << '\n';

    std::cout << platform << " working directory changed -> "
              << (directoryChanged ? "yes" : "no") << std::endl;

    return;
  }

  const std::string stdoutPath = documentsDirectory + "/info.log";

  const std::string stderrPath = documentsDirectory + "/error.log";

  const bool logsRedirected =
      Kotonoha::Mobile::RedirectLogs(documentsDirectory.c_str());

  const std::string header = Kotonoha::Mobile::CreateLogHeader();

  std::cout << header << "STDOUT -> " << stdoutPath << '\n'
            << platform << " resources directory -> " << resourcesDirectory
            << '\n'
            << platform << " documents directory -> " << documentsDirectory
            << '\n'
            << platform << " working directory changed -> "
            << (directoryChanged ? "yes" : "no") << '\n'
            << platform << " logs redirected -> "
            << (logsRedirected ? "yes" : "no") << std::endl;

  std::cerr << header << "STDERR -> " << stderrPath << '\n'
            << platform << " resources directory -> " << resourcesDirectory
            << '\n'
            << platform << " documents directory -> " << documentsDirectory
            << '\n'
            << platform << " working directory changed -> "
            << (directoryChanged ? "yes" : "no") << '\n'
            << platform << " logs redirected -> "
            << (logsRedirected ? "yes" : "no") << std::endl;
}