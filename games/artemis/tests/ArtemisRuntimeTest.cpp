// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisAssets.hpp>
#include <Artemis/ArtemisRuntime.hpp>
#include <Artemis/AstParser.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

void testSyntheticRuntime() {
  auto assets = std::make_shared<Artemis::ArtemisAssets>();
  Artemis::ArtemisRuntime runtime(assets);
  assert(runtime.viewport().x == 1280);
  assert(runtime.viewport().y == 720);
  std::cout << "[ArtemisRuntimeTest] Synthetic runtime check passed.\n";
}

void testRetailGame(const std::string &gameDir) {
  std::cout << "[ArtemisRetailTest] Testing retail directory: " << gameDir
            << "\n";
  auto assets = std::make_shared<Artemis::ArtemisAssets>(gameDir);
  assert(assets->archiveCount() >= 1);
  assert(assets->entryCount() > 100);

  const auto &cfg = assets->systemConfig();
  std::cout << "  Archives mounted: " << assets->archiveCount()
            << ", merged entries: " << assets->entryCount()
            << ", stage: " << cfg.width << "x" << cfg.height << "\n";
  assert(cfg.width == 1600 && cfg.height == 900);

  const auto scripts = assets->discoverScripts();
  assert(!scripts.empty());
  const std::string startScript = assets->findStartScript();
  assert(!startScript.empty());
  std::cout << "  Scripts discovered: " << scripts.size()
            << ", startScript: " << startScript << "\n";

  // Verify VM executes through gamestart -> first scenario dialogue!
  Artemis::ArtemisRuntime runtime(assets);
  auto &vm = runtime.vm();
  assert(vm.loadScript(startScript));

  int dialogueCount = 0;
  bool verifiedBgImage = false;
  bool verifiedFgImage = false;
  bool verifiedAudio = false;

  for (int i = 0; i < 64 && dialogueCount < 10; ++i) {
    const auto reason = vm.step();
    if (reason == Artemis::VmWaitReason::WaitTimer) {
      vm.finishWaitTimer();
      continue;
    }
    if (reason == Artemis::VmWaitReason::Movie) {
      vm.finishMovie();
      continue;
    }
    if (reason == Artemis::VmWaitReason::Dialogue) {
      ++dialogueCount;
      if (dialogueCount <= 3) {
        std::cout << "  Dialogue #" << dialogueCount << " ["
                  << vm.currentScript() << ":" << vm.currentBlock() << "] "
                  << (vm.dialogue().speaker.empty() ? "(narration)"
                                                    : vm.dialogue().speaker)
                  << ": " << vm.dialogue().text << "\n";
      }
      assert(!vm.dialogue().text.empty());

      // Verify background image decodes cleanly via AssetProvider::loadSurface!
      if (!verifiedBgImage && !vm.bg().file.empty() && !vm.bg().isSolidColor) {
        const std::string bgPath =
            assets->resolveImagePath(vm.bg().path, vm.bg().file);
        if (!bgPath.empty()) {
          auto surf = assets->loadSurface(bgPath);
          assert(surf != nullptr && surf->w > 0 && surf->h > 0);
          std::cout << "  Verified BG surface: " << bgPath << " (" << surf->w
                    << "x" << surf->h << ")\n";
          verifiedBgImage = true;
        }
      }

      // Verify FG sprite decodes cleanly!
      if (!verifiedFgImage && !vm.sprites().empty()) {
        const auto &spr = vm.sprites().front();
        const std::string fgPath = assets->resolveImagePath(spr.path, spr.file);
        if (!fgPath.empty()) {
          auto surf = assets->loadSurface(fgPath);
          assert(surf != nullptr && surf->w > 0 && surf->h > 0);
          std::cout << "  Verified FG surface: " << fgPath << " (" << surf->w
                    << "x" << surf->h << ")\n";
          verifiedFgImage = true;
        }
      }

      // Verify audio clip decodes cleanly!
      if (!verifiedAudio) {
        for (const auto &se : vm.consumeSeEvents()) {
          if (!se.stop && !se.file.empty()) {
            const std::string sePath =
                assets->resolveAudioPath(":se/", se.file);
            if (!sePath.empty()) {
              auto clip = assets->loadAudio(sePath);
              assert(!clip.data.empty());
              std::cout << "  Verified SE audio: " << sePath << " ("
                        << clip.data.size() << " PCM bytes)\n";
              verifiedAudio = true;
              break;
            }
          }
        }
      }

      vm.advance();
      continue;
    }
    break;
  }

  // Also verify BGM pair resolution & decode if SE wasn't hit yet.
  if (!verifiedAudio) {
    const auto [intro, loop] = assets->resolveBgmPaths("bgm01");
    const std::string bgmPath = !loop.empty() ? loop : intro;
    assert(!bgmPath.empty());
    auto clip = assets->loadAudio(bgmPath);
    assert(!clip.data.empty());
    std::cout << "  Verified BGM audio: " << bgmPath << " (" << clip.data.size()
              << " PCM bytes)\n";
    verifiedAudio = true;
  }

  // Verify at least one .ipt animation spec parses cleanly if present.
  const auto iptFiles = assets->list("image/anime");
  for (const auto &f : iptFiles) {
    if (f.size() > 4 && f.compare(f.size() - 4, 4, ".ipt") == 0) {
      const auto spec = Artemis::AstParser::parseIpt(assets->loadTextUtf8(f));
      assert(!spec.frames.empty());
      std::cout << "  Verified IPT spec: " << f << " (" << spec.frames.size()
                << " frames)\n";
      break;
    }
  }

  assert(dialogueCount >= 5);
  assert(verifiedBgImage);
  assert(verifiedAudio);
  std::cout << "[ArtemisRetailTest] PASSED for " << gameDir << "!\n";
}

} // namespace

int main(int argc, char **argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  const char *retail = SDL_getenv("KOTONOHA_ARTEMIS_RETAIL_DIR");
  if (argc >= 2) {
    testRetailGame(argv[1]);
  } else if (retail && *retail) {
    testRetailGame(retail);
  } else {
    testSyntheticRuntime();
  }

  SDL_Quit();
  return 0;
}
