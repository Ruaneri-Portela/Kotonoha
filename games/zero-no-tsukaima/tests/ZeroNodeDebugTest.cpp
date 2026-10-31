#include "ZeroRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto disc = std::make_shared<ZeroNoTsukaima::DiscAssetProvider>(argv[1]);
    ZeroNoTsukaima::ZeroRuntime runtime(disc);
    Kotonoha::EngineOptions options;
    options.assets = disc;
    Kotonoha::Engine engine(runtime, options);
    runtime.SetState(Kotonoha::GameState::Gameplay);
    runtime.init(engine);
    auto *session = runtime.Session();
    if (!session || !session->Start())
      throw std::runtime_error("Cannot start native story");
    const auto nodes = engine.debugNodes();
    if (!session || nodes.empty() || !session->Playing())
      throw std::runtime_error("Missing native debug nodes");
    const auto script = session->Story()->ScriptName();
    const auto previous = runtime.debugNode();
    bool rejected = false;
    try {
      engine.debugJumpToNode("missing-debug-script");
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected || runtime.debugNode() != previous)
      throw std::runtime_error("Invalid jumps must preserve native state");
    engine.debugJumpToNode(script);
    if (session->Story()->ScriptName() != script || !session->Playing())
      throw std::runtime_error("Native debug jump did not execute");
    const auto save = std::filesystem::temp_directory_path() /
                      ("zero-node-debug-" +
                       std::to_string(SDL_GetPerformanceCounter()) + ".sav");
    if (session->SaveTo(save.string())) {
      std::filesystem::remove(save);
      throw std::runtime_error(
          "Replay saves after debug jumps must be rejected");
    }
    runtime.Shutdown();
    std::cout << "Zero node debug tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
