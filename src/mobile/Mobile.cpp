#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace Kotonoha::Mobile {

static std::string GetSafeDirectory(const char *directory) {
  return directory != nullptr && *directory != '\0' ? std::string(directory)
                                                    : std::string(".");
}

static bool SetWorkingDirectory(const char *resourcesDir) {
  const std::string directory = GetSafeDirectory(resourcesDir);

  std::error_code error;
  std::filesystem::current_path(directory, error);

  if (!error) {
    return true;
  }

  std::cerr << "Failed to change working directory to " << directory << ": "
            << error.message() << '\n';

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
  SDL_Time sdlTime = 0;
  SDL_DateTime localTime{};
  const bool hasLocalTime =
      SDL_GetCurrentTime(&sdlTime) && SDL_TimeToDateTime(sdlTime, &localTime, true);

  std::stringstream header;

  header << '\n' << "New execution: ";

  if (hasLocalTime) {
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d",
                  localTime.year, localTime.month, localTime.day,
                  localTime.hour, localTime.minute, localTime.second);
    header << stamp;
  } else {
    header << "time unavailable";
  }

  header << "\n-------------------------\n";

  return header.str();
}

static std::string gameDataDirectory;

} // namespace Kotonoha::Mobile

extern "C" const char *KotonohaMobileGameDataDir(void) {
  return Kotonoha::Mobile::gameDataDirectory.empty()
             ? nullptr
             : Kotonoha::Mobile::gameDataDirectory.c_str();
}

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

  if (redirect) {
    Kotonoha::Mobile::gameDataDirectory = documentsDirectory;
  }

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