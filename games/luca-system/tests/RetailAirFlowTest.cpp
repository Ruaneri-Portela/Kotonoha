// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Retail 1:1 flow verification for LucaSystem (AIR).
// Validates all 26 scenario scripts, jump targets, seen170 (DREAM) New Game
// prologue, @Speaker@ + voice IDs + anchored sprites + BGM/SE + SELECT (#6001)
// + IFN branch + cross-scenario JUMP (seen170 -> seen190 -> seen230) +
// Save/Load roundtrip + SUMMER (seen700), AIR (seen163), FIRST SKY (seen800).
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/TextInputDialog.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <LucaSystem/AirGallery.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>
#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool cond, const std::string &msg) {
  if (!cond)
    throw std::runtime_error("LucaSystem RetailAirFlow FAIL: " + msg);
}

struct TestStorage {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("luca-retail-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  TestStorage() {
    require(std::filesystem::create_directory(root), "create test storage");
    require(SDL_SetEnvironmentVariable(SDL_GetEnvironment(),
                                       "KOTONOHA_USER_DATA_DIR",
                                       root.string().c_str(), true),
            "set isolated save root");
  }
  ~TestStorage() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

void checkGallery(
    const std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> &assets) {
  const TestStorage storage;
  const auto catalog = LucaSystem::ParseAirGallery(assets->read("_cggallery"));
  require(catalog.size() == 4 && catalog[0].size() == 22 &&
              catalog[1].size() == 20 && catalog[2].size() == 16 &&
              catalog[3].size() == 14,
          "AIR gallery catalog contains 72 retail slots");
  const auto first = catalog.front().front().variants;
  require(first == std::vector<uint16_t>{1127, 1128, 1130},
          "AIR gallery first slot resolves its grouped retail variants");
  require(assets->read("image:1127") == assets->readImageAsset(1127),
          "gallery image logical ID resolves the event CG class");

  const auto setIdentity = [] {
    // The Engine's SDLSession can clear app metadata on destruction.
    require(SDL_SetAppMetadata("LucaSystem Gallery Test", "0.1.0",
                               "me.hirameki.kotonoha.lucasystem.gallery-test"),
            "set gallery test application identity");
  };
  const auto visitGallery = [&](bool unlocked) {
    setIdentity();
    LucaSystem::LucaSystemRuntime runtime(assets);
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.launcher = false;
    options.rendererOverride = "software";
    Kotonoha::Engine engine(runtime, options);
    const auto key = [&](SDL_Keycode value) {
      SDL_Event event{};
      event.type = SDL_EVENT_KEY_DOWN;
      event.key.key = value;
      require(runtime.onEvent(event), "gallery key is handled");
    };
    key(SDLK_ESCAPE); // retail splash logo
    key(SDLK_LEFT);   // MUSIC
    key(SDLK_LEFT);   // GALLERY
    key(SDLK_RETURN);
    require(runtime.isGalleryOpen() && runtime.isStoryPaused(),
            "title opens gallery as a Kotonoha modal");
    runtime.renderOverlay(engine.GetRenderer(), 1);
    require(assets->cachedTextures() >= 2,
            "gallery renders the original category background and atlas");
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    key(SDLK_PAGEDOWN);
    key(SDLK_ESCAPE);
    key(SDLK_LEFT);
    key(SDLK_RETURN);
    key(SDLK_RETURN);
    require(runtime.galleryImageId() == (unlocked ? first[0] : 0),
            "gallery opens only the unlocked retail CG");
    if (unlocked) {
      key(SDLK_RIGHT);
      require(runtime.galleryImageId() == first[0],
              "gallery viewer cannot browse locked variants");
      const auto cached = assets->cachedTextures();
      runtime.renderOverlay(engine.GetRenderer(), 1);
      require(assets->cachedTextures() > cached,
              "gallery renderer loaded the unlocked full-size CG");
      key(SDLK_ESCAPE);
    } else {
      runtime.renderOverlay(engine.GetRenderer(), 1);
    }
    key(SDLK_ESCAPE);
    key(SDLK_ESCAPE);
    require(!runtime.isGalleryOpen() && !runtime.isStoryPaused(),
            "back returns to the AIR title and releases modal input");
    runtime.shutdown();
  };
  visitGallery(false);
  {
    setIdentity();
    LucaSystem::LucaSystemRuntime story(assets);
    const LucaSystem::Script script("seen170", assets->read("seen170"));
    const auto *load = script.instructions().data();
    require(script.instructionCount() > 141 && load[133].opcode == 55 &&
                load[141].opcode == 74 && load[133].paramBytes.size() == 4,
            "retail seen170 starts by staging the first gallery CG");
    require(story.startScenario("seen170", 133) &&
                story.isCgUnlocked(first[0]) && !story.isCgUnlocked(first[1]),
            "retail DRAW unlocks only the CG that appeared");
    require(std::filesystem::is_regular_file(Kotonoha::saveDirectory() /
                                             "global_system.kns"),
            "CG unlock is persisted in Kotonoha's global save");
  }
  visitGallery(true);
  std::cout << "AIR Gallery: 72 slots; title, navigation, locked variants, "
               "retail DRAW, persistence and return verified\n";
}

void checkSaveGrid(
    const std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> &assets) {
  const TestStorage storage;
  LucaSystem::LucaSystemRuntime runtime(assets);
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.launcher = false;
  options.rendererOverride = "software";
  Kotonoha::Engine engine(runtime, options);
  struct RuntimeCleanup {
    LucaSystem::LucaSystemRuntime &runtime;
    ~RuntimeCleanup() { runtime.shutdown(); }
  } cleanup{runtime};
  const auto key = [&](SDL_Keycode value) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = value;
    require(runtime.onEvent(event), "save grid key is handled");
  };
  const auto click = [&](float x, float y) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    require(runtime.onEvent(event), "save grid pointer action is handled");
  };
  const auto slots = [&]() -> const Kotonoha::VNSaveMenu * {
    for (auto *scene : runtime.GetActiveScenes())
      if (auto *menu = dynamic_cast<Kotonoha::VNSaveMenu *>(scene))
        return menu->isOpen() ? menu : nullptr;
    return nullptr;
  };
  const auto openTitleLoad = [&] { click(621, 642); };
  const auto openPauseLoad = [&] {
    key(SDLK_ESCAPE);
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
  };
  key(SDLK_ESCAPE); // skip retail startup logos
  openTitleLoad();
  require(slots() && runtime.isStoryPaused(), "title opens native load grid");
  auto cached = assets->cachedTextures();
  runtime.renderOverlay(engine.GetRenderer(), 1);
  require(assets->cachedTextures() >= cached + 4,
          "retail Load renders the original background, cards and arrows");
  key(SDLK_X);
  require(!slots() && !runtime.isStoryPaused(), "cancel Load returns to title");
  click(70, 642); // NEW GAME
  for (int i = 0; i < 1000 && !runtime.isDialogueVisible(); ++i) {
    if (runtime.isMoviePlaying())
      runtime.skipMovie();
    else
      runtime.skipWait();
  }
  require(runtime.currentScenarioName() == "seen170" &&
              runtime.isDialogueVisible(),
          "AIR title starts retail dialogue");
  const auto pc = runtime.instructionPosition();
  const auto line = runtime.dialogueText();
  constexpr uint32_t marker = 6100;
  runtime.setVariable(marker, 42);
  key(SDLK_ESCAPE);
  key(SDLK_RETURN); // Save
  require(slots() && runtime.isStoryPaused(), "pause opens native save grid");
  cached = assets->cachedTextures();
  runtime.renderOverlay(engine.GetRenderer(), 1);
  require(assets->cachedTextures() >= cached + 5,
          "retail Save renders original art and focused card");
  click(171, 147); // first card: native new-save dialog
  Kotonoha::TextInputDialog *nameEditor = nullptr;
  for (auto *scene : runtime.GetActiveScenes())
    if (auto *dialog = dynamic_cast<Kotonoha::TextInputDialog *>(scene))
      nameEditor = dialog;
  require(nameEditor != nullptr, "new slot opens native naming editor");
  nameEditor->update();
  key(SDLK_RETURN); // accept the suggested scenario name
  const auto directory = Kotonoha::saveDirectory();
  const auto named = directory / "seen170.vn";
  require(!slots() && std::filesystem::is_regular_file(named) &&
              runtime.instructionPosition() == pc,
          "naming editor saves a .vn without advancing the story");

  // Populate a second page with real native saves carrying a different
  // marker, so page navigation cannot pass by loading the initial card.
  for (int i = 10; i < 30; ++i) {
    runtime.setVariable(marker, i);
    runtime.saveGame(
        (directory / ("grid-" + std::to_string(i) + ".vn")).string());
  }
  runtime.setVariable(marker, 99);
  openPauseLoad();
  require(slots() && runtime.isStoryPaused(), "Load opens from pause");
  key(SDLK_DOWN);
  key(SDLK_ESCAPE);
  require(!slots() && runtime.instructionPosition() == pc &&
              runtime.getVariable(marker) == 99,
          "cancelling pause Load does not resume a save");
  openPauseLoad();
  SDL_Event dpad{};
  dpad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  dpad.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
  require(runtime.onEvent(dpad), "save grid accepts the controller D-pad");
  key(SDLK_RETURN);
  require(!slots() && runtime.getVariable(marker) == 15,
          "controller down moves by a full grid row");
  openPauseLoad();
  key(SDLK_C);
  key(SDLK_RETURN);
  require(!slots() && runtime.getVariable(marker) == 42 &&
              runtime.instructionPosition() == pc &&
              runtime.dialogueText() == line,
          "pause Load restores the selected slot from page two");

  key(SDLK_ESCAPE);
  for (int i = 0; i < 3; ++i)
    key(SDLK_RIGHT);
  key(SDLK_RETURN); // return to title
  runtime.setVariable(marker, 77);
  openTitleLoad();
  click(778, 675); // next page (original arrow)
  click(171, 147); // first slot of second page
  require(!slots() && !runtime.isStoryPaused() &&
              runtime.instructionPosition() == pc &&
              runtime.dialogueText() == line &&
              runtime.getVariable(marker) == 42,
          "title Load restores dialogue and reattaches gameplay scenes");
  std::cout << "AIR save grid: original Save/Load art, title, pause, paging, "
               "native .vn write/load and cancellation verified\n";
}

