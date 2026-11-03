#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
using namespace Kotonoha;
class NativeRuntime : public Runtime {
public:
  std::string active = "line 110";
  NativeRuntime()
      : Runtime("DebugTranslationTest", "1", "debug-translation-test") {}
  int translationRoute() const override { return 12; }
  std::vector<int> translationRoutes() const override { return {12}; }
  std::vector<std::string> activeTranslationKeys() const override {
    return {active};
  }
  std::vector<std::string> translationTexts(int) const override {
    std::vector<std::string> texts;
    for (int i = 0; i < 120; ++i)
      texts.push_back("line " + std::to_string(i));
    return texts;
  }
};
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
ImGuiWindow *entries() {
  for (auto *window : ImGui::GetCurrentContext()->Windows)
    if (std::string(window->Name).find("StringEntries") != std::string::npos)
      return window;
  return nullptr;
}
int main() {
  try {
    NativeRuntime game;
    EngineOptions options;
    options.persistSettings = false;
    Engine engine(game, options);
    game.SetState(GameState::Gameplay);
    engine.GetDebug().trackActiveTranslation();
    for (int i = 0; i < 5; ++i)
      engine.Iterate();
    auto *list = entries();
    require(list != nullptr, "Live editor lists native scene strings");
    const float firstScroll = list->Scroll.y;
    require(firstScroll > 1000,
            "Track Active Node scrolls to the exact late scene string");
    game.active = "line 3";
    for (int i = 0; i < 4; ++i)
      engine.Iterate();
    require(entries()->Scroll.y < firstScroll / 2,
            "Following tracks another string within the same scene");
    game.active = "line 110";
    for (int i = 0; i < 4; ++i)
      engine.Iterate();
    require(entries()->Scroll.y > firstScroll / 2,
            "Following scrolls back to the current original key");
    std::cout << "Native translation tracking and scrolling passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
