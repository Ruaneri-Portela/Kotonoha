// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterRuntime.hpp>

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

} // namespace

int main(int argc, char **argv) {
  try {
    runSyntheticTest();
    if (argc > 1 && std::filesystem::exists(argv[1])) {
      runRetailSayaAudit(argv[1]);
    }
    std::cout << "PonscripterRuntimeTests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "PonscripterRuntimeTests failed: " << e.what() << '\n';
    return 1;
  }
}
