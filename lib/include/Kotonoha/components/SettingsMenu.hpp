#pragma once
#include <Kotonoha/components/TouchActivation.hpp>

#include <Kotonoha/components/SelectBox.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <memory>
#include <vector>

namespace Kotonoha {
class Engine;
namespace ImGuiTheme {
class ShaderFilePicker;
}

class SettingsMenu {
  TouchActivation touchActivation;
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
  // The input page only exists while a gamepad is connected so the option is
  // never shown for keyboard/mouse-only setups.
  int pageCount() const;
  std::vector<int> rows() const;
  void activate(int index, int direction = 1);
  // Applies only the audio fields to the running engine (no disk write).
  void applyAudioDraft();
  // Persists the live engine settings (used when audio changes are committed).
  void persistAudio();

public:
  explicit SettingsMenu(Engine &engine);
  ~SettingsMenu();
  void open();
  void close();
  bool isOpen() const { return visible; }
  bool onEvent(EventRef event);
  void render(RendererRef renderer);
};
} // namespace Kotonoha
