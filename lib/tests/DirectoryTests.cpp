#include <Kotonoha/core/AssetOverlay.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Kotonoha;
namespace fs = std::filesystem;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool contains(const std::vector<fs::path> &paths, const fs::path &path) {
  return std::find(paths.begin(), paths.end(), path) != paths.end();
}

struct Fixture {
  fs::path previous = fs::current_path();
  fs::path root;
  Fixture() {
    root =
        fs::temp_directory_path() /
        ("kotonoha-directories-" + std::to_string(SDL_GetPerformanceCounter()));
    fs::create_directories(root);
    // temp_directory_path() pode expor o symlink /var do macOS; canonicamos
    // para que os caminhos resolvidos pelo engine (via current_path) coincidam.
    root = fs::canonical(root);
  }
  ~Fixture() {
    std::error_code error;
    fs::current_path(previous, error);
    fs::remove_all(root, error);
  }
};

void layouts(const fs::path &root) {
  StorageLocations locations;
  locations.packageIdentifier = "com.example.game";
  locations.resources = root / "App.app/Contents/Resources";
  locations.workingDirectory = root / "source";
  for (const auto &base :
       {root / "AppData/Roaming", root / "Library/Application Support",
        root / ".local/share"}) {
    locations.userData = base;
    auto layout = directoryLayout(locations, "amagami");
    require(layout.saves == base / "Kotonoha/com.example.game",
            "Every desktop uses Kotonoha/package, never the game slug");
    require(layout.engineAssets.front() == locations.resources / "assets",
            "Bundle engine assets take precedence over the working directory");
    require(layout.gameAssets.front() == locations.resources / "game-assets",
            "Bundle game assets have their own root");
    require(directoryLayout(locations).saves == layout.saves,
            "An omitted source-game slug does not change save identity");
    require(contains(layout.gameAssets,
                     locations.workingDirectory / "games/amagami/game-assets"),
            "Source-tree game assets");
  }

  locations.platform = StoragePlatform::Android;
  locations.resources = root / "data/data/com.example.game/files";
  locations.privateGameAssets = root / "data/data/com.example.game/game_assets";
  locations.obb = root / "Android/obb/com.example.game";
  locations.userData = root / "Android/data/com.example.game/files";
  const auto android = directoryLayout(locations);
  require(android.engineAssets ==
              std::vector<fs::path>{locations.resources / "assets"},
          "Android engine assets only use private files/assets");
  require(contains(android.gameAssets, locations.privateGameAssets) &&
              contains(android.gameAssets, locations.obb) &&
              contains(android.gameAssets, locations.obb / "game_assets"),
          "Android accepts private game_assets and OBB data");
  require(android.saves == locations.userData / "Kotonoha/com.example.game",
          "Android saves use external app data, not internal APK resources");
  for (const auto &base :
       {locations.resources, locations.privateGameAssets.parent_path(),
        locations.userData, locations.obb})
    require(
        contains(referenceCandidates(android, "overlay2"), base / "overlay2"),
        "Named overlays can live in internal data, files, external data or "
        "OBB");
  require(!contains(android.references, locations.workingDirectory),
          "Mobile lookups do not depend on process working directory");

  locations.platform = StoragePlatform::IOS;
  locations.resources = root / "Application/App.app";
  locations.userData = root / "Application/Documents";
  locations.obb.clear();
  locations.privateGameAssets.clear();
  auto ios = directoryLayout(locations);
  require(ios.saves == locations.userData / "Kotonoha/com.example.game",
          "iOS saves are namespaced inside Files-accessible Documents");
  require(contains(ios.engineAssets, locations.resources / "assets") &&
              contains(ios.engineAssets, locations.userData / "assets") &&
              contains(ios.gameAssets, locations.userData / "game_assets"),
          "iOS supports both bundled and Documents assets");
  locations.packageIdentifier.clear();
  require(directoryLayout(locations).saves.empty(),
          "Missing metadata cannot silently create a game-name save directory");
}

