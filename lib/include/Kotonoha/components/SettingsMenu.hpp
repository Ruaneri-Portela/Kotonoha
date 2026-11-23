#pragma once
#include <Kotonoha/components/TouchActivation.hpp>

#include <Kotonoha/components/SelectBox.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <memory>
#include <utility>
#include <vector>

namespace Kotonoha {
class Engine;
namespace ImGuiTheme {
class ShaderFilePicker;
}

class SettingsMenu {
public:
  enum class Page {
    Display,
    Audio,
    Text,
    Input,
    Basic,
    TextPacing,
    TextColors,
    Sound,
    Voice,
    Keyboard,
    Mouse,
    Touch,
    System
  };
  class Skin {
  public:
    virtual ~Skin() = default;
    virtual const std::vector<Page> &pages() const = 0;
    virtual std::vector<std::string> audioChannels() const { return {}; }
    virtual SDL_FRect tabArea(int index) const = 0;
    virtual void renderFrame(RendererRef renderer, Engine &engine, int tab,
                             const EngineSettings &draft) = 0;
    virtual bool renderSlider(RendererRef, SDL_FRect, float) { return false; }
  };

private:
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
  std::unique_ptr<Skin> skin;
  // The input page only exists while a gamepad is connected so the option is
  // never shown for keyboard/mouse-only setups.
  int pageCount() const;
  Page currentPage() const;
  SDL_FRect tabArea(int index) const;
  SDL_FRect rowArea(int index) const;
  SDL_FRect sliderArea(SDL_FRect row) const;
  std::vector<int> rows() const;
  void activate(int index, int direction = 1);
  // Applies only the audio fields to the running engine (no disk write).
  void applyAudioDraft();
  void applyTextSpeedDraft();
  // Persists the live engine settings (used when audio changes are committed).
  void persistAudio();

public:
  explicit SettingsMenu(Engine &engine);
  ~SettingsMenu();
  // The appearance can change per port. Native input, drafts and persistence
  // always remain owned by this menu.
  void setSkin(std::unique_ptr<Skin> value) { skin = std::move(value); }
  void open();
  void close();
  bool isOpen() const { return visible; }
  bool onEvent(EventRef event);
  void render(RendererRef renderer);
};
} // namespace Kotonoha
