#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace {

void Require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

void Put(const fs::path &path, const std::string &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << data;
  Require(bool(out), "Cannot create " + path.string());
}

struct TemporaryDirectory {
  fs::path path =
      fs::temp_directory_path() /
      ("school-days-back-" + std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Require(fs::create_directory(path), "Cannot create temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

Kotonoha::EngineOptions
Options(const std::shared_ptr<SchoolDays::Assets> &assets) {
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  return options;
}

struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;

  Application(const std::shared_ptr<SchoolDays::Assets> &assets,
              const char *route, const fs::path &documents)
      : runtime(assets, route, documents), engine(runtime, Options(assets)) {
    runtime.SetState(Kotonoha::GameState::Gameplay);
  }
  ~Application() { runtime.shutdown(); }
};

void WriteScripts(const fs::path &root) {
  const auto script = root / "Script/English/00";
  const std::string ordinary =
      "[PrintText]=00:00:00\tFixture\tSynthetic.\t00:01:00;\n"
      "[SkipFRAME]=00:01:00;\n"
      "[Next]=00:02:00;\n";
  for (const char *name : {"00-00-A00", "00-00-A01", "00-00-A02"})
    Put(script / (std::string(name) + ".ENG.ORS"), ordinary);
}

void TestTwoStageBack(const char *route, const fs::path &root) {
  const auto documents = root / "documents";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);

  app.runtime.debugJumpToNode("00/00-00-A02");
  Require(app.runtime.debugNode() == "00/00-00-A02",
          "fixture did not enter A02");

  app.runtime.openPanel(2);
  Require(app.runtime.debugNode() == "00/00-00-A02",
          "first Back traversed instead of restarting A02");
  Require(*app.runtime.debugNodeSeconds() == 0.0,
          "first Back did not restart A02 at tick zero");

  app.runtime.openPanel(2);
  Require(app.runtime.debugNode() == "00/00-00-A01",
          "second Back did not traverse to A01");
  Require(*app.runtime.debugNodeSeconds() == 0.0,
          "A01 Back destination did not start at tick zero");

  // A Back-created predecessor is already in the retail start-navigation
  // state, so the next Back must continue walking rather than restart A01.
  app.runtime.openPanel(2);
  Require(app.runtime.debugNode() == "00/00-00-A00",
          "third Back restarted A01 instead of traversing to A00");
}

} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2, "Usage: SchoolDaysBackRuntimeTest route");
    TemporaryDirectory temporary;
    WriteScripts(temporary.path);
    TestTwoStageBack(argv[1], temporary.path);
    std::cout << "SchoolDaysBackRuntimeTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysBackRuntimeTest: " << error.what() << '\n';
    return 1;
  }
}
