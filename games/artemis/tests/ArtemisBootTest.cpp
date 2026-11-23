// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Regression coverage for the Artemis title-first boot and native UI skins.
#include <Artemis/ArtemisAssets.hpp>
#include <Artemis/ArtemisRuntime.hpp>
#include <Artemis/ArtemisUi.hpp>
#include <Artemis/PfsArchive.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// assert() is compiled out in Release (NDEBUG), which turns a missing retail
// asset into a cryptic "Artemis asset not found: " error deep in the runtime.
// Keep every boot/UI invariant active in all build types with an actionable
// message naming the exact value that failed.
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void appendU32LE(std::vector<std::uint8_t> &buf, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    buf.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

std::vector<std::uint8_t>
buildPfs(const std::vector<std::pair<std::string, std::string>> &files) {
  std::uint32_t tableLen = 4;
  for (const auto &[name, content] : files)
    tableLen += 4 + static_cast<std::uint32_t>(name.size()) + 12;

  std::vector<std::uint8_t> table;
  appendU32LE(table, static_cast<std::uint32_t>(files.size()));
  std::uint32_t offset = 7 + tableLen;
  for (const auto &[name, content] : files) {
    appendU32LE(table, static_cast<std::uint32_t>(name.size()));
    table.insert(table.end(), name.begin(), name.end());
    appendU32LE(table, 0);
    appendU32LE(table, offset);
    appendU32LE(table, static_cast<std::uint32_t>(content.size()));
    offset += static_cast<std::uint32_t>(content.size());
  }

  const auto key = Artemis::PfsArchive::sha1(table.data(), table.size());
  std::vector<std::uint8_t> out{'p', 'f', '8'};
  appendU32LE(out, static_cast<std::uint32_t>(table.size()));
  out.insert(out.end(), table.begin(), table.end());
  for (const auto &[name, content] : files) {
    (void)name;
    for (std::size_t i = 0; i < content.size(); ++i) {
      const auto byte = static_cast<std::uint8_t>(content[i]);
      out.push_back(static_cast<std::uint8_t>(byte ^ key[i % 20]));
    }
  }
  return out;
}

std::vector<std::uint8_t> solidBmp(std::uint8_t red, std::uint8_t green,
                                   std::uint8_t blue) {
  std::vector<std::uint8_t> out(58, 0);
  out[0] = 'B';
  out[1] = 'M';
  appendU32LE(out, 58);
  appendU32LE(out, 54);
  appendU32LE(out, 40);
  appendU32LE(out, 1); // width
  appendU32LE(out, 1); // height
  out[26] = 1;         // planes
  out[28] = 32;        // bits per pixel
  appendU32LE(out, 4); // image size
  out[54] = blue;
  out[55] = green;
  out[56] = red;
  out[57] = 255;
  return out;
}

void writeBin(const std::filesystem::path &path,
              const std::vector<std::uint8_t> &bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

// Derive the shared engine font from the compiled source location instead of
// hardcoding an installation path. __FILE__ is emitted as an absolute path by
// CMake, so this works from any build directory.
std::filesystem::path engineFontPath() {
  auto root = std::filesystem::absolute(__FILE__);
  for (int i = 0; i < 4; ++i)
    root = root.parent_path();
  return root / "assets" / "fonts" / "NotoSansJP-Regular.ttf";
}

// The engine loads fonts from the executable's resource directory. Standalone
// test executables do not always get that directory populated, so stage a
// copy next to the test binary from the shared source asset.
void stageEngineFont() {
  const auto source = engineFontPath();
  const char *base = SDL_GetBasePath(); // SDL3 owns the returned string.
  const std::filesystem::path baseDir(base ? base : ".");
  const auto target = baseDir / "assets" / "fonts" / source.filename();
  std::error_code error;
  if (std::filesystem::exists(target, error))
    return;
  std::filesystem::create_directories(target.parent_path(), error);
  std::filesystem::copy_file(source, target,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
}

std::filesystem::path capture(Kotonoha::Engine &engine,
                              const std::filesystem::path &directory,
                              const std::string &name) {
  auto surface = Kotonoha::SurfacePtr(
      SDL_RenderReadPixels(&engine.GetRenderer(), nullptr));
  if (!surface)
    throw std::runtime_error("Cannot read Artemis frame for capture");
  const auto path = directory / name;
  if (!SDL_SaveBMP(surface.get(), path.string().c_str()))
    throw std::runtime_error("Cannot save Artemis capture: " + path.string());
  std::cout << "  capture: " << path << "\n";
  return path;
}

const char *kSyntheticTable = R"(lang = {
	ui_title01_en = {
		bt_start = { name="bt_start", file="title/bt_new_game", x=30, y=744, w=278, h=121, com="btn", exec="title_click", p1="start", p2="gamestart", clip="0,0,278,121", },
		bg = { name="bg", file="title/background_1a", x=0, y=0, w=1600, h=900, com="obj", },
	},
	ui_adv01 = {
		dockarea2 = { name="dockarea2", file="mw/base", x=360, y=868, w=1080, h=32, com="obj", },
		bt_save = { name="bt_save", file="mw/bt_save", x=1520, y=546, w=80, h=42, com="btn", clip="0,0,80,42", },
	},
}
)";

const char *kSyntheticBaseTable = R"(lang = {
	ui_system = {
		select = { file="mw/choice", x=415, y=309, w=770, h=44, clip="0,0,770,44", },
	},
}
)";

