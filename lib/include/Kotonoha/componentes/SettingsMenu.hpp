#pragma once

#include <Kotonoha/componentes/SelectBox.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <memory>
#include <vector>

namespace Kotonoha {
class Engine;
namespace ImGuiTheme {
class ShaderFilePicker;
}

class SettingsMenu {
  Engine &engine;
  EngineSettings draft;
  bool visible = false;
  int selected = 0, page = 0, first = 0;
  int slider = -1;
  std::vector<std::string> channels;
  std::vector<std::string> fonts;
  SelectBox selectBox;
  ScrollGesture scrolling;
  float scrollOffset = 0, scrollVelocity = 0;
  bool scrollFollow = false;
  std::string error;
  std::unique_ptr<ImGuiTheme::ShaderFilePicker> shaderPicker;
  std::vector<int> rows() const;
  void activate(int index, int direction = 1);

public:
  explicit SettingsMenu(Engine &engine);
  ~SettingsMenu();
  void open();
  void close() {
    visible = false;
    selectBox.close();
    slider = -1;
  }
  bool isOpen() const { return visible; }
  void onEvent(EventRef event);
  void render(RendererRef renderer);
};
} // namespace Kotonoha