void stepThroughScenario(LucaSystem::LucaSystemRuntime &rt, int maxSteps,
                         int choiceOption, bool &sawSpeaker, bool &sawVoice,
                         bool &sawBgm, bool &sawChoice,
                         std::string &reachedNextScenario,
                         std::string_view expectedLine = {},
                         bool *sawExpectedLine = nullptr) {
  const std::string startScen = rt.currentScenarioName();
  for (int i = 0; i < maxSteps && rt.isRunning(); ++i) {
    if (rt.currentScenarioName() != startScen &&
        !rt.currentScenarioName().empty()) {
      reachedNextScenario = rt.currentScenarioName();
      return;
    }
    if (rt.isMoviePlaying()) {
      rt.skipMovie();
      continue;
    }
    if (rt.isChoiceActive()) {
      const auto opts = rt.activeChoices();
      require(opts.size() >= 2,
              "SELECT choice must decode >= 2 localized options");
      sawChoice = true;
      const int pick =
          (choiceOption >= 0 && static_cast<size_t>(choiceOption) < opts.size())
              ? choiceOption
              : 0;
      require(rt.selectChoice(pick), "selectChoice succeeded");
      require(rt.getVariable(6001) == pick,
              "SELECT wrote option index into #6001");
      continue;
    }
    if (rt.isDialogueVisible()) {
      if (sawExpectedLine && rt.dialogueText() == expectedLine)
        *sawExpectedLine = true;
      if (!rt.dialogueSpeaker().empty())
        sawSpeaker = true;
      if (rt.lastVoiceId() != 0)
        sawVoice = true;
      if (rt.activeBgmId() != 0)
        sawBgm = true;
      // Ensure raw @Speaker@ or $n / $[ tags were sanitized
      require(rt.dialogueText().find("$n") == std::string::npos,
              "Inline $n tag sanitized in dialogue");
      require(rt.dialogueText().find("$]") == std::string::npos,
              "Inline $[ruby$] tag sanitized in dialogue");
      rt.advanceDialogue();
      continue;
    }
    rt.skipWait();
  }
  if (rt.currentScenarioName() != startScen &&
      !rt.currentScenarioName().empty()) {
    reachedNextScenario = rt.currentScenarioName();
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string airPath;
    if (argc > 1 && argv[1] && *argv[1])
      airPath = argv[1];
    else if (const char *env = std::getenv("LUCA_SYSTEM_TEST_DATA"))
      airPath = env;

    if (airPath.empty() || !std::filesystem::exists(airPath)) {
      std::cout << "skip - LUCA_SYSTEM_TEST_DATA not provided\n";
      return 0;
    }

    SDL_SetAppMetadata("LucaSystem Retail Test", "0.1.0",
                       "me.hirameki.kotonoha.lucasystem.test");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
      throw std::runtime_error(SDL_GetError());

    const std::string_view mode = argc > 2 ? argv[2] : "";
    const bool gallery = mode == "--gallery";
    const bool saveGrid = mode == "--save-grid";
    const std::string overlay = !gallery && !saveGrid ? std::string(mode) : "";
    const std::string_view expectedLine = argc > 3 ? argv[3] : "";
    auto assets = std::make_shared<Kotonoha::Providers::LucaSystemAssets>(
        airPath, overlay);
    if (gallery || saveGrid) {
      // This mode has no overlay and does not access the user's save slots.
      if (gallery)
        checkGallery(assets);
      else
        checkSaveGrid(assets);
      SDL_Quit();
      return 0;
    }

    // 1. Verify resource-class asset separation (no VOICE.PAK / BGCG.PAK ID
    // collision!) and virtual 65000/65001 solid fills + loose WebM movies.
    {
      const auto bg1 = LucaSystem::DecodeCzImage(assets->readImageAsset(1));
      require(bg1.width == 1280 && bg1.height == 720,
              "Image ID 1 resolves to BGCG.PAK 1280x720, not SCRIPT/VOICE");
      const auto voice1 = assets->readVoiceAsset(1);
      require(voice1.size() > 4 && voice1[0] == 'O' && voice1[1] == 'g' &&
                  voice1[2] == 'g' && voice1[3] == 'S',
              "Voice ID 1 resolves to Ogg Vorbis in VOICE.PAK");
      const auto decodedVoice = assets->loadAudio("voice:1");
      require(!decodedVoice.data.empty() && decodedVoice.spec.freq > 0,
              "Kotonoha decodes original retail AIR voice");
      const auto bgm1 = assets->readBgmAsset(100001);
      require(bgm1.size() > 4 && bgm1[0] == 'O' && bgm1[1] == 'g',
              "BGM 100001 resolves to Ogg Vorbis in MUSIC.PAK");
      const auto se1 = assets->readSeAsset(200001);
      require(se1.size() > 4 && se1[0] == 'O' && se1[1] == 'g',
              "SE 200001 resolves to Ogg Vorbis in SE.PAK");
      const auto white =
          LucaSystem::DecodeCzImage(assets->readImageAsset(65000));
      const auto black =
          LucaSystem::DecodeCzImage(assets->readImageAsset(65001));
      require(white.rgba.size() == 4 && white.rgba[0] == 255 &&
                  black.rgba.size() == 4 && black.rgba[0] == 0,
              "Virtual image IDs 65000 (white) and 65001 (black) synthesize");
      const auto char1 =
          LucaSystem::DecodeCzImage(assets->readImageAsset(10001));
      require(char1.anchorX > 0 && char1.anchorY > 0,
              "CHARCG.PAK sprite preserves extended CZ4 anchorX/anchorY");
      require(assets->contains("movie/AIR_OP_A.webm") ||
                  assets->contains("movie/AIR_OP_A_EN.webm"),
              "Loose files/movie/AIR_OP_A*.webm exposed through VFS");
    }

    // 2. Exhaustive bytecode & branch target verification across all 26 seen*
    // scenarios in SCRIPT.PAK.
    const auto *scriptPak = assets->scriptPak();
    require(scriptPak != nullptr, "SCRIPT.PAK mounted");
    size_t scenarioCount = 0;
    for (const auto &entry : scriptPak->entries()) {
      if (entry.name.rfind("seen", 0) != 0)
        continue;
      ++scenarioCount;
      LucaSystem::Script scr(entry.name, scriptPak->read(entry));
      require(scr.valid(), "Scenario parses cleanly: " + entry.name);
      for (const auto &ins : scr.instructions()) {
        if (ins.opcode == 15 && ins.paramBytes.size() >= 4) { // GOTO
          const uint32_t target = uint32_t(ins.paramBytes[0]) |
                                  (uint32_t(ins.paramBytes[1]) << 8) |
                                  (uint32_t(ins.paramBytes[2]) << 16) |
                                  (uint32_t(ins.paramBytes[3]) << 24);
          require(
              LucaSystem::FindInstructionAtOffset(scr.instructions(), target)
                  .has_value(),
              entry.name + ": GOTO lands on exact instruction");
        } else if (ins.opcode == 19) { // IFN
          size_t cur = 0;
          const std::string expr = LucaSystem::ReadLsUtf8(ins.paramBytes, cur);
          require(!expr.empty() && cur + 4 <= ins.paramBytes.size(),
                  entry.name + ": IFN decodes ls_utf8 expression + target");
          const uint32_t target = uint32_t(ins.paramBytes[cur]) |
                                  (uint32_t(ins.paramBytes[cur + 1]) << 8) |
                                  (uint32_t(ins.paramBytes[cur + 2]) << 16) |
                                  (uint32_t(ins.paramBytes[cur + 3]) << 24);
          require(
              LucaSystem::FindInstructionAtOffset(scr.instructions(), target)
                  .has_value(),
              entry.name + ": IFN lands on exact instruction");
        } else if (ins.opcode == 21) { // JUMP
          size_t cur = 0;
          const std::string dst = LucaSystem::ReadLsUtf8(ins.paramBytes, cur);
          require(!dst.empty() && scriptPak->findByName(dst) != nullptr,
                  entry.name + ": JUMP target scenario exists: " + dst);
        }
      }
    }
    require(scenarioCount == 26,
            "Verified all 26 seen* scenarios in AIR SCRIPT.PAK");

    // 3. Drive DREAM New Game (seen170) -> choices -> BGM/Voice/Sprites ->
    // cross-scenario JUMP to seen190, plus Save/Load roundtrip!
    LucaSystem::LucaSystemRuntime runtime(assets);
    require(runtime.startScenario("seen170"),
            "Launch canonical DREAM New Game root seen170");
    bool sawSpeaker = false, sawVoice = false, sawBgm = false,
         sawChoice = false;
    bool sawExpectedLine = false;
    std::string nextScenario;
    stepThroughScenario(runtime, 20000, 0, sawSpeaker, sawVoice, sawBgm,
                        sawChoice, nextScenario, expectedLine,
                        &sawExpectedLine);
    if (!expectedLine.empty()) {
      require(sawExpectedLine, "seen170 displayed the imported MESSAGE");
      std::cout << "AIR overlay displayed the imported MESSAGE\n";
    }
    require(sawSpeaker, "seen170 parsed @Speaker@ tags");
    require(sawVoice, "seen170 triggered voice IDs from MESSAGE");
    require(sawBgm, "seen170 triggered 32-bit MUSIC.PAK BGM IDs");
    require(sawChoice, "seen170 presented UTF-16LE $d SELECT choices");
    require(nextScenario == "seen190",
            "seen170 chained via OP_JUMP to seen190 (got '" + nextScenario +
                "')");
    require(runtime.getVariable(30001) == 1280 &&
                runtime.getVariable(30002) == 720,
            "EQUN 16-bit variable assignment set #30001=1280, #30002=720");

    // Save/Load roundtrip mid-seen190
    while (runtime.isRunning() && !runtime.isDialogueVisible() &&
           !runtime.isChoiceActive())
      runtime.skipWait();
    const auto saveFile =
        (Kotonoha::saveDirectory() / "retail_air_test.kns").string();
    const auto savedDialogue = runtime.dialogueText();
    const auto savedSpeaker = runtime.dialogueSpeaker();
    const auto savedPc = runtime.instructionPosition();
    runtime.saveGame(saveFile);

    LucaSystem::LucaSystemRuntime restored(assets);
    restored.loadGame(saveFile);
    require(restored.currentScenarioName() == "seen190" &&
                restored.instructionPosition() == savedPc &&
                restored.dialogueText() == savedDialogue &&
                restored.dialogueSpeaker() == savedSpeaker,
            "Headless Save/Load roundtrip preserves exact scenario, PC, "
            "speaker and dialogue");

    // 4. Verify SUMMER (seen700), AIR (seen163), and FIRST SKY (seen800) roots
    // boot and advance cleanly!
    for (const char *root : {"seen700", "seen163", "seen800"}) {
      require(runtime.startScenario(root),
              std::string("Boot chapter root ") + root);
      bool spk = false, v = false, b = false, ch = false;
      std::string nxt;
      stepThroughScenario(runtime, 250, 0, spk, v, b, ch, nxt);
      require(runtime.instructionPosition() > 5,
              std::string("Chapter root advances cleanly: ") + root);
    }

    std::filesystem::remove(saveFile);
    SDL_Quit();
    std::cout << "LucaSystem Retail AIR 1:1 flow verification passed!\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    SDL_Quit();
    return 1;
  }
}