const char *kSyntheticGameStart = R"(astver = 2.0
ast = {
	block_00000 = {
		{"bg", file="bg01", path=":bg/"},
		{"text"},
		line = 3,
	},
	label = { top = { block="block_00000", label=1 }, },
}
)";

void requireRuntimeBoot(const std::shared_ptr<Artemis::ArtemisAssets> &assets,
                        const std::filesystem::path &captureDir,
                        std::size_t expectedButtons = 0) {
  require(assets->systemConfig().bootFile == "system/first.iet",
          "system.ini BOOT is not 'system/first.iet': '" +
              assets->systemConfig().bootFile + "'");
  require(assets->findStartScript() == "script/gamestart.ast",
          "start script is not 'script/gamestart.ast': '" +
              assets->findStartScript() + "'");
  require(!assets->uiLanguages().empty(), "no pc/<language> UI assets found");
  require(assets->uiLanguage() == "en",
          "UI language is not 'en': '" + assets->uiLanguage() + "'");

  Artemis::ArtemisUi ui(*assets);
  require(ui.load(), "cannot load the native UI table");
  require(ui.tablePath().find("list_windows") != std::string::npos,
          "UI table path is not a list_windows table: '" + ui.tablePath() +
              "'");
  if (expectedButtons != 0)
    require(ui.titleButtons().size() == expectedButtons,
            "expected " + std::to_string(expectedButtons) +
                " title buttons, got " +
                std::to_string(ui.titleButtons().size()));
  else
    require(!ui.titleButtons().empty(), "title screen has no buttons");
  const auto start = ui.titleItem("bt_start");
  require(start.has_value(), "UI table has no 'bt_start' item");
  require(start->logicalFile == "title/bt_new_game",
          "bt_start file is not 'title/bt_new_game': '" + start->logicalFile +
              "'");
  require(start->p2 == "gamestart",
          "bt_start p2 is not 'gamestart': '" + start->p2 + "'");
  const auto resolved = ui.resolveImage(*start);
  require(!resolved.empty(), "bt_start logical file '" + start->logicalFile +
                                 "' does not resolve to an asset");
  require(assets->contains(resolved),
          "resolved bt_start asset is missing: '" + resolved + "'");
  const auto choiceImage = ui.resolveImage("mw/choice");
  require(!choiceImage.empty(), "UI image 'mw/choice' does not resolve");

  Kotonoha::EngineOptions options;
  options.assets = assets;
  const auto font = engineFontPath();
  require(std::filesystem::exists(font),
          "shared engine font not found: '" + font.string() + "'");
  options.settings.fontPath = font.string();
  options.persistSettings = false;
  options.launcher = false;
  Artemis::ArtemisRuntime runtime(assets);
  Kotonoha::Engine engine(runtime, options);
  require(runtime.GetState() == Kotonoha::GameState::Menu,
          "boot did not enter the Menu state");
  require(runtime.titleVisible(), "title screen is not visible at boot");
  require(runtime.vm().currentScript().empty(),
          "a script is already running before START");
  for (int i = 0; i < 12; ++i) {
    require(engine.Iterate() == SDL_APP_CONTINUE,
            "engine stopped iterating on the title screen");
    SDL_Delay(50);
  }
  capture(engine, captureDir, "artemis-boot-title.bmp");

  require(runtime.handleNewGameAction(), "START action was rejected");
  require(runtime.GetState() == Kotonoha::GameState::Gameplay,
          "START did not enter the Gameplay state");
  require(!runtime.titleVisible(), "title screen is still visible after START");
  // Retail gamestart.ast immediately excalls 01_c_00.ast, so the live node
  // may have advanced; both are valid gameplay entry points.
  const std::string liveScript = runtime.vm().currentScript();
  std::cout << "  debug node after START: " << runtime.debugNode()
            << " (script='" << liveScript << "')\n";
  require(!liveScript.empty(),
          "no gameplay script is running after START (gamestart failed to "
          "load or execute)");
  std::cout << "  gameplay script: " << liveScript << "\n";
  for (int i = 0; i < 12; ++i) {
    require(engine.Iterate() == SDL_APP_CONTINUE,
            "engine stopped iterating during gameplay");
    SDL_Delay(50);
  }
  capture(engine, captureDir, "artemis-boot-gameplay-ui.bmp");
}

