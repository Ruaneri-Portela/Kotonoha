#include "PmailRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <iostream>
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PMAIL_SKIP_INTRO", "1", true);
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
      if (engine.Iterate() != SDL_APP_CONTINUE) break;
      for (const auto &audio : runtime.debugSceneAudio())
        if (audio.id.rfind("voice/", 0) == 0 && !runtime.dialogueText().empty()) voice = true;
      if (voice) break;
      SDL_Delay(16);
    }
    std::cout << runtime.debugNode() << "\nVoice and dialogue reached: " << voice << '\n';
    runtime.shutdown();
    return voice ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; runtime.shutdown(); return 1;
  }
}
