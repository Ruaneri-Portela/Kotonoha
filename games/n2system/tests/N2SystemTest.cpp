// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <N2System/N2Runtime.hpp>
#include <SDL3/SDL.h>
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
    // Menu/title loops must not swallow native choice navigation.
    check(!Kotonoha::VideoSkipGesture::capturesInput(true, true),
          "Looping title movie must not intercept input");
    check(Kotonoha::VideoSkipGesture::capturesInput(true, false),
          "Narrative movie must intercept input");
    check(!Kotonoha::VideoSkipGesture::capturesInput(false, false),
          "Inactive movie must not intercept input");

    Kotonoha::VideoSkipGesture gesture;
    SDL_Event pointer{};
    pointer.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    pointer.button.which = 0;
    pointer.button.button = SDL_BUTTON_LEFT;
    pointer.button.clicks = 1;
    pointer.button.x = 100;
    pointer.button.y = 100;
    check(!gesture.accept(pointer, 1000),
          "Single mouse click must not skip a movie");
    check(gesture.accept(pointer, 1100),
          "Double mouse click may skip a narrative movie");
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    check(!gesture.accept(wheel, 1200),
          "Mouse wheel must never skip a movie");
    SDL_Event keyboard{};
    keyboard.type = SDL_EVENT_KEY_DOWN;
    keyboard.key.repeat = false;
    check(gesture.accept(keyboard, 1300),
          "Explicit keyboard press may skip a narrative movie");

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
                << vm.currentScriptPath() << " line=" << vm.currentLine()
                << " executed=" << vm.totalExecuted()
                << " waitKey=" << vm.waitingForKey()
                << " waitChoice=" << vm.waitingForChoice()
                << " finished=" << vm.finished()
                << " text='" << vm.dialogueSystem().fullText().substr(0, 60)
                << "' entities=" << vm.sceneGraph().entities().size() << "\n";

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

      // Headless Engine-backed title menu input test (hover, keyboard, gamepad,
      // click -> sg01_01 prologue dialogue -> advance).
      {
        N2System::N2Runtime engineRuntime(sgAssets);
        Kotonoha::EngineOptions opts;
        opts.assets = sgAssets;
        Kotonoha::Engine engine(engineRuntime, std::move(opts));
        struct ShutdownGuard {
          N2System::N2Runtime &rt;
          ~ShutdownGuard() { rt.shutdown(); }
        } guard{engineRuntime};

        auto *nvm = engineRuntime.nsbVm();
        check(nvm != nullptr, "Engine-backed N2Runtime must activate NsbVm");

        for (int frame = 0; frame < 600 && !nvm->waitingForChoice(); ++frame) {
          engineRuntime.advanceStoryTime(0.1);
          engineRuntime.update();
          if (nvm->waitingForKey() || nvm->activeMovie().active)
            engineRuntime.handleAdvanceAction();
        }
        check(nvm->currentScriptPath() == "nss/sys_title.nsb",
              "Engine-backed SG must reach sys_title.nsb");
        check(nvm->waitingForChoice() && nvm->activeChoices().size() == 5,
              "Engine-backed SG title menu must expose 5 choices");

        // Render one frame so textures & hover rendering path are exercised.
        check(engine.Iterate() == SDL_APP_CONTINUE,
              "Engine iterate on SG title screen must succeed");

        // Hover Load button (888, 97), navigate via keyboard + gamepad back to
        // Start (888, 37), then click Start in 1024x576 logical coordinates.
        SDL_Event hover{};
        hover.type = SDL_EVENT_MOUSE_MOTION;
        hover.motion.x = 920.0f;
        hover.motion.y = 110.0f;
        check(engineRuntime.onEvent(hover),
              "Mouse hover on SG Load button must be handled");

        SDL_Event keyUp{};
        keyUp.type = SDL_EVENT_KEY_DOWN;
        keyUp.key.key = SDLK_UP;
        keyUp.key.down = true;
        check(engineRuntime.onEvent(keyUp),
              "Keyboard Up on SG title menu must be handled");

        SDL_Event padDown{};
        padDown.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        padDown.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        padDown.gbutton.down = true;
        check(engineRuntime.onEvent(padDown),
              "Gamepad D-pad Down on SG title menu must be handled");

        SDL_Event padUp{};
        padUp.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        padUp.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
        padUp.gbutton.down = true;
        check(engineRuntime.onEvent(padUp),
              "Gamepad D-pad Up on SG title menu must be handled");

        SDL_Event clickStart{};
        clickStart.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        clickStart.button.button = SDL_BUTTON_LEFT;
        clickStart.button.down = true;
        clickStart.button.x = 920.0f;
        clickStart.button.y = 50.0f;
        check(engineRuntime.onEvent(clickStart),
              "Left click on SG Start button must trigger New Game");

        for (int frame = 0; frame < 400; ++frame) {
          engineRuntime.advanceStoryTime(0.1);
          engineRuntime.update();
          if (!engineRuntime.dialogueText().empty())
            break;
          if (nvm->waitingForKey() || nvm->activeMovie().active)
            engineRuntime.handleAdvanceAction();
        }
        const std::string firstLine = engineRuntime.dialogueText();
        check(!firstLine.empty(),
              "Selecting Start on SG title menu must enter sg01_01 prologue "
              "with visible dialogue");

        // Advance dialogue with Enter and verify dialogue progresses.
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        enter.key.down = true;
        bool advanced = false;
        for (int attempt = 0; attempt < 20 && !advanced; ++attempt) {
          engineRuntime.onEvent(enter);
          for (int f = 0; f < 10; ++f) {
            engineRuntime.advanceStoryTime(0.1);
            engineRuntime.update();
          }
          if (engineRuntime.dialogueText() != firstLine)
            advanced = true;
        }
        check(advanced,
              "Pressing Enter must advance SG sg01_01 prologue dialogue");

        // The runtime and Engine share the provider. Verify shutdown
        // destroys the native font atlas and its cached SDL textures while
        // the Engine renderer is still alive, including repeated teardown.
        engineRuntime.shutdown();
        check(sgAssets->cachedTextures() == 0,
              "N2System shutdown must release provider GPU textures");
        engineRuntime.shutdown();
        check(TTF_WasInit() > 0,
              "Engine UI must remain TTF-initialized during N2 teardown");
      }

      std::cout << "[Steins;Gate] Retail verification PASSED!\n";
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "N2SystemTest error: " << e.what() << "\n";
    return 1;
  }
}