void testSyntheticBoot() {
  const auto dir = std::filesystem::temp_directory_path() /
                   "kotonoha_artemis_boot_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto pngBackground = solidBmp(30, 60, 140);
  const auto pngButton = solidBmp(220, 30, 40);
  const auto pngDock = solidBmp(20, 20, 20);
  const auto pngSave = solidBmp(40, 100, 200);
  const auto pngChoice = solidBmp(200, 200, 60);
  const auto bytes = buildPfs(
      {{"system.ini",
        "[WINDOWS]\nWIDTH = 1600\nHEIGHT = 900\nBOOT = system/first.iet\n"},
       {"script\\gamestart.ast", kSyntheticGameStart},
       {"system\\table\\list_windows_en.tbl", kSyntheticTable},
       {"system\\table\\list_windows.tbl", kSyntheticBaseTable},
       {"pc\\en\\title\\background_1a.bmp",
        std::string(pngBackground.begin(), pngBackground.end())},
       {"pc\\en\\title\\bt_new_game.bmp",
        std::string(pngButton.begin(), pngButton.end())},
       {"pc\\en\\mw\\base.bmp",
        std::string(pngDock.begin(), pngDock.end())},
       {"pc\\en\\mw\\bt_save.bmp",
        std::string(pngSave.begin(), pngSave.end())},
       {"pc\\en\\mw\\choice.bmp",
        std::string(pngChoice.begin(), pngChoice.end())}});
  writeBin(dir / "root.pfs", bytes);

  auto assets = std::make_shared<Artemis::ArtemisAssets>(dir.string());
  require(assets->archiveCount() == 1,
          "expected 1 synthetic PFS archive, got " +
              std::to_string(assets->archiveCount()));
  requireRuntimeBoot(assets, dir, 1);
  std::cout << "[ArtemisBootTest] Synthetic boot flow passed.\n";
  std::filesystem::remove_all(dir);
}

