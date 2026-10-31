#include "SnowRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto assets = std::make_shared<SnowRadish::SnowAssets>(argv[1]);
    SnowRadish::SnowRuntime runtime(assets);
    Kotonoha::EngineOptions options;
    options.assets = assets;
    Kotonoha::Engine engine(runtime, options);
    runtime.SetState(Kotonoha::GameState::Gameplay);
    runtime.init(engine);
    auto *session = runtime.Session();
    if (!session || !session->Start())
      throw std::runtime_error("Cannot start native story");
    const auto nodes = engine.debugNodes();
    if (!session || nodes.empty() || !session->Playing())
      throw std::runtime_error("Missing native debug nodes");
    const auto script = session->Story()->currentScript();
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
    if (session->Story()->currentScript() != script || !session->Playing())
      throw std::runtime_error("Native debug jump did not execute");
    rejected = false;
    try {
      session->Serialize();
    } catch (const std::exception &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error(
          "Replay saves after debug jumps must be rejected");
    runtime.Shutdown();
    std::cout << "Snow node debug tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
