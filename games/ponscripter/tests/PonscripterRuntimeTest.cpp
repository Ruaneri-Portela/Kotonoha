// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterRuntime.hpp>

#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/core/Engine.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void check(bool cond, const char *msg) {
  if (!cond)
    throw std::runtime_error(msg);
}

void runSyntheticTest() {
  auto assets = std::make_shared<Ponscripter::PonscripterAssets>();
  Ponscripter::PonscripterRuntime runtime(assets);
  check(runtime.assets() != nullptr, "PonscripterRuntime assets initialized");

  const std::string script =
      "\xEF\xBB\xBF;mode800\n"
      "*define\n"
      "globalon\n"
      "numalias end1_clear,901\n"
      "game\n"
      "*start\n"
      "setwindow3 60,70,34,20,20,20,0,4,0,1,1,#000000,40,50\n"
      "^Bem-vindo ao PONscripter!@\n"
      "br\n"
      "select \"^Iniciar\",*begin,\"^Sair\",*quit\n"
      "*begin\n"
      "mov %end1_clear,1\n"
      "^Rota iniciada.\\\n"
      "end\n"
      "*quit\n"
      "end\n";

  runtime.vm().appendScriptFile("00.utf", script);
  check(runtime.vm().start(), "Synthetic script must start");
  check(runtime.viewport().x == 800 && runtime.viewport().y == 600,
        "Viewport must be 800x600");

  runtime.update();
  check(runtime.dialogueText().find("Bem-vindo ao PONscripter!") !=
            std::string::npos,
        "Dialogue text must match initial line");

  check(runtime.handleAdvanceAction(), "Advance past @ wait");
  runtime.update();
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSelect,
        "VM must reach select choice");

  check(runtime.handleAdvanceAction(), "Select first choice (*begin)");
  runtime.update();
  check(runtime.vm().variables().getNum("end1_clear") == 1,
        "%end1_clear must be 1");
  check(runtime.handleAdvanceAction(), "Advance past final page");
  runtime.update();
  check(runtime.vm().isHalted(), "VM must halt at end");
}

