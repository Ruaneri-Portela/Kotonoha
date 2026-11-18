// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Retail 1:1 flow verification for LucaSystem (AIR).
// Validates all 26 scenario scripts, jump targets, seen170 (DREAM) New Game
// prologue, @Speaker@ + voice IDs + anchored sprites + BGM/SE + SELECT (#6001)
// + IFN branch + cross-scenario JUMP (seen170 -> seen190 -> seen230) +
// Save/Load roundtrip + SUMMER (seen700), AIR (seen163), FIRST SKY (seen800).
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/LucaSystemScript.hpp>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool cond, const std::string &msg) {
  if (!cond)
    throw std::runtime_error("LucaSystem RetailAirFlow FAIL: " + msg);
}

void stepThroughScenario(LucaSystem::LucaSystemRuntime &rt, int maxSteps,
                         int choiceOption, bool &sawSpeaker, bool &sawVoice,
                         bool &sawBgm, bool &sawChoice,
                         std::string &reachedNextScenario) {
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

    auto assets =
        std::make_shared<Kotonoha::Providers::LucaSystemAssets>(airPath, "");

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
    std::string nextScenario;
    stepThroughScenario(runtime, 20000, 0, sawSpeaker, sawVoice, sawBgm,
                        sawChoice, nextScenario);
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
