#include <Kotonoha/core/BuildVersion.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/SystemInfo.hpp>

#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace
int main() {
  try {
    using namespace Kotonoha;
    const std::string version(BuildVersion::Full);
    require(!version.empty() && version.find(BuildVersion::Base) == 0,
            "Generated version does not contain CMake base version");
    require(version.find(BuildVersion::ShortCommit) != std::string::npos &&
                std::string(BuildVersion::Branch).size() > 0 &&
                std::string(BuildVersion::Commit).size() > 0,
            "Generated version missing Git metadata");
    {
      EngineOptions options;
      CommandLine cli("KotonohaTest", options);
      char program[] = "kotonoha";
      char flag[] = "--version";
      char *args[]{program, flag};
      cli.parse(2, args);
      require(cli.wantsHelp() && cli.wantsVersion(),
              "--version should terminate before SDL initialization");
      const auto printed = cli.help();
      require(printed.find(version) != std::string::npos &&
                  printed.find(BuildVersion::Branch) != std::string::npos &&
                  printed.find(BuildVersion::Commit) != std::string::npos,
              "Version output must report version, branch, commit");
    }
    {
      EngineOptions options;
      CommandLine cli("KotonohaTest", options);
      char program[] = "kotonoha";
      char flag[] = "-V";
      char *args[]{program, flag};
      cli.parse(2, args);
      require(cli.wantsVersion(), "-V alias not supported");
    }
    {
      EngineOptions options;
      CommandLine cli("KotonohaTest", options);
      char program[] = "kotonoha";
      char flag[] = "--help";
      char *args[]{program, flag};
      cli.parse(2, args);
      require(!cli.wantsVersion() && cli.wantsHelp() &&
                  cli.help().find("--version") != std::string::npos,
              "--help should continue to list all options");
    }
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD),
            "SDL dummy subsystems unavailable for system info test");
    const auto report = captureSystemInfo(nullptr, nullptr).text();
    require(report.find(version) != std::string::npos &&
                report.find("[CPU and memory]") != std::string::npos &&
                report.find("[Video and GPU]") != std::string::npos &&
                report.find("[Power]") != std::string::npos &&
                report.find("[Input]") != std::string::npos,
            "SDL diagnostic report omitted required sections");
    SDL_Quit();
    std::cout << "Build version, --version, and SDL diagnostics OK\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Build version test failed: " << e.what() << '\n';
    SDL_Quit();
    return 1;
  }
}
