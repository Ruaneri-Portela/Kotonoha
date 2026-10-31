#include <Kotonoha/core/Engine.hpp>
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
      : GameLauncher("Game", settings,
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

void gameLauncher(const std::filesystem::path &directory) {
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
  require(reopened.path() == root.string() && reopened.pathValid(),
          "The original single-path launcher remembers its path");
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
              saved.translationPath == transPath &&
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
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("kotonoha-launcher-" + std::to_string(SDL_GetPerformanceCounter()));
  try {
    std::filesystem::create_directories(directory);
    launcher(directory);
    gameLauncher(directory);
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
  std::filesystem::remove_all(directory);
  std::cout << "Launcher tests passed\n";
  return 0;
}