void runRetailSayaAudit(const std::string &sayaDir) {
  auto assets = std::make_shared<Ponscripter::PonscripterAssets>(sayaDir);
  const auto scripts = assets->discoverScripts();
  check(scripts.size() == 2 && scripts[0].first == "00.utf" &&
            scripts[1].first == "01.utf",
        "Retail SayaNoUta_PT-BR must discover 00.utf and 01.utf");

  // Verify case-insensitive asset resolution & sound->voice fallback on retail
  check(assets->contains("cg\\MENU\\title_back01.png"),
        "Retail case-insensitive cg\\MENU\\title_back01.png must resolve");
  check(assets->contains("data\\CG_menu.png"),
        "Retail case-insensitive data\\CG_menu.png must resolve");
  check(assets->contains("sound\\12\\001400.ogg"),
        "Retail sound\\12\\001400.ogg must resolve to voice/12/001400.ogg");

  Ponscripter::PonscripterRuntime runtime(assets);
  for (const auto &[name, content] : scripts) {
    runtime.vm().appendScriptFile(name, content);
  }
  check(runtime.vm().start(), "Retail 00.utf + 01.utf must start cleanly");
  check(runtime.viewport().x == 800 && runtime.viewport().y == 600,
        "Retail SayaNoUta_PT-BR must run at 800x600 (;mode800 after BOM)");

  // Walk through *start -> *saya_config_chk -> *warning -> *logo -> *main_menu
  runtime.vm().setSkipMode(true);
  for (int i = 0;
       i < 40 &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitSelect &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitButton;
       ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
      runtime.vm().finishVideo();
  }

  if (runtime.vm().waitState() == Ponscripter::WaitState::WaitSelect) {
    // License agreement select ("^Concordo", *accepted)
    check(runtime.vm().selectChoice(0), "Accept license terms (*accepted)");
  }

  for (int i = 0;
       i < 40 && runtime.vm().waitState() != Ponscripter::WaitState::WaitButton;
       ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
      runtime.vm().finishVideo();
  }

  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitButton,
        "Must reach *main_menu title screen spbtn wait");
  check(runtime.vm().activeBgm() == "sound/s02.ogg",
        "Title screen BGM must be sound/s02.ogg");

  // Click New Game (button 1 -> *saya_start -> *grotesque -> *001)
  check(runtime.vm().selectButton(1), "Click New Game button (1)");
  for (int i = 0;
       i < 30 && runtime.vm().waitState() != Ponscripter::WaitState::WaitSelect;
       ++i) {
    runtime.vm().tick(1000.0);
  }
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSelect,
        "Must reach *grotesque filter select");
  check(runtime.vm().selectChoice(0), "Select default grotesque setting");

  // Step into scene *001 up to the first dialogue wait and verify Kotonoha
  // Scene Inspector, Translation Applet, Voice Dubbing, and Save/Load
  // round-trip!
  runtime.vm().setSkipMode(false);
  for (int step = 0;
       step < 400 &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitClickInline &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitClickPage;
       ++step) {
    runtime.vm().tick(1000.0);
  }
  check(!runtime.debugNodes().empty(),
        "Kotonoha Scene Inspector debugNodes() must be populated");
  check(runtime.translationRoute() >= 0,
        "Kotonoha Translation Applet translationRoute() must be >= 0");
  check(!runtime.translationRoutes().empty() &&
            !runtime.translationSources().empty(),
        "Kotonoha Translation Applet routes and sources must be populated");
  check(!runtime.activeTranslationKeys().empty(),
        "Active dialogue line must expose activeTranslationKeys()");
  check(!runtime.debugSceneAudio().empty(),
        "Scene *001 must expose voice dubbing lines via debugSceneAudio()");
  const auto firstVoice = runtime.debugSceneAudio().front();
  const auto origClip = runtime.debugOriginalAudio(firstVoice.id);
  check(origClip.has_value() && !origClip->data.empty(),
        "debugOriginalAudio() must load voice asset samples from arc.nsa");

  // Save & Load round-trip via Kotonoha saveGame / loadGame
  const std::string savedLabel = runtime.vm().currentLabel();
  const std::string savedText = runtime.dialogueText();
  const auto tmpSavePath =
      (std::filesystem::temp_directory_path() / "ponscr_test_save.ksav")
          .string();
  runtime.saveGame(tmpSavePath);
  runtime.debugJumpToNode("*005");
  check(runtime.vm().currentLabel() == "005", "Current label must be 005");
  runtime.loadGame(tmpSavePath);
  check(runtime.vm().currentLabel() == savedLabel &&
            runtime.dialogueText() == savedText,
        "loadGame() must restore exact saved scene label and dialogue text");
  std::error_code ec;
  std::filesystem::remove(tmpSavePath, ec);

  // Fast-forward through scenes *001..*010 up to Choice 1 (*select_01)
  runtime.vm().setSkipMode(true);
  for (int step = 0; step < 10000 && runtime.vm().waitState() !=
                                         Ponscripter::WaitState::WaitButton;
       ++step) {
    runtime.vm().tick(1000.0);
  }
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitButton,
        "Must reach Choice 1 (*select_01) spbtn wait");
  check(runtime.vm().variables().getNum("cgset_0") == 1,
        "%cgset_0 must be unlocked at *010");

  // Choose Option 2 ("Não preciso mais disso" -> *chapter_3)
  check(runtime.vm().selectButton(2), "Select Option 2 (*chapter_3)");
  runtime.vm().setSkipMode(true);
  for (int step = 0; step < 10000 && runtime.vm().waitState() !=
                                         Ponscripter::WaitState::WaitButton;
       ++step) {
    runtime.vm().tick(1000.0);
  }
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitButton,
        "Must reach Choice 2 (*select_02) spbtn wait");

  // Verify Ending 1 (*parting_end), Ending 2 (*badend), and Ending 3 (*madend)
  for (const char *endLbl : {"parting_end", "badend", "madend"}) {
    check(runtime.vm().jumpToLabel(endLbl), "Jump to ending label");
    for (int step = 0; step < 5000; ++step) {
      runtime.vm().setSkipMode(true);
      runtime.vm().tick(1000.0);
      if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
        runtime.vm().finishVideo();
      if (runtime.vm().currentLabel() == "start" ||
          runtime.vm().currentLabel() == "main_loop2")
        break;
    }
  }
  check(runtime.vm().variables().getNum("end1_clear") == 1 &&
            runtime.vm().variables().getNum("end2_clear") == 1 &&
            runtime.vm().variables().getNum("end3_clear") == 1,
        "All 3 endings (%end1_clear, %end2_clear, %end3_clear) must complete "
        "and unlock 100% extras");
}

