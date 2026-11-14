#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/core/Launcher.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace Kotonoha;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

class TestLauncher final : public Launcher {
public:
  bool valid = true;
  int drawn = 0, saved = 0;
  using Launcher::Launcher;

protected:
  void drawGameOptions() override { ++drawn; }
  bool validate(std::string &error) override {
    if (!valid)
      error = "Missing game data";
    return valid;
  }
  void save() override { ++saved; }
};

class TestRuntime final : public Runtime {
public:
  TestRuntime() : Runtime("LauncherTests", "1", "launcher-tests") {}
};

class TestGameLauncher final : public GameLauncher {
public:
  TestGameLauncher(const std::string &settings, std::string directory)
      : GameLauncher(
            "Game", settings,
            {"game directory", PathKind::Folder, {}, "game-path.txt", {}}) {
    loadPath(std::move(directory));
  }

protected:
  bool checkPath(const std::string &path, std::string &info) override {
    const bool valid = std::filesystem::is_directory(path);
    info = valid ? "Directory found" : "Directory not found";
    return valid;
  }
};

void pathDiscovery(const std::filesystem::path &directory) {
  const auto invalid = directory / "stale-folder";
  const auto valid = directory / "valid-folder";
  std::filesystem::create_directory(invalid);
  std::filesystem::create_directory(valid);
  std::ofstream(valid / "required.dat").put('x');
  const auto validator = [](const std::string &path, std::string &) {
    return std::filesystem::is_regular_file(std::filesystem::path(path) /
                                            "required.dat");
  };
  require(GameLauncher::findGamePath("path-tests",
                                     {invalid.string(), valid.string()},
                                     validator) == valid.string(),
          "Invalid remembered paths are skipped before trying a valid default");
  require(GameLauncher::findGamePath("path-tests", {invalid.string()},
                                     [](const auto &, auto &) { return false; })
              .empty(),
          "No valid candidate returns an empty chooser instead of launching "
          "invalid data");
}

void gameLauncher(const std::filesystem::path &directory) {
  const auto previous = std::filesystem::current_path();
  struct RestoreDirectory {
    std::filesystem::path path;
    ~RestoreDirectory() { std::filesystem::current_path(path); }
  } restore{previous};
  std::filesystem::current_path(directory);
  const auto root = directory / "game-launcher";
  std::filesystem::create_directory(root);
  const auto settings = (root / "settings.json").string();
  {
    TestGameLauncher launcher(settings, (root / "missing").string());
    require(!launcher.pathValid() && !launcher.launch(),
            "The original single-path launcher rejects invalid data");
    launcher.setPath(root.string());
    launcher.open();
    launcher.iterate();
    require(launcher.pathValid() && launcher.launch(),
            "The original single-path launcher still starts");
    launcher.close();
  }
  TestGameLauncher reopened(settings, "");
  require(GameLauncher::rememberedPath(settings, "game-path.txt") ==
              "game-launcher",
          "The launcher persists a portable name rather than an absolute path");
  require(std::filesystem::equivalent(reopened.path(), root) &&
              reopened.pathValid(),
          "The original single-path launcher remembers its path");

  const auto external = directory.parent_path() /
                        (directory.filename().string() + "-external-game");
  struct RemoveExternal {
    std::filesystem::path path;
    ~RemoveExternal() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } cleanup{external};
  std::filesystem::create_directory(external);
  const auto externalSettings = (directory / "external-settings.json").string();
  {
    TestGameLauncher selected(externalSettings, external.string());
    selected.open();
    selected.iterate();
    require(selected.pathValid() && selected.launch(),
            "External game root is valid");
    selected.close();
  }
  require(GameLauncher::rememberedPath(externalSettings, "game-path.txt") ==
              external.string(),
          "External game roots keep their full path");
  TestGameLauncher externalReopened(externalSettings, "");
  require(externalReopened.path() == external.string() &&
              externalReopened.pathValid(),
          "External game root reopens after saving");
}

