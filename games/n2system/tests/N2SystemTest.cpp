// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/N2Runtime.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

static void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}

int main() {
  try {
    auto assets = std::make_shared<N2System::N2ArchiveAssets>();
    N2System::N2Runtime runtime(assets);

    check(runtime.assets() != nullptr, "N2Runtime assets initialized");

    const std::string script = "chapter main {\n"
                               "SetDialogue(\"Kurisu\", \"Mad scientist!\");\n"
                               "WaitKey();\n"
                               "Finish();\n"
                               "}\n";

    runtime.vm().loadScript(script, "main");
    runtime.update();

    check(runtime.dialogueSpeaker() == "Kurisu", "N2Runtime speaker match");
    check(runtime.dialogueText() == "Mad scientist!",
          "N2Runtime dialogue match");

    bool advanceResult = runtime.handleAdvanceAction();
    check(advanceResult, "N2Runtime handleAdvanceAction");

    runtime.update();
    check(runtime.vm().finished(), "N2Runtime script finished");

    std::cout << "All N2Runtime integration tests passed!\n";

    if (const char *sgDir = std::getenv("KOTONOHA_STEINSGATE_DIR");
        sgDir &&
        std::filesystem::exists(std::filesystem::path(sgDir) / "nss.npa")) {
      std::cout << "[Steins;Gate] Mounting " << sgDir << "...\n";
      auto sgAssets = std::make_shared<N2System::N2ArchiveAssets>(sgDir);
      check(sgAssets->contains("system/system.ini"), "SG system.ini missing");
      check(sgAssets->contains("nss/0_boot.nsb"), "SG 0_boot.nsb missing");
      check(sgAssets->contains("cg/bg/bg01a.jpg"), "SG bg01a.jpg missing");
      check(sgAssets->contains("sound/bgm/bgm03.ogg"), "SG bgm03.ogg missing");
      check(sgAssets->contains("voice/oka_0102.ogg"),
            "SG oka_0102.ogg missing");
      check(sgAssets->contains("dx/title.ngs"), "SG dx/title.ngs missing");
      check(sgAssets->contains("Droid Sans Fallback.ttf"), "SG font missing");

      // Verify O(1) seekable streaming on voice.npa and loose dx/title.ngs
      auto voiceStream = sgAssets->openStream("voice/oka_0102.ogg");
      check(voiceStream != nullptr && voiceStream->size() > 1000,
            "SG voice stream failed");
      std::uint8_t oggHdr[4] = {};
      check(voiceStream->read(oggHdr, 4) == 4 && oggHdr[0] == 'O' &&
                oggHdr[1] == 'g' && oggHdr[2] == 'g' && oggHdr[3] == 'S',
            "SG voice stream OggS magic mismatch");

      auto movieStream = sgAssets->openStream("dx/title.ngs");
      check(movieStream != nullptr && movieStream->size() > 100000,
            "SG movie stream failed");
      std::uint8_t mpegHdr[4] = {};
      check(movieStream->read(mpegHdr, 4) == 4 && mpegHdr[0] == 0x00 &&
                mpegHdr[1] == 0x00 && mpegHdr[2] == 0x01 && mpegHdr[3] == 0xBA,
            "SG movie stream MPEG-PS magic mismatch");

      // Verify viewport detection
      N2System::N2Runtime sgRuntime(sgAssets);
      const auto vp = sgRuntime.viewport();
      check(vp.x == 1024 && vp.y == 576, "SG viewport must be 1024x576");

      // Boot NsbVm on system/system.ini -> nss/0_boot.nss
      auto cfg = N2System::SystemIniConfig::parseUtf16Le(
          sgAssets->read("system/system.ini"));
      check(cfg.productName == "STEINS;GATE", "SG productName mismatch");
      check(cfg.startScript == "nss/0_boot.nss", "SG startScript mismatch");

      N2System::NsbVm vm;
      vm.setConfig(cfg);
      vm.setLoader([sgAssets](std::string_view p,
                              std::vector<std::uint8_t> &nsb,
                              std::vector<std::uint8_t> &map) -> bool {
        const auto nsbPath = N2System::NsbScript::nssToNsbPath(p);
        const auto mapPath = N2System::NsbScript::nssToMapPath(p);
        if (!sgAssets->contains(nsbPath))
          return false;
        nsb = sgAssets->read(nsbPath);
        map = sgAssets->contains(mapPath) ? sgAssets->read(mapPath)
                                          : std::vector<std::uint8_t>{};
        return true;
      });
      check(vm.boot(cfg.startScript, "chapter.main"),
            "SG 0_boot.nsb boot failed");

      // Step through boot splash / title setup (advancing waits/movies)
      for (int frame = 0; frame < 600 && !vm.waitingForChoice(); ++frame) {
        vm.update(0.1f);
        vm.step(4096);
        if (vm.waitingForKey() || vm.activeMovie().active)
          vm.advance();
      }
      std::cout << "[Steins;Gate] After 0_boot: script="
                << vm.currentScriptPath() << " executed=" << vm.totalExecuted()
                << " entities=" << vm.sceneGraph().entities().size()
                << " choices=" << vm.activeChoices().size() << "\n";
      for (const auto &c : vm.activeChoices())
        std::cout << "  choice: " << c << "\n";
      check(vm.totalExecuted() > 50, "SG 0_boot did not execute bytecode");
      check(vm.currentScriptPath() == "nss/sys_title.nsb",
            "SG 0_boot did not reach sys_title.nsb");
      check(!vm.activeChoices().empty(), "SG title screen has no choices");

      // Click New Game ("タイトル開始") from the title screen!
      vm.triggerChoice("タイトル開始");
      for (int frame = 0; frame < 300; ++frame) {
        vm.update(0.1f);
        vm.step(4096);
        if (!vm.dialogueSystem().fullText().empty())
          break;
        if (vm.waitingForKey() || vm.activeMovie().active)
          vm.advance();
      }
      std::cout << "[Steins;Gate] After New Game: script="
                << vm.currentScriptPath() << " executed=" << vm.totalExecuted()
                << " entities=" << vm.sceneGraph().entities().size() << "\n";

      // Now verify prologue sg01_01.nsb executes to real dialogue + voice
      check(vm.jumpToScript("nss/sg01_01.nsb", "scene.sg01_01.nss"),
            "SG jumpToScript sg01_01 failed");
      for (int frame = 0; frame < 300; ++frame) {
        vm.update(0.1f);
        vm.step(4096);
        if (!vm.dialogueSystem().fullText().empty())
          break;
        if (vm.waitingForKey() || vm.activeMovie().active)
          vm.advance();
      }
      std::cout << "[Steins;Gate] sg01_01 script=" << vm.currentScriptPath()
                << " line=" << vm.currentLine()
                << " waitKey=" << vm.waitingForKey()
                << " waitChoice=" << vm.waitingForChoice()
                << " finished=" << vm.finished() << " speaker='"
                << vm.dialogueSystem().speaker() << "' voice='"
                << vm.dialogueSystem().voicePath() << "' text='"
                << vm.dialogueSystem().fullText().substr(0, 60) << "'\n";
      check(!vm.dialogueSystem().fullText().empty(),
            "SG sg01_01 did not produce dialogue");
      check(!vm.dialogueSystem().voicePath().empty(),
            "SG sg01_01 did not trigger voice");
      std::cout << "[Steins;Gate] Retail verification PASSED!\n";
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "N2SystemTest error: " << e.what() << "\n";
    return 1;
  }
}