template <typename T> T *findActiveScene(Kotonoha::Runtime &runtime) {
  for (auto *scene : runtime.GetActiveScenes())
    if (auto *typed = dynamic_cast<T *>(scene))
      return typed;
  return nullptr;
}

void runRetailMenuIntegrationTest(const std::string &sayaDir) {
  auto assets = std::make_shared<Ponscripter::PonscripterAssets>(sayaDir);
  Ponscripter::PonscripterRuntime runtime(assets);
  Kotonoha::Engine engine(runtime);
  struct ShutdownGuard {
    Ponscripter::PonscripterRuntime &runtime;
    ~ShutdownGuard() { runtime.shutdown(); }
  } guard{runtime};

  runtime.vm().setSkipMode(true);
  for (int i = 0;
       i < 80 &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitSelect &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitButton;
       ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
      runtime.vm().finishVideo();
  }
  if (runtime.vm().waitState() == Ponscripter::WaitState::WaitSelect)
    check(runtime.vm().selectChoice(0), "Accept retail license terms");

  for (int i = 0;
       i < 80 && runtime.vm().waitState() != Ponscripter::WaitState::WaitButton;
       ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
      runtime.vm().finishVideo();
  }
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitButton,
        "Retail Saya must reach the title-screen buttons");
  check(runtime.vm().selectButton(1), "Start a retail New Game");
  for (int i = 0;
       i < 80 && runtime.vm().waitState() != Ponscripter::WaitState::WaitSelect;
       ++i)
    runtime.vm().tick(1000.0);
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSelect,
        "Retail Saya must reach the grotesque settings select");
  check(runtime.vm().selectChoice(0), "Accept retail grotesque default");

  for (int i = 0; i < 400; ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitClickInline ||
        runtime.vm().waitState() == Ponscripter::WaitState::WaitClickPage)
      break;
  }
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitClickInline ||
            runtime.vm().waitState() == Ponscripter::WaitState::WaitClickPage,
        "Retail Saya must reach its first dialogue wait");

  check(runtime.vm().rightClick(), "Retail Saya right click must open rmenu");
  Kotonoha::InGameMenu *menu = findActiveScene<Kotonoha::InGameMenu>(runtime);
  check(menu && menu->IsOpen(),
        "Retail native rmenu must open the PONscripter InGameMenu");
  menu->close();

  runtime.vm().enterSystemCall("lookback");
  Kotonoha::VNBacklog *backlog = findActiveScene<Kotonoha::VNBacklog>(runtime);
  check(backlog && backlog->isVisible(),
        "Retail native lookback must open the PONscripter VNBacklog");
  backlog->close();
}

