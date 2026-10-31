#pragma once

#include <Kotonoha/core/Settings.hpp>
#include <memory>
#include <vector>

namespace Kotonoha {
class Engine;
namespace ImGuiTheme { class ShaderFilePicker; }

class SettingsMenu {
  Engine &engine;
  EngineSettings draft;
  bool visible = false;
  int selected = 0;
  std::string error;
  std::unique_ptr<ImGuiTheme::ShaderFilePicker> shaderPicker;
  std::vector<int> rows() const;
  void activate(int index, int direction = 1);

public:
  explicit SettingsMenu(Engine &engine);
  ~SettingsMenu();
  void open();
  void close() { visible = false; }
  bool isOpen() const { return visible; }
  void onEvent(EventRef event);
  void render(RendererRef renderer);
};
} // namespace Kotonoha
