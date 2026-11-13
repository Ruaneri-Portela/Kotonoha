#include "PmailRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <cstring>

using namespace Kotonoha::PmailPort;
namespace {
int bootFrame(const PmailRuntime &runtime) {
  for (const auto &node : runtime.debugNodes())
    for (const auto &id : node.assets) {
      constexpr const char *prefix = "graphic/osboot.anim#";
      if (id.rfind(prefix, 0) == 0) return std::stoi(id.substr(std::strlen(prefix)));
    }
  return -1;
}
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PMAIL_SKIP_INTRO", "1", true);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PMAIL_TRACE", "1", true);
  auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(argv[1]);
  Kotonoha::EngineOptions options;
  options.assets = assets; options.launcher = false; options.persistSettings = false;
  PmailRuntime runtime(argv[1], assets);
  Kotonoha::Engine engine(runtime, options);
  const auto checkpoint = std::filesystem::temp_directory_path() /
      ("pmail-before-boot-" + std::to_string(SDL_GetTicksNS()) + ".vn");
  try {
    std::set<int> seen;
    std::string lastText;
    bool saved = false, replaying = false, captured = false;
    const auto began = SDL_GetTicks();
    uint64_t firstFrameAt = 0;
    while (SDL_GetTicks() - began < 150000) {
      if (bootFrame(runtime) < 0) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN; event.key.key = SDLK_RETURN;
        event.key.windowID = SDL_GetWindowID(&engine.GetWindow());
        require(engine.Event(&event) == SDL_APP_CONTINUE, "Input ended game before OS boot");
      }
      require(engine.Iterate() == SDL_APP_CONTINUE, "Runtime exited before OS boot completed");
      const auto text = runtime.dialogueText();
      if (!replaying && seen.empty() && bootFrame(runtime) < 0 && !text.empty() && text != lastText &&
          runtime.GetState() == Kotonoha::GameState::Gameplay) {
        runtime.saveGame(checkpoint.string()); saved = true; lastText = text;
      }
      const int frame = bootFrame(runtime);
      if (frame >= 0) {
        if (seen.empty()) { firstFrameAt = SDL_GetTicks(); std::cout << "Boot began: " << runtime.debugNode() << std::endl; }
        seen.insert(frame);
        if (frame == 40 && !captured) {
          auto &renderer = engine.GetRenderer();
          auto surface = Kotonoha::SurfacePtr(SDL_RenderReadPixels(&renderer, nullptr));
          require(bool(surface), "Cannot capture boot from engine renderer");
          const auto imagePath = std::filesystem::temp_directory_path() / "pmail-runtime-boot.bmp";
          require(SDL_SaveBMP(surface.get(), imagePath.string().c_str()), "Cannot save boot capture");
          std::cout << "Boot capture: " << imagePath << std::endl;
          captured = true;
        }
      } else if (!seen.empty()) {
        require(seen.size() == 80 && *seen.begin() == 0 && *seen.rbegin() == 79,
                "Real runtime skipped an OSBOOT frame");
        require(SDL_GetTicks() - firstFrameAt >= 4250, "Real runtime ran OSBOOT too fast");
        require(saved && captured, "Boot did not provide a rendered frame/checkpoint");
        if (replaying) break;
        runtime.returnToTitle(); runtime.loadGame(checkpoint.string());
        seen.clear(); replaying = true;
      }
      SDL_Delay(8);
    }
    require(replaying && seen.size() == 80, "Did not complete both script-driven boot calls");
    std::filesystem::remove(checkpoint);
    runtime.shutdown();
    std::cout << "Real runtime rendered all 80 OSBOOT frames on both script calls, including reload.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; runtime.shutdown(); return 1;
  }
}