void runEngineMenuIntegrationTest() {
  auto assets = std::make_shared<Ponscripter::PonscripterAssets>();
  Ponscripter::PonscripterRuntime runtime(assets);
  Kotonoha::Engine engine(runtime);
  struct ShutdownGuard {
    Ponscripter::PonscripterRuntime &runtime;
    ~ShutdownGuard() { runtime.shutdown(); }
  } guard{runtime};

  const std::string script =
      "\xEF\xBB\xBF;mode800\n"
      "*define\n"
      "game\n"
      "rmenu \"Salvar\",save,\"Carregar\",load,\"Histórico\",lookback,"
      "\"Tela inicial\",reset\n"
      "*start\n"
      "setwindow3 60,70,34,20,20,20,0,4,0,1,1,#000000,40,50\n"
      "^Primeira página.@\n"
      "br\n"
      "^Segunda página.@\n"
      "br\n"
      "end\n";
  runtime.vm().appendScriptFile("00.utf", script);
  runtime.init(engine);

  for (int i = 0; i < 30 && runtime.vm().waitState() !=
                                Ponscripter::WaitState::WaitClickInline;
       ++i)
    runtime.vm().tick(1000.0);
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitClickInline,
        "Synthetic menu script must reach its first dialogue wait");

  const auto findScene = [&](auto *tag) -> decltype(tag) {
    for (auto *scene : runtime.GetActiveScenes()) {
      if (auto *typed = dynamic_cast<std::decay_t<decltype(tag)>>(scene))
        return typed;
    }
    return nullptr;
  };

  // Native right-click `rmenu` must open PONscripter's InGameMenu and keep the
  // VM blocked at WaitSystemCall until the engine menu closes.
  check(runtime.vm().rightClick(), "Right click must open native rmenu");
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSystemCall,
        "Native rmenu must block the VM");
  Kotonoha::InGameMenu *menu =
      findScene(static_cast<Kotonoha::InGameMenu *>(nullptr));
  check(menu && menu->IsOpen(),
        "Native rmenu must open the PONscripter InGameMenu scene");

  // The first native entry is Save: Enter must enter PONscripter's native
  // save system call and keep the native overlay active.
  SDL_Event enter{};
  enter.type = SDL_EVENT_KEY_DOWN;
  enter.key.key = SDLK_RETURN;
  enter.key.down = true;
  check(runtime.onEvent(enter), "Save entry must be handled by the menu");
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSystemCall &&
            runtime.vm().activeSystemCall() == "save",
        "Native rmenu Save must enter PONscripter's save system call");
  runtime.vm().finishSystemCall();

  // The third native entry is History: Down, Down, Enter must route to the
  // PONscripter VNBacklog override.
  check(runtime.vm().rightClick(), "Right click must reopen native rmenu");
  SDL_Event down{};
  down.type = SDL_EVENT_KEY_DOWN;
  down.key.key = SDLK_DOWN;
  down.key.down = true;
  check(runtime.onEvent(down) && runtime.onEvent(down),
        "History navigation must be handled by the menu");
  check(runtime.onEvent(enter), "History entry must be handled by the menu");
  Kotonoha::VNBacklog *backlog =
      findScene(static_cast<Kotonoha::VNBacklog *>(nullptr));
  check(backlog && backlog->isVisible(),
        "Native rmenu History must open the PONscripter VNBacklog scene");
  SDL_Event left{};
  left.type = SDL_EVENT_KEY_DOWN;
  left.key.key = SDLK_LEFT;
  left.key.down = true;
  check(runtime.onEvent(left),
        "PONscripter backlog must handle left-arrow navigation");
  SDL_Event right{};
  right.type = SDL_EVENT_KEY_DOWN;
  right.key.key = SDLK_RIGHT;
  right.key.down = true;
  check(runtime.onEvent(right),
        "PONscripter backlog must handle right-arrow navigation");
  for (int i = 0; i < 40; ++i) {
    check(runtime.onEvent(left),
          "PONscripter backlog left navigation must stay in bounds");
  }
  for (int i = 0; i < 40; ++i) {
    check(runtime.onEvent(right),
          "PONscripter backlog right navigation must stay in bounds");
  }
  backlog->close();
  check(runtime.vm().waitState() != Ponscripter::WaitState::WaitSystemCall,
        "Closing the PONscripter backlog must release the native rmenu wait");

  // Native `lookback` must open PONscripter's VNBacklog scene directly.
  runtime.vm().enterSystemCall("lookback");
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSystemCall,
        "Native lookback must block the VM");
  check(backlog->isVisible(),
        "Native lookback must open the PONscripter VNBacklog scene");
  backlog->close();
  check(
      runtime.vm().waitState() != Ponscripter::WaitState::WaitSystemCall,
      "Closing the PONscripter backlog must release the native lookback wait");

  // Native `save` must enter PONscripter's save system call directly.
  runtime.vm().enterSystemCall("save");
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitSystemCall &&
            runtime.vm().activeSystemCall() == "save",
        "Native save must use the PONscripter save system call");
  runtime.vm().finishSystemCall();
}