void testRetailBoot(const std::string &gameDir) {
  auto assets = std::make_shared<Artemis::ArtemisAssets>(gameDir);
  require(assets->archiveCount() >= 1,
          "no .pfs archives found in retail directory: '" + gameDir + "'");
  require(assets->entryCount() > 100, "retail directory has too few assets (" +
                                          std::to_string(assets->entryCount()) +
                                          ") in '" + gameDir + "'");

  Artemis::ArtemisUi ui(*assets);
  require(ui.load(), "cannot load the native UI table from '" + gameDir + "'");
  const auto start = ui.titleItem("bt_start");
  require(start.has_value(),
          "UI table has no 'bt_start' item in '" + gameDir + "'");
  require(start->logicalFile == "title/bt_new_game",
          "bt_start file is not 'title/bt_new_game': '" + start->logicalFile +
              "'");
  require(start->p2 == "gamestart",
          "bt_start p2 is not 'gamestart': '" + start->p2 + "'");
  require(ui.titleButtons().size() >= 7,
          "expected at least 7 title buttons, got " +
              std::to_string(ui.titleButtons().size()));

  // Take the artwork IDs from the shipped UI table instead of hardcoding
  // NUKITASHI 1 names: NUKITASHI 2 names the same items
  // `title/background_2a` / `title/start_titlelogo_2`. Asserting the table's
  // own IDs keeps this retail check valid for both titles.
  std::vector<std::string> uiImages;
  for (const char *name : {"bt_start", "bg", "titlelogo"}) {
    const auto item = ui.titleItem(name);
    require(item.has_value(), std::string("UI table has no '") + name +
                                  "' item in '" + gameDir + "'");
    uiImages.push_back(item->logicalFile);
  }
  for (const char *logical : {"mw/base", "mw/choice", "mw/bt_save"})
    uiImages.push_back(logical);
  for (const auto &logical : uiImages) {
    const std::string resolved = ui.resolveImage(logical);
    require(!resolved.empty(), "UI image '" + logical +
                                   "' does not resolve to an asset in '" +
                                   gameDir + "'");
    auto surface = assets->loadSurface(resolved);
    require(surface != nullptr && surface->w > 0 && surface->h > 0,
            "UI asset '" + logical + "' -> '" + resolved +
                "' failed to decode to a non-empty surface");
    std::cout << "  native UI: " << logical << " -> " << resolved << " ("
              << surface->w << "x" << surface->h << ")\n";
  }

  const char *captureEnv = SDL_getenv("KOTONOHA_ARTEMIS_CAPTURE_DIR");
  const auto captureDir = captureEnv ? std::filesystem::path(captureEnv)
                                     : std::filesystem::temp_directory_path();
  std::filesystem::create_directories(captureDir);
  requireRuntimeBoot(assets, captureDir);
  std::cout << "[ArtemisBootTest] Retail boot assertions passed.\n";
}

} // namespace

int main(int argc, char **argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }
  // This binary backs both the synthetic Boot test and the retail-only test.
  // Only --require-retail (used by Artemis.Retail) turns missing retail data
  // into a CTest "Skipped" result; synthetic coverage still runs and passes.
  const bool requireRetail =
      argc >= 2 && std::string(argv[1]) == "--require-retail";
  bool skippedRetail = false;
  try {
    stageEngineFont();
    testSyntheticBoot();
    // Retail checks are gated by an environment variable, never a hardcoded
    // absolute path in the repository.
    const char *retail = SDL_getenv("KOTONOHA_ARTEMIS_RETAIL_DIR");
    if (!requireRetail && argc >= 2)
      testRetailBoot(argv[1]);
    else if (retail && *retail)
      testRetailBoot(retail);
    else if (requireRetail) {
      std::cerr << "[SKIP] KOTONOHA_ARTEMIS_RETAIL_DIR not provided\n";
      skippedRetail = true;
    } else
      std::cout << "[ArtemisBootTest] Retail checks skipped (set "
                   "KOTONOHA_ARTEMIS_RETAIL_DIR).\n";
  } catch (const std::exception &error) {
    std::cerr << "[ArtemisBootTest] FAILED: " << error.what() << "\n";
    SDL_Quit();
    return 1;
  }
  SDL_Quit();
  return skippedRetail ? 77 : 0;
}
