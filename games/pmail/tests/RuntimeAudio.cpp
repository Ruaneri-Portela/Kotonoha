#include "PmailRuntime.hpp"
#include <Kotonoha/components/TextInputDialog.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PMAIL_SKIP_INTRO", "1",
                             true);
  auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(argv[1]);
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.launcher = false;
  options.persistSettings = false;
  Kotonoha::PmailPort::PmailRuntime runtime(argv[1], assets);
  Kotonoha::Engine engine(runtime, options);
  bool voice = false;
  try {
    const auto began = SDL_GetTicks();
    for (int frame = 0; SDL_GetTicks() - began < 45000; ++frame) {
      if (frame >= 120 && frame % 8 == 0) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_RETURN;
        runtime.onEvent(event);
      }
      if (engine.Iterate() != SDL_APP_CONTINUE)
        break;
      for (const auto &audio : runtime.debugSceneAudio())
        if (audio.id.rfind("voice/", 0) == 0 && !runtime.dialogueText().empty())
          voice = true;
      if (voice)
        break;
      SDL_Delay(16);
    }
    std::cout << runtime.debugNode()
              << "\nVoice and dialogue reached: " << voice << '\n';
    if (!voice)
      throw std::runtime_error("Did not reach voiced dialogue");
    const auto savedNode = runtime.debugNode();
    const auto savedText = runtime.dialogueText();
    const auto path =
        std::filesystem::temp_directory_path() /
        ("pmail-runtime-" + std::to_string(SDL_GetTicksNS()) + ".vn");
    runtime.saveGame(path.string());
    runtime.returnToTitle();
    if (runtime.GetState() != Kotonoha::GameState::Menu)
      throw std::runtime_error("Return to title did not change engine state");
    runtime.loadGame(path.string());
    std::filesystem::remove(path);
    if (runtime.debugNode() != savedNode ||
        runtime.dialogueText() != savedText ||
        runtime.GetState() != Kotonoha::GameState::Gameplay)
      throw std::runtime_error("Runtime save/load changed dialogue or node");
    if (engine.Iterate() != SDL_APP_CONTINUE)
      throw std::runtime_error("Could not render restored scene");
    runtime.openSaves(false);
    if (engine.Iterate() != SDL_APP_CONTINUE)
      throw std::runtime_error("Could not render save menu");
    // New save opens a separate text modal: it must be visible and receive
    // keyboard events, not only the underlying save list.
    SDL_Event enter{};
    enter.type = SDL_EVENT_KEY_DOWN;
    enter.key.key = SDLK_RETURN;
    runtime.onEvent(enter);
    bool foundNameEditor = false;
    for (auto *scene : runtime.GetActiveScenes())
      foundNameEditor |=
          dynamic_cast<Kotonoha::TextInputDialog *>(scene) != nullptr;
    if (!foundNameEditor)
      throw std::runtime_error("New save did not create its name editor");
    if (engine.Iterate() != SDL_APP_CONTINUE)
      throw std::runtime_error("Could not render/update save name modal");
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    runtime.onEvent(escape); // Cancel modal, leave save list open.
    for (auto *scene : runtime.GetActiveScenes())
      if (dynamic_cast<Kotonoha::TextInputDialog *>(scene))
        throw std::runtime_error("Cancelled save editor remained active");
    runtime.onEvent(escape);  // Close save list completely.
    runtime.openSaves(false); // Must re-register after VNSaveMenu::close().
    bool reopened = false;
    for (auto *scene : runtime.GetActiveScenes())
      if (auto *menu = dynamic_cast<Kotonoha::VNSaveMenu *>(scene))
        reopened |= menu->isOpen();
    if (!reopened)
      throw std::runtime_error("Save menu cannot reopen after closing");
    if (engine.Iterate() != SDL_APP_CONTINUE)
      throw std::runtime_error("Reopened save list could not render");
    std::cout
        << "Runtime save/load, name-entry modal and menu reopening passed.\n";
    runtime.shutdown();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    runtime.shutdown();
    return 1;
  }
}
