#include "SchoolDaysAssets.hpp"
#include "SchoolDaysLauncher.hpp"
#include <Kotonoha/script/KtrfScript.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

void run(const std::vector<std::string> &arguments) {
  std::vector<const char *> argv;
  for (const auto &argument : arguments)
    argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  SDL_Process *process = SDL_CreateProcess(argv.data(), false);
  require(process != nullptr,
          std::string("Cannot start School Days: ") + SDL_GetError());
  int exitCode = -1;
  const bool waited = SDL_WaitProcess(process, true, &exitCode);
  SDL_DestroyProcess(process);
  require(waited && exitCode == 0, "School Days process failed");
}

struct TemporaryDirectory {
  fs::path path =
      fs::temp_directory_path() /
      ("school-days-launcher-" + std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    require(fs::create_directory(path), "Cannot create test directory");
  }
  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 4,
            "Usage: SchoolDaysLauncherTest route fixtures executable");
    const fs::path original = fs::absolute(argv[1]);
    const fs::path fixtures = fs::absolute(argv[2]);
    TemporaryDirectory temporary;
    const auto workspace = temporary.path / "workspace";
    const auto gameAssets = workspace / "games/school-days/game-assets";
    fs::create_directories(gameAssets / "Script/English");
    fs::copy_file(original, gameAssets / "school-days-hq.ktnroute");
    const auto previousDirectory = fs::current_path();
    fs::current_path(workspace);
    const auto discoveredGame = fs::absolute(SchoolDays::Assets::defaultDirectory());
    const auto discoveredRoute = fs::absolute(SchoolDays::Assets::findDefaultRoute());
    fs::current_path(previousDirectory);
    require(fs::is_directory(discoveredGame) && fs::is_regular_file(discoveredRoute) &&
                fs::equivalent(discoveredGame, gameAssets) &&
                fs::equivalent(discoveredRoute, gameAssets / "school-days-hq.ktnroute"),
            "Source game-assets directory and route were not discovered: " +
                discoveredGame.string() + ", " + discoveredRoute.string());

    const auto custom = temporary.path / "custom route.ktnroute";
    fs::copy_file(fixtures / "edited.ktnroute", custom);
    require(read(custom) != read(original),
            "Custom route fixture must differ from the default");
    const auto settings = (temporary.path / "settings.json").string();
    const auto invalid = (temporary.path / "invalid.ktnroute").string();

    Kotonoha::ScriptNode end;
    end.id = 0;
    end.type = Kotonoha::NodeType::End;
    Kotonoha::KtrfScript{{end}, Kotonoha::ScriptMode::Standard, 0}.dump(
        invalid);

    {
      SchoolDays::Launcher launcher(
          settings, fixtures.string(),
          (temporary.path / "missing.ktnroute").string());
      require(launcher.pathValid() && !launcher.routeValid(),
              "Missing route was accepted");
      require(!launcher.launch() && !fs::exists(settings),
              "Missing route must block Play");
      launcher.setRoutePath(invalid);
      require(!launcher.routeValid() && !launcher.launch(),
              "Other KTRF profiles must be rejected");
      launcher.setRoutePath(custom.string());
      require(launcher.routeValid(), launcher.routeInfo());
      launcher.open();
      launcher.iterate();
      launcher.iterate();
      require(launcher.pathValid() && launcher.routeValid(),
              "Launcher frames must keep the selected game and route");
      require(launcher.launch() &&
                  launcher.state() == Kotonoha::Launcher::State::Launch,
              "Valid route and game directory must launch");
      launcher.close();
    }

    SchoolDays::Launcher reopened(settings, "", "");
    require(reopened.path() == fixtures.string() && reopened.pathValid(),
            "The game directory was not remembered");
    require(reopened.routePath() == custom.string() && reopened.routeValid(),
            "The selected route was not remembered");
    SchoolDays::Launcher explicitRoute(settings, fixtures.string(),
                                       original.string());
    require(explicitRoute.routePath() == original.string() &&
                explicitRoute.routeValid(),
            "An explicit route must override the remembered route");

    const auto documents = (temporary.path / "documents").string();
    const auto rememberedDump =
        (temporary.path / "remembered.ktnroute").string();
    const auto explicitDump = (temporary.path / "explicit.ktnroute").string();
    const std::vector<std::string> common = {argv[3],
                                             "--school-days",
                                             fixtures.string(),
                                             "--config",
                                             settings,
                                             "--documents",
                                             documents,
                                             "--renderer",
                                             "software",
                                             "--frames",
                                             "4"};
    auto command = common;
    command.insert(command.end(), {"--dump-ktrf", rememberedDump});
    run(command);
    require(read(rememberedDump) == read(custom),
            "Starting without the launcher did not use the remembered route");
    command = common;
    command.insert(command.end(),
                   {"--ktrf", original.string(), "--dump-ktrf", explicitDump});
    run(command);
    require(read(explicitDump) == read(original),
            "The command-line route did not override the remembered route");
    const auto defaultDump = (temporary.path / "default.ktnroute").string();
    command = {argv[3],       "--school-days", fixtures.string(),
               "--no-config", "--documents",   documents,
               "--renderer",  "software",      "--frames",
               "4",           "--dump-ktrf",   defaultDump};
    run(command);
    require(read(defaultDump) == read(original),
            "Disabling configuration must use the bundled default route");

    std::ofstream(custom, std::ios::binary | std::ios::trunc) << "corrupt";
    require(!reopened.launch() &&
                reopened.getStatus().find("Invalid School Days route") !=
                    std::string::npos,
            "A route changed after selection must be checked again");
    reopened.setRoutePath("");
    require(!reopened.routeValid(), "An empty route must be rejected");
    std::cout << "School Days launcher route selection passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysLauncherTest: " << error.what() << '\n';
    return 1;
  }
}