void runEngineExitTest() {
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  auto assets = std::make_shared<Ponscripter::PonscripterAssets>();
  Ponscripter::PonscripterRuntime runtime(assets);
  Kotonoha::Engine engine(runtime);
  struct ShutdownGuard {
    Ponscripter::PonscripterRuntime &runtime;
    ~ShutdownGuard() { runtime.shutdown(); }
  } guard{runtime};

  const std::string script = "\xEF\xBB\xBF;mode800\n"
                             "*define\n"
                             "game\n"
                             "*start\n"
                             "delay 10\n"
                             "end\n";
  runtime.vm().clear();
  runtime.vm().appendScriptFile("00.utf", script);
  check(runtime.vm().start(), "Exit test script must start");

  bool sawQuitEvent = false;
  for (int i = 0; i < 60 && !sawQuitEvent; ++i) {
    check(engine.Iterate() == SDL_APP_CONTINUE,
          "Engine iterate before quit must succeed");
    SDL_Event ev{};
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        check(engine.Event(&ev) == SDL_APP_SUCCESS,
              "Engine::Event must return SDL_APP_SUCCESS on SDL_EVENT_QUIT");
        sawQuitEvent = true;
        break;
      }
    }
  }
  check(runtime.vm().isHalted(), "Exit test VM must reach Halted state");
  check(sawQuitEvent,
        "Halting Ponscripter VM must push SDL_EVENT_QUIT to exit cleanly");
}

void runRetailExitTest(const std::string &sayaDir) {
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  auto assets = std::make_shared<Ponscripter::PonscripterAssets>(sayaDir);
  Ponscripter::PonscripterRuntime runtime(assets);
  Kotonoha::EngineOptions opts;
  opts.assets = assets;
  Kotonoha::Engine engine(runtime, std::move(opts));
  struct ShutdownGuard {
    Ponscripter::PonscripterRuntime &runtime;
    ~ShutdownGuard() { runtime.shutdown(); }
  } guard{runtime};

  runtime.vm().setSkipMode(true);
  for (int i = 0;
       i < 80 &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitSelect &&
       runtime.vm().waitState() != Ponscripter::WaitState::WaitButton;
       ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
      runtime.vm().finishVideo();
  }
  if (runtime.vm().waitState() == Ponscripter::WaitState::WaitSelect)
    check(runtime.vm().selectChoice(0), "Accept retail license terms");

  for (int i = 0;
       i < 80 && runtime.vm().waitState() != Ponscripter::WaitState::WaitButton;
       ++i) {
    runtime.vm().tick(1000.0);
    if (runtime.vm().waitState() == Ponscripter::WaitState::WaitVideo)
      runtime.vm().finishVideo();
  }
  check(runtime.vm().waitState() == Ponscripter::WaitState::WaitButton,
        "Retail Saya exit test must reach title-screen buttons");

  // Button 6 is "Sair" -> *saya_exit -> delay 1000 -> end
  check(runtime.vm().selectButton(6), "Click retail Saya Exit button (6)");

  bool sawQuitEvent = false;
  for (int i = 0; i < 120 && !sawQuitEvent; ++i) {
    runtime.advanceStoryTime(0.1);
    check(engine.Iterate() == SDL_APP_CONTINUE,
          "Engine iterate during *saya_exit must succeed");
    SDL_Event ev{};
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        check(engine.Event(&ev) == SDL_APP_SUCCESS,
              "Engine::Event must return SDL_APP_SUCCESS on Saya exit");
        sawQuitEvent = true;
        break;
      }
    }
  }
  check(runtime.vm().isHalted(),
        "Retail Saya *saya_exit must halt the Ponscripter VM");
  check(sawQuitEvent,
        "Retail Saya *saya_exit must push SDL_EVENT_QUIT without hanging");
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (SDL_getenv("SAYA_RETAIL_MENU_ONLY")) {
      runRetailMenuIntegrationTest(argv[1]);
      std::cout << "PonscripterRuntimeTests passed\n";
      return 0;
    }
    runSyntheticTest();
    runEngineMenuIntegrationTest();
    runEngineExitTest();
    if (argc > 1 && std::filesystem::exists(argv[1])) {
      runRetailSayaAudit(argv[1]);
      runRetailMenuIntegrationTest(argv[1]);
      runRetailExitTest(argv[1]);
    }
    std::cout << "PonscripterRuntimeTests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "PonscripterRuntimeTests failed: " << e.what() << '\n';
    return 1;
  }
}