void relocation(Fixture &fixture) {
  fs::current_path(fixture.root);
  fs::create_directories("overlay2");
  std::ofstream("overlay2/scene.txt") << "translated";
  EngineSettings settings;
  settings.assetOverlays.emplace("overlay2",
                                 (fixture.root / "overlay2").string());
  settings.assetOverlayName = "overlay2";
  settings.translationPath = (fixture.root / "translation.json").string();
  settings.fontPath = (fixture.root / "custom.ttf").string();
  settings.vnFontPath = (fixture.root / "dialogue.ttf").string();
  settings.upscaling.shaderFiles =
      UpscaleShaderFileList({(fixture.root / "first.glsl").string(),
                             (fixture.root / "second.glsl").string()});
  const auto config = fixture.root / "settings.json";
  settings.save(config.string());
  auto restored = EngineSettings::load(config.string());
  const auto overlay2Absolute = (fixture.root / "overlay2").string();
  require(restored.assetOverlays.at("overlay2") == overlay2Absolute,
          "Settings store the overlay as name -> absolute path");
  require(restored.assetOverlayName == "overlay2",
          "Settings store the selected overlay name");
  require(restored.assetOverlayPath == overlay2Absolute,
          "Resolved overlay path is the persisted absolute");
  require(
      restored.translationPath == "translation.json" &&
          restored.fontPath == "custom.ttf" &&
          restored.vnFontPath == "dialogue.ttf" &&
          UpscaleShaderFiles(restored.upscaling.shaderFiles) ==
              std::vector<std::string>{"first.glsl", "second.glsl"},
      "Translations, fonts and ordered shaders also persist portable names");
  fs::create_directory("relocated");
  fs::rename("overlay2", "relocated/overlay2");
  // The persisted absolute no longer exists; without a matching name-based
  // candidate the overlay resolves to nothing.
  auto relocated = EngineSettings::load(config.string());
  require(relocated.assetOverlayPath.empty(),
          "A missing absolute overlay with no name candidate resolves empty");
  // Restore the overlay under a name-based candidate location and it resolves
  // by name.
  fs::create_directories("overlay2");
  std::ofstream("overlay2/scene.txt") << "translated";
  auto foundByName = EngineSettings::load(config.string());
  require(foundByName.assetOverlayPath == (fixture.root / "overlay2").string(),
          "The overlay name resolves through the standard folder search");
  AssetOverlay moved(foundByName.assetOverlayPath);
  require(!moved.resolve("scene.txt").empty(),
          "The name-resolved overlay still reads its assets");
  require(FileResolver::referenceName("C:\\old-machine\\overlay2\\") ==
              "overlay2",
          "Windows reference names are still portable to other platforms");

  fs::create_directory("overlay");
  std::ofstream("overlay/default.txt") << "auto";
  require(!AssetOverlay().resolve("default.txt").empty(),
          "Default overlay is discovered automatically");
  require(AssetOverlay("overlay2").resolve("default.txt").empty(),
          "Explicit overlay replaces automatic overlay selection");
  FileSystemAssetProvider provider;
  provider.setOverlay({});
  require(provider.overlayPath("default.txt").empty(),
          "Explicitly clearing an overlay disables it");

  fs::create_directories("game_assets/MyGame");
  std::ofstream("game_assets/MyGame/disc.iso") << "fixture";
  const auto candidates = FileResolver::candidates("MyGame");
  require(contains(candidates, fixture.root / "game_assets/MyGame"),
          "Launcher game directories use the same named-reference resolver");
  fs::create_directories("assets");
  std::ofstream("assets/directory-test.txt") << "engine";
  std::ofstream("game_assets/directory-test.txt") << "game";
  require(FileResolver::findAsset("assets/directory-test.txt") ==
                  fixture.root / "assets/directory-test.txt" &&
              FileResolver::findAsset("game_assets/directory-test.txt") ==
                  fixture.root / "game_assets/directory-test.txt",
          "Explicit asset mounts do not mix engine and game files with the "
          "same name");
  require(!gameAssetDirectory().empty(),
          "Empty game argument is valid before a launcher opens");
  require(saveDirectory().filename() == "org.kotonoha.directories" &&
              saveDirectory().parent_path().filename() == "Kotonoha",
          "Runtime save directory uses application package metadata");
  fs::create_directories("assets/fonts");
  std::ofstream("assets/fonts/OFL.txt") << "font license";
  std::ofstream("assets/fonts/invalid.ttf") << "not a font";
  require(!FileResolver::isFontFile("assets/fonts/OFL.txt") &&
              !FileResolver::isFontFile("assets/fonts/invalid.ttf") &&
              FileResolver::isFontFile(FileResolver::font()),
          "Font catalog requires a supported extension and a font signature");
  const auto &fonts = FileResolver::getFontMap();
  require(!fonts.count("OFL.txt") && !fonts.count("invalid.ttf"),
          "Licenses and invalid files are not registered as memory fonts");
}
} // namespace

int main() {
  try {
    SDL_SetAppMetadata("Directory tests", "1", "org.kotonoha.directories");
    Fixture fixture;
    layouts(fixture.root);
    relocation(fixture);
    std::cout << "Directory layouts, references and relocation passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