void launcher(const std::filesystem::path &directory) {
  const auto path = (directory / "settings.json").string();
  const auto transPath = (directory / "translation.json").string();
  std::ofstream(transPath) << "{}\n";
  {
    TestLauncher launcher("Test", path);
    require(launcher.state() == Launcher::State::Running &&
                !launcher.settings().upscaling.enabled(),
            "Defaults without a settings file");
    require(launcher.getAudioChannels().empty(),
            "A generic launcher does not invent game audio channels");
    launcher.settings().channelVolumes = {{"other-game", .2f}, {"voice", .6f}};
    launcher.setAudioChannels({"voice", "bgm", "voice"});
    require(launcher.getAudioChannels() ==
                    std::vector<std::string>{"voice", "bgm"} &&
                launcher.settings().channelVolumes.at("voice") == .6f &&
                launcher.settings().channelVolumes.at("other-game") == .2f,
            "Injected channels preserve order and saved gains without exposing "
            "stale channels");
    bool rejected = false;
    try {
      launcher.setAudioChannels({"se", "invalid\nchannel"});
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected && launcher.getAudioChannels() ==
                            std::vector<std::string>{"voice", "bgm"},
            "Invalid injection leaves the existing channel list intact");
    launcher.setAudioChannels({"se"});
    require(launcher.getAudioChannels() == std::vector<std::string>{"se"},
            "A game replaces the visible channel list instead of accumulating "
            "every mixer");
    launcher.open();
    for (int i = 0; i < 3; ++i)
      launcher.iterate();
    require(launcher.drawn == 3, "Game options are drawn in the first tab");

    launcher.settings().upscaling.backend = UpscaleBackend::MetalFX;
    launcher.settings().upscaling.factor = 2.5f;
    launcher.settings().width = 1600;
    launcher.settings().height = 900;
    launcher.settings().translationPath = transPath;
    launcher.settings().fontPath = "font.ttf";
    launcher.valid = false;
    require(!launcher.launch() &&
                launcher.state() == Launcher::State::Running &&
                launcher.getStatus() == "Missing game data" &&
                !std::filesystem::exists(path) && launcher.saved == 0,
            "Game validation blocks the launch");
    launcher.settings().upscaling.factor = 9;
    launcher.valid = true;
    require(!launcher.launch() && !std::filesystem::exists(path),
            "Invalid engine settings block the launch");
    launcher.settings().upscaling.factor = 2.5f;
    require(launcher.launch() && launcher.state() == Launcher::State::Launch &&
                launcher.saved == 1,
            "Launch saves and finishes");
    launcher.close();
    require(!launcher.isOpen(), "Close releases the window");
  }
  const auto saved = EngineSettings::load(path);
  require(saved.upscaling.factor == 2.5f &&
              saved.upscaling.backend == UpscaleBackend::MetalFX &&
              saved.width == 1600 &&
              saved.translationPath == "translation.json" &&
              saved.fontPath == "font.ttf",
          "The engine settings file holds the launcher choices");

  {
    TestLauncher reopened("Test", path);
    require(reopened.settings().upscaling.factor == 2.5f,
            "Launcher reloads saved settings");
    reopened.open();
    reopened.iterate();
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    reopened.event(quit);
    require(reopened.state() == Launcher::State::Quit, "Quit event");
  }

  // The engine starts after the launcher, with its file. MetalFX is not
  // available on the software renderer, so it falls back to Auto (Linear).
  TestRuntime runtime;
  EngineOptions options;
  options.settingsPath = path;
  options.rendererOverride = "software";
  std::filesystem::create_directories(directory / "assets/fonts");
  std::filesystem::copy_file(KOTONOHA_TEST_FONT,
                             directory / "assets/fonts/font.ttf");
  {
    Engine engine(runtime, options);
    require(engine.GetSettings().upscaling.factor == 2.5f &&
                engine.GetSettings().upscaling.backend ==
                    UpscaleBackend::Auto &&
                engine.GetUpscaler().activeBackend() == UpscaleBackend::Linear,
            "Engine applies the launcher upscaling with a safe fallback");
  }

  std::ofstream(path) << "{\"factor\":0.5}";
  TestLauncher broken("Test", path);
  require(!broken.getStatus().empty() &&
              broken.settings().upscaling.factor == 1,
          "Invalid file falls back to defaults with a message");
}

} // namespace

int main() {
  const auto previous = std::filesystem::current_path();
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("kotonoha-launcher-" + std::to_string(SDL_GetPerformanceCounter()));
  try {
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    launcher(directory);
    pathDiscovery(directory);
    gameLauncher(directory);
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
    return 1;
  }
  std::filesystem::current_path(previous);
  std::filesystem::remove_all(directory);
  std::cout << "Launcher tests passed\n";
  return 0;
}
