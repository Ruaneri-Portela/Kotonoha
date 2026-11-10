#include "SummerRuntime.hpp"
#include "SummerBacklog.hpp"
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/ImageButtonBar.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/TextInputForm.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SummerRadish/Map.hpp>
#include <SummerRadish/Native.hpp>
#include <SummerRadish/Script.hpp>
#include <SummerRadish/Transition.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace SummerRadish {
using namespace Kotonoha;
namespace {
class SummerText final : public VNTextBox {
  SummerAssets &assets;
  std::unique_ptr<ClickMap> controlMap;
  void renderFrame(RendererRef r) override {
    // The normal frame is already composited by the stage. Overlay the
    // original atlas states without rebuilding/upscaling the whole canvas.
    if (assets.containsAsset("GRAPHIC/FRAME/MESSFRAME.TEXB")) {
      auto &atlas = assets.texture(r, "GRAPHIC/FRAME/MESSFRAME.TEXB");
      for (int i = 0; i < 6; ++i) {
        const int state = controls.state(i);
        const bool active = (i == 0 && skipActive && skipActive()) ||
                            (i == 2 && autoActive && autoActive());
        if (!state && !active)
          continue;
        SDL_FRect source{};
        if (i == 0 || i == 2)
          source = {float((active ? 192 : 0) + (state ? 96 : 0)),
                    float(i == 0 ? 250 : 268), 96, 18};
        else if (i == 1 || i == 3)
          source = {480, float(i == 1 ? 250 : 268), 96, 18};
        else if (i == 4)
          source = {594, 250, 18, 18};
        else
          source = {float(612 + state * 46), 224, 46, 62};
        RenderUpscaledTexture(&r, &atlas, &source, &controls.buttons[i].area);
      }
    }
  }
  void renderSpeaker(RendererRef r) override {
    engine.GetVNUI().text(r, speakerName, 24, 400, 650,
                          engine.GetSettings().vnTextColorOverride
                              ? engine.GetSettings().vnSpeakerColor
                              : SDL_Color{240, 240, 245, 255});
  }
  void renderDialogue(RendererRef r) override {
    auto measure = [&](const std::string &value) {
      return engine.GetVNUI().textSize(value, 10000).x;
    };
    const auto lines = VNTextBoxUtility::RevealLines(
        fullDialogue, visibleDialogue.size(), measure, 710);
    for (std::size_t i = 0; i < lines.size(); ++i)
      engine.GetVNUI().text(r, lines[i], 20, 444 + i * 28, 710,
                            engine.GetSettings().vnTextColorOverride
                                ? engine.GetSettings().vnTextColor
                                : SDL_Color{240, 240, 245, 255});
  }
  void renderIndicator(RendererRef r) override {
    if (assets.containsAsset("GRAPHIC/FRAME/MESSFRAME.TEXB")) {
      auto &atlas = assets.texture(r, "GRAPHIC/FRAME/MESSFRAME.TEXB");
      SDL_FRect source{float(256 + ((SDL_GetTicks() / 200) % 4) * 16), 210, 16,
                       40};
      SDL_FRect target{719, 526, 16, 40};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
    }
  }

public:
  ImageButtonBar controls;
  bool concealed = false;
  std::function<bool()> autoActive, skipActive;
  void update() override {
    if (concealed)
      clock.reset();
    else
      VNTextBox::update();
  }
  void render(RendererRef r, float scale) override {
    if (!concealed)
      VNTextBox::render(r, scale);
  }
  SummerText(Engine &e, SummerAssets &a) : VNTextBox(e, "", "", 40), assets(a) {
    hide();
    if (auto cmap = assets.tryReadAsset("GRAPHIC/FRAME/MESSFRAME.CMAP"))
      controlMap = std::make_unique<ClickMap>(std::move(*cmap));
    controls.hitTest = [this](SDL_FPoint p) {
      const int id =
          controlMap ? controlMap->value(int(p.x), int(p.y) - 391) : -1;
      return id >= 1 && id <= 6 ? id - 1 : -1;
    };
    controls.buttons = {{{417, 576, 96, 18}, {}}, {{507, 576, 96, 18}, {}},
                        {{597, 576, 96, 18}, {}}, {{687, 576, 96, 18}, {}},
                        {{778, 577, 18, 18}, {}}, {{750, 515, 46, 62}, {}}};
  }
  bool designSpace() const override { return false; }
  bool handleEvent(EventRef e) override {
    return isVisible() && !concealed && controls.handle(e);
  }
};
class SummerChoice final : public ChoiceQuestionScene {
  SummerAssets &assets;
  std::vector<bool> readStates;
  bool pointerFocus = false, keyboardFocus = false;
  void layout() override {
    if (nativeMode >= 0) {
      ChoiceQuestionScene::layout();
      return;
    }
    float width = 16;
    for (const auto &option : options)
      width = std::max(
          width, engine.GetUI().measureSized(option.label, 10000, 28).x + 16);
    width = std::min(736.0f, width);
    const float left = (800 - width) / 2 - 32;
    float y = 300 - (50 * options.size()) / 2.0f + 4 - scroll;
    for (auto &option : options) {
      option.hitBox = {left, y, width + 64, 42};
      y += 50;
    }
  }
  void renderOption(RendererRef r, std::size_t index, bool focused) override {
    focused = focused && (pointerFocus || keyboardFocus);
    const auto &rect = options[index].hitBox;
    const bool read = index < readStates.size() && readStates[index];
    const float start = (read ? 160 : 0) + (focused ? 80 : 0);
    if (assets.containsAsset("GRAPHIC/FRAME/SELECT.TEXB")) {
      auto &atlas = assets.texture(r, "GRAPHIC/FRAME/SELECT.TEXB");
      SDL_FRect source{start, 0, 32, 42}, target{rect.x, rect.y, 32, 42};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
      source = {start + 32, 0, 16, 42};
      target = {rect.x + 32, rect.y, rect.w - 64, 42};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
      source = {start + 48, 0, 32, 42};
      target = {rect.x + rect.w - 32, rect.y, 32, 42};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
    }
    const auto size =
        engine.GetUI().measureSized(options[index].label, rect.w - 16, 28);
    const SDL_Color color = read ? (focused ? SDL_Color{255, 240, 128, 255}
                                            : SDL_Color{204, 192, 96, 255})
                                 : (focused ? SDL_Color{240, 240, 240, 255}
                                            : SDL_Color{192, 192, 192, 255});
    engine.GetUI().textSized(
        r, options[index].label, rect.x + (rect.w - size.x) / 2,
        rect.y + (42 - size.y) / 2, rect.w - 16, 28, color);
  }
  SDL_FRect listArea() const override { return {0, 0, 800, 600}; }
  void renderPanel(RendererRef r) override {
    if (!questionText.empty()) {
      engine.GetUI().panel(r, {60, 70, 680, 65});
      engine.GetUI().text(r, questionText, 80, 85, 640);
    }
  }
  std::unique_ptr<ClickMap> hitmap;
  int nativeMode = -1, focus = -1, uniformPhase = 0, uniformIndex = 0;
  float oldX = -100, oldY = -100;

public:
  std::function<void(const std::string &)> nativeSound;
  explicit SummerChoice(Engine &e, SummerAssets &a)
      : ChoiceQuestionScene(e, "", {}, nullptr), assets(a) {}
  void setReadStates(std::vector<bool> states) {
    readStates = std::move(states);
  }
  bool designSpace() const override { return false; }
  int mode() const { return active ? nativeMode : -1; }
  int focused() const { return focus; }
  void hide() override {
    ChoiceQuestionScene::hide();
    nativeMode = -1;
    hitmap.reset();
  }
  void setQuestion(const std::string &q, const std::vector<ChoiceOption> &o,
                   std::function<void(int, const std::string &)> cb) override {
    nativeMode = -1;
    readStates.clear();
    pointerFocus = keyboardFocus = false;
    hitmap.reset();
    ChoiceQuestionScene::setQuestion(q, o, std::move(cb));
  }
  void setNative(int mode, SummerAssets &assets,
                 std::function<void(int, const std::string &)> cb) {
    nativeMode = mode;
    focus = mode == 3 ? 0 : -1;
    uniformPhase = 0;
    oldX = oldY = -100;
    const auto id =
        mode == 3 ? "GRAPHIC/UNIFORM/UNICHOICE.CMAP" : mapLayout(mode).hitmap;
    hitmap = std::make_unique<ClickMap>(assets.read(id));
    std::vector<ChoiceOption> list;
    if (mode == 3)
      for (int i = 0; i < 3; ++i)
        list.push_back({std::to_string(i), i});
    else
      for (const auto &location : mapLayout(mode).locations)
        list.push_back(
            {mapCharacterVariable(location.result), location.result});
    ChoiceQuestionScene::setQuestion("", list, std::move(cb));
    if (mode == 3 && nativeSound)
      nativeSound("systemmsg_0000");
  }
  std::string nativeBackground() const {
    if (nativeMode == 3) {
      if (uniformPhase) {
        const std::string kakunin = std::string("GRAPHIC/UNIFORM/KAKUNIN_") +
                                    char('A' + uniformIndex) + ".CGD";
        if (assets.containsAsset(kakunin))
          return kakunin;
        return "GRAPHIC/UNIFORM/UNICHOICE.CGD";
      }
      return "GRAPHIC/UNIFORM/UNICHOICE.CGD";
    }
    return nativeMode >= 0 ? mapLayout(nativeMode).background : "";
  }
  void compose(Image &canvas,
               const std::function<const Image &(const std::string &)> &image,
               const Machine &vm) const {
    if (!active || nativeMode < 0)
      return;
    if (nativeMode < 3) {
      const auto &layout = mapLayout(nativeMode);
      if (focus >= 0 && focus < int(layout.locations.size())) {
        const auto &item = layout.locations[focus];
        compositeImageRegion(canvas, image(layout.texture), item.position.left,
                             item.position.top, item.source.left,
                             item.source.top,
                             item.source.right - item.source.left,
                             item.source.bottom - item.source.top);
      }
      for (auto &item : layout.locations) {
        auto v = vm.variables.find("@" + mapCharacterVariable(item.result));
        int actor = v == vm.variables.end() ? 0 : int(number(v->second));
        int face = mapFaceIndex(nativeMode,
                                size_t(&item - layout.locations.data()), actor);
        if (face >= 0 && face < 8)
          compositeImageRegion(canvas, image("GRAPHIC/MAP/FACE.TEXB"),
                               item.face.x, item.face.y, 0, face * 62, 98, 62);
      }
    } else if (uniformPhase == 0) {
      if (focus >= 0) {
        static const int x[3] = {220, 20, 420};
        compositeImageRegion(canvas, image("GRAPHIC/UNIFORM/UNICHOICE.TEXB"),
                             x[focus], 52, focus * 362, 0, 361, 547);
      }
    } else if (uniformPhase == 2) {
      const std::string kakuninTex = "GRAPHIC/UNIFORM/KAKUNIN.TEXB";
      if (assets.containsAsset(kakuninTex)) {
        const auto &texture = image(kakuninTex);
        compositeImageRegion(canvas, texture, 208, 362, 0, 0, 386, 90);
        if (focus >= 0)
          compositeImageRegion(canvas, texture, focus == 0 ? 288 : 414, 413,
                               focus * 100, 90, 100, 28);
      }
    }
  }
  bool onEvent(EventRef input) override {
    const auto e = MapControllerEvent(input);
    if (nativeMode < 0) {
      if (e.type == SDL_EVENT_MOUSE_MOTION && active) {
        layout();
        pointerFocus = false;
        keyboardFocus = false;
        SDL_FPoint point{e.motion.x, e.motion.y};
        for (std::size_t i = 0; i < options.size(); ++i)
          if (SDL_PointInRectFloat(&point, &options[i].hitBox)) {
            selected = i;
            pointerFocus = true;
          }
      }
      if (e.type == SDL_EVENT_KEY_DOWN)
        keyboardFocus = true;
      return ChoiceQuestionScene::onEvent(e);
    }
    if (BackAction(engine, e)) {
      if (nativeMode == 3 && uniformPhase) {
        uniformPhase = 0;
        focus = uniformIndex;
      } else
        openMenu();
      return true;
    }
    if (!active)
      return false;
    bool confirm = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                   (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE);
    bool mouse = e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                 e.button.button == SDL_BUTTON_LEFT;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_ESCAPE) {
        if (nativeMode == 3 && uniformPhase) {
          uniformPhase = 0;
          focus = uniformIndex;
          return true;
        }
        openMenu();
        return true;
      }
      int direction = e.key.key == SDLK_UP      ? 0
                      : e.key.key == SDLK_DOWN  ? 1
                      : e.key.key == SDLK_LEFT  ? 2
                      : e.key.key == SDLK_RIGHT ? 3
                                                : -1;
      if (direction >= 0) {
        if (nativeMode < 3)
          focus = mapNavigate(nativeMode, focus, direction);
        else if (uniformPhase == 2)
          focus = focus == 0 ? 1 : 0;
        else if (uniformPhase == 0) {
          static const int up[4] = {2, 1, 2, 0}, down[4] = {1, 2, 0, 1};
          focus = (direction == 0 || direction == 2 ? up : down)[focus + 1];
        }
      }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION &&
        (std::abs(e.motion.x - oldX) > 3 || std::abs(e.motion.y - oldY) > 3)) {
      if (nativeMode == 3 && uniformPhase == 2)
        focus = (e.motion.y >= 413 && e.motion.y < 441)
                    ? (e.motion.x >= 288 && e.motion.x < 388   ? 0
                       : e.motion.x >= 414 && e.motion.x < 514 ? 1
                                                               : -1)
                    : -1;
      else if (!uniformPhase)
        focus = hitmap->locationIndex(int(e.motion.x), int(e.motion.y),
                                      options.size());
      oldX = e.motion.x;
      oldY = e.motion.y;
    }
    if (mouse) {
      if (nativeMode == 3 && uniformPhase == 1)
        confirm = true;
      else if (nativeMode == 3 && uniformPhase == 2)
        confirm = focus >= 0 && e.button.y >= 413 && e.button.y < 441 &&
                  e.button.x >= (focus == 0 ? 288 : 414) &&
                  e.button.x < (focus == 0 ? 388 : 514);
      else
        confirm = focus >= 0 &&
                  hitmap->locationIndex(int(e.button.x), int(e.button.y),
                                        options.size()) == focus;
    }
    if (!confirm)
      return false;
    if (nativeMode < 3) {
      if (focus >= 0)
        select(size_t(focus));
      return true;
    }
    if (uniformPhase == 0 && focus >= 0) {
      uniformIndex = focus;
      uniformPhase = 1;
      if (nativeSound)
        nativeSound("systemmsg_000" + std::to_string(4 * (uniformIndex + 1)));
      return true;
    }
    if (uniformPhase == 1) {
      uniformPhase = 2;
      focus = -1;
      if (nativeSound)
        nativeSound("systemmsg_0016");
      return true;
    }
    if (uniformPhase == 2 && focus == 0) {
      if (nativeSound)
        nativeSound("systemmsg_0020");
      select(size_t(uniformIndex));
      return true;
    }
    if (uniformPhase == 2 && focus == 1) {
      uniformPhase = 0;
      focus = uniformIndex;
      if (nativeSound)
        nativeSound("systemmsg_0024");
      return true;
    }
    return false;
  }
  void render(RendererRef r, float scale) override {
    if (nativeMode < 0)
      ChoiceQuestionScene::render(r, scale);
  }
};
class Stage final : public Scene {
public:
  std::function<void(RendererRef, float)> draw;
  std::function<bool(EventRef)> event;
  explicit Stage(Engine &e) : Scene(e) {}
  void render(RendererRef r, float s) override {
    if (draw)
      draw(r, s);
  }
  bool handleEvent(EventRef e) override { return event ? event(e) : false; }
};
struct Sprite {
  std::string id;
  int x = 0;
  int32_t requestedPosition = -1;
  unsigned positionSlot = 0;
  uint64_t creationOrder = 0;
};
std::string stem(std::string name) {
  return name.substr(0, name.find_last_of('.'));
}
} // namespace
struct SummerRuntime::Impl {
  SummerRuntime &runtime;
  std::shared_ptr<SummerAssets> assets;
  Engine *engine = nullptr;
  Machine vm;
  std::unique_ptr<Stage> stage;
  std::unique_ptr<SummerText> text;
  std::unique_ptr<SummerChoice> choice;
  std::unique_ptr<VNSaveMenu> saves;
  PcmPlayer bgm, se, voice;
  std::unique_ptr<MediaPlayer> movie;
  std::string background = "GRAPHIC/黒.BGD", music, pending, byteOverlay,
              lastVoice;
  bool skipMode = false;
  std::map<int, Sprite> sprites;
  uint64_t nextSpriteOrder = 0;
  std::map<std::string, Image> decodedImages;
  std::map<std::string, std::vector<CharacterAnimation>> characterAnimations;
  TexturePtr canvasTexture;
  Uint64 canvasContentSerial = 0;
  Image presented, transitionFrom, lastCanvas;
  int transitionType = 0, transitionDuration = 0;
  uint64_t transitionStart = 0;
  uint64_t bgmFadeStart = 0;
  int bgmFadeDuration = 0;
  std::string nextMusic;
  bool waitSe = false, waitSkippable = true;
  std::string canvasKey;
  std::map<std::string, std::pair<int, int>> origins;
  std::vector<std::string> speakers;
  std::array<std::string, 4> defaultNames{};
  TextInputForm *nameForm = nullptr;
  static std::string nameKey(const char *prefix, size_t index) {
    return std::string("$") + prefix + "0" + char('0' + index);
  }
  std::array<std::string, 4> playerNames() const {
    auto names = defaultNames;
    for (size_t i = 0; i < 4; ++i) {
      auto found = vm.variables.find(nameKey("PlayerName", i));
      if (found != vm.variables.end())
        names[i] = string(found->second);
    }
    return names;
  }
  void setDefaultNames() {
    auto names = defaultNames;
    for (size_t i = 0; i < 4; ++i) {
      auto found = vm.variables.find(nameKey("ChangeName", i));
      if (found != vm.variables.end() && !string(found->second).empty())
        names[i] = string(found->second);
      vm.variables[nameKey("PlayerName", i)] = names[i];
    }
    vm.variables["$ChangePlayerName"] =
        int64_t(playerNameChangeMask(names, defaultNames));
  }
  void enterNames() {
    setDefaultNames();
    auto names = playerNames();
    std::vector<TextInputForm::Field> fields;
    const std::array<std::string, 4> labels{"姓", "名", "姓（かな）",
                                            "名（かな）"};
    for (size_t i = 0; i < 4; ++i)
      fields.push_back({labels[i], names[i], defaultNames[i]});
    nameForm = &engine->sharedScene<TextInputForm>();
    nameForm->configure(
        "名前入力", std::move(fields),
        [this](const auto &values) {
          std::array<std::string, 4> names;
          try {
            for (size_t i = 0; i < 4; ++i)
              names[i] = normalizePlayerName(values[i]);
          } catch (const std::invalid_argument &error) {
            return std::string(error.what());
          }
          for (size_t i = 0; i < 4; ++i) {
            vm.variables[nameKey("PlayerName", i)] = names[i];
            vm.variables[nameKey("ChangeName", i)] = names[i];
          }
          vm.variables["$ChangePlayerName"] =
              int64_t(playerNameChangeMask(names, defaultNames));
          nameForm->close();
          runtime.RemoveScene(nameForm);
          pending.clear();
          saveSystem();
          vm.resume();
          return std::string();
        },
        [this] {
          runtime.RemoveScene(nameForm);
          pending.clear();
          vm.resume();
        });
    // Keep the form above the current main scene in every state: the script
    // can request the name before GAMESTART switches from Menu to Gameplay.
    runtime.AddGlobalScene(nameForm);
    vm.waiting = true;
    pending = "NAMEENTRY";
  }
  std::string choiceTarget = "@RESULT";
  uint64_t deadline = 0, lastUpdate = 0, lastSystemWrite = 0;
  bool title = false, autoMode = false, loaded = false, skipHeld = false,
       currentRead = false, openingPlayed = false;
  int titleFocus = 0;
  std::string pendingScript;
  // Per-edition title layout loaded from EASYTITLE.INI.
  struct TitleChip {
    std::string name;
    int atlasX = 0, atlasY = 0, atlasW = 0, atlasH = 0; // Source rect in atlas
  };
  int titleBaseX = 0, titleBaseY = 0; // Screen base position from [TITLE000]
  int titleChipSpacing = 40;          // Vertical spacing between chips
  std::string titleChipTexture;       // TEMPLATENAME or fallback
  std::vector<TitleChip> titleChips;
  std::set<std::string> warned;
  explicit Impl(SummerRuntime &r, std::shared_ptr<SummerAssets> a)
      : runtime(r), assets(std::move(a)) {
    for (auto &id : assets->list("SCRIPT/")) {
      auto bytes = assets->read(id);
      vm.add(stem(id.substr(7)), std::string(bytes.begin(), bytes.end()));
    }
    auto config = assets->read("INI/CONFIG.INI");
    std::istringstream stream(utf16(config, 0, config.size() / 2));
    std::string line;
    speakers.resize(32);
    while (std::getline(stream, line)) {
      if (line.rfind("PlayerName0", 0) == 0 && line.size() > 12 &&
          line[11] >= '0' && line[11] <= '3') {
        const auto eq = line.find('=');
        if (eq != line.npos) {
          auto hex = line.substr(eq + 1);
          hex.erase(0, hex.find_first_not_of(" \t"));
          hex.erase(hex.find_last_not_of(" \t\r") + 1);
          if (hex.size() % 2 ||
              hex.find_first_not_of("0123456789abcdefABCDEF") != hex.npos)
            throw std::runtime_error("Invalid native player name");
          auto &name = defaultNames[size_t(line[11] - '0')];
          for (size_t at = 0; at < hex.size(); at += 2)
            name += char(std::stoi(hex.substr(at, 2), nullptr, 16));
        }
      }
      if (line.size() > 11 && line.substr(0, 3) == "CHR" &&
          line.substr(6, 4) == "Name") {
        int i = std::stoi(line.substr(3, 3));
        auto eq = line.find('=');
        if (i >= int(speakers.size()) || eq == line.npos)
          continue;
        std::string hex = line.substr(eq + 1);
        hex.erase(0, hex.find_first_not_of(" \t"));
        hex.erase(hex.find_last_not_of(" \t\r") + 1);
        if (hex.size() % 2 == 0 &&
            hex.find_first_not_of("0123456789ABCDEFabcdef") == hex.npos) {
          std::string name;
          for (size_t p = 0; p < hex.size(); p += 2)
            name += char(std::stoi(hex.substr(p, 2), nullptr, 16));
          speakers[i] = name;
        }
      }
    }
    vm.function = [this](const auto &name, const auto &args) {
      function(name, args);
    };
  }
  std::filesystem::path saveDirectory() const {
    if (const char *path = SDL_getenv("SUMMER_SAVE_DIR"))
      return path;
    return Kotonoha::saveDirectory();
  }
  void loadSystem() {
    std::ifstream input(saveDirectory() / "system.dat", std::ios::binary);
    if (!input)
      return;
    std::string version;
    size_t count;
    if (!(input >> version >> count) || version != "SUMMER-SYSTEM-1" ||
        count > 100000)
      throw std::runtime_error("Invalid Summer system data");
    std::map<std::string, Value> variables;
    while (count--) {
      std::string key, value;
      int type;
      if (!(input >> std::quoted(key) >> type >> std::quoted(value)) ||
          key.empty() || key[0] != '$' || (type != 0 && type != 1))
        throw std::runtime_error("Invalid Summer persistent variable");
      variables[key] = type ? Value(value) : Value(int64_t(std::stoll(value)));
    }
    input >> std::ws;
    if (!input.eof())
      throw std::runtime_error("Trailing Summer system data");
    for (auto &[key, value] : variables)
      vm.variables[key] = value;
  }
  void saveSystem() {
    if (!loaded)
      return;
    size_t count = 0;
    for (auto &[key, value] : vm.variables)
      if (!key.empty() && key[0] == '$')
        ++count;
    std::ostringstream data;
    data << "SUMMER-SYSTEM-1 " << count << '\n';
    for (auto &[key, value] : vm.variables)
      if (!key.empty() && key[0] == '$')
        data << std::quoted(key) << ' ' << value.index() << ' '
             << std::quoted(string(value)) << '\n';
    const auto path = saveDirectory() / "system.dat";
    std::filesystem::create_directories(path.parent_path());
    std::ifstream existing(path, std::ios::binary);
    std::string previous((std::istreambuf_iterator<char>(existing)), {});
    if (previous == data.str())
      return;
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << data.str();
    output.close();
    if (!output)
      throw std::runtime_error("Cannot write Summer system data");
    std::filesystem::rename(temporary, path);
  }
  void finishMessage() {
    if (vm.resource.children.size() > 1)
      vm.markRead(vm.resource.children[1]);
    voice.Stop();
    pending.clear();
    autoMode = skipMode = false;
    vm.resume();
  }
  void openSaves(bool load) {
    if (!saves)
      saves = std::make_unique<VNSaveMenu>(*engine);
    saves->open(saveDirectory().string(), load);
    runtime.RemoveScene(saves.get());
    runtime.AddScene(saves.get());
  }
  void audio(PcmPlayer &player, const std::string &id,
             const std::string &channel, bool loop = false) {
    player.Stop();
    if (!assets->contains(id)) {
      warn("Missing audio: " + id);
      return;
    }
    auto decoded = assets->loadAudio(id);
    SDL_AudioSpec target{SDL_AUDIO_S16, decoded.spec.channels,
                         decoded.spec.freq};
    Uint8 *converted = nullptr;
    int size = 0;
    if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                 int(decoded.data.size()), &target, &converted,
                                 &size))
      throw std::runtime_error(SDL_GetError());
    PcmPlayer::Clip clip;
    clip.channels = target.channels;
    clip.rate = target.freq;
    clip.loop = loop;
    clip.samples.resize(size / 2);
    std::memcpy(clip.samples.data(), converted, size);
    SDL_free(converted);
    if (loop && channel == "BGM") {
      const auto introId = std::string("SOUND/BGM/INTORO/") +
                           id.substr(id.find_last_of('/') + 1);
      if (assets->contains(introId)) {
        auto intro = assets->loadAudio(introId);
        Uint8 *pcm = nullptr;
        int bytes = 0;
        if (!SDL_ConvertAudioSamples(&intro.spec, intro.data.data(),
                                     int(intro.data.size()), &target, &pcm,
                                     &bytes))
          throw std::runtime_error(SDL_GetError());
        std::vector<int16_t> combined(bytes / 2);
        std::memcpy(combined.data(), pcm, bytes);
        SDL_free(pcm);
        clip.loopStart = combined.size() / clip.channels;
        combined.insert(combined.end(), clip.samples.begin(),
                        clip.samples.end());
        clip.samples = std::move(combined);
      }
    }
    if (!player.Play(engine->GetAudio().channel(channel), std::move(clip)))
      throw std::runtime_error(SDL_GetError());
  }
  void warn(const std::string &s) {
    if (warned.insert(s).second)
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Summer Radish: %s", s.c_str());
  }
  int route() const {
    int id = 0;
    for (auto &[name, script] : vm.programs()) {
      if (name == vm.position.script)
        return id;
      ++id;
    }
    return 0;
  }
  std::string translated(const std::string &s) const {
    return engine->GetTranslation().text(route(), s);
  }
  // Reads EASYTITLE.INI (UTF-16 LE) and extracts the per-edition title chip
  // layout: which texture to use and where each chip is drawn. Strips a
  // leading '@' from the path (the original engine's INI pack marker).
  void loadEasyTitle(const std::string &iniPath) {
    titleChipTexture.clear();
    titleChips.clear();
    titleBaseX = 0;
    titleBaseY = 0;
    titleChipSpacing = 40;
    auto inipath = iniPath;
    if (!inipath.empty() && inipath[0] == '@')
      inipath.erase(0, 1);
    std::string data;
    try {
      data = std::string(assets->read(inipath).begin(),
                         assets->read(inipath).end());
    } catch (const std::exception &) {
      return;
    }
    auto decode = [](const std::string &s) {
      if (s.size() >= 2 && s[1] == '\0') {
        Bytes bytes(s.begin(), s.end());
        try {
          return utf16(bytes, 0, s.size() / 2);
        } catch (const std::exception &) {
          // Some INI files have mixed or invalid UTF-16 sequences. Strip null
          // bytes and treat as single-byte text.
          std::string result;
          result.reserve(s.size() / 2);
          for (size_t i = 0; i < s.size(); i += 2)
            result += s[i];
          return result;
        }
      }
      return s;
    };
    std::string text = decode(data);
    std::istringstream stream(text);
    std::string line;
    std::string section;
    int chipIndex = -1;
    while (std::getline(stream, line)) {
      size_t start = line.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
        continue;
      line = line.substr(start);
      if (line.empty() || line[0] == '#' || line[0] == ';')
        continue;
      if (line.front() == '[' && line.back() == ']') {
        section = line.substr(1, line.size() - 2);
        if (section.rfind("CHIP", 0) == 0) {
          chipIndex = std::stoi(section.substr(4));
          if (chipIndex >= 0)
            titleChips.resize(
                std::max(titleChips.size(), size_t(chipIndex + 1)));
        }
        continue;
      }
      auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;
      auto key = line.substr(0, eq);
      auto val = line.substr(eq + 1);
      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      val.erase(0, val.find_first_not_of(" \t"));
      val.erase(val.find_last_not_of(" \t\r") + 1);
      if (section == "TOTAL") {
        if (key == "TEMPLATENAME")
          titleChipTexture = val;
      } else if (section.rfind("TITLE", 0) == 0) {
        if (key == "DRAWAREA") {
          std::istringstream v(val);
          int a, b, c, d, e;
          if (v >> a >> b >> c >> d >> e) {
            titleBaseX = b;
            titleBaseY = a;
          }
        }
      } else if (section.rfind("CHIP", 0) == 0 && chipIndex >= 0 &&
                 chipIndex < int(titleChips.size())) {
        auto &chip = titleChips[size_t(chipIndex)];
        if (key == "NAME")
          chip.name = val;
        else if (key == "SIZE") {
          titleChipSpacing = std::stoi(val);
        } else if (key == "AREA" || key == "DRAWAREA") {
          std::istringstream v(val);
          int a, b, c, d, e;
          if (v >> a >> b >> c >> d >> e) {
            chip.atlasX = b;
            chip.atlasY = a;
            chip.atlasW = c;
            chip.atlasH = d;
          }
        }
      }
    }
    if (titleChipTexture.empty())
      titleChipTexture = assets->containsAsset("GRAPHIC/TITLE/TITLECHIP.TEXB")
                             ? "GRAPHIC/TITLE/TITLECHIP.TEXB"
                             : "GRAPHIC/TITLE/TITLE.TEXB";
    if (titleChips.empty()) {
      // Hardcoded fallback: four chips at the classic positions.
      titleChips.resize(4);
      static const char *names[4] = {"START", "LOAD", "CONFIG", "QUIT"};
      for (int i = 0; i < 4; ++i) {
        titleChips[i].name = names[i];
        titleChips[i].atlasX = 0;
        titleChips[i].atlasY = i * 32;
        titleChips[i].atlasW = 230;
        titleChips[i].atlasH = 32;
      }
      titleBaseX = 285;
      titleBaseY = 342;
      titleChipSpacing = 40;
    }
  }
  void begin(const std::string &script = "START") {
    if (movie)
      movie->stop();
    bgm.Stop();
    se.Stop();
    voice.Stop();
    music.clear();
    sprites.clear();
    background = "GRAPHIC/黒.BGD";
    pending.clear();
    autoMode = skipMode = skipHeld = false;
    text->concealed = false;
    text->controls.reset();
    title = false;
    deadline = 0;
    runtime.SetState(GameState::Menu);
    text->hide();
    choice->hide();
    byteOverlay.clear();
    transitionDuration = 0;
    presented = image(background);
    canvasKey.clear();
    GetVNBacklogRuntime(*engine).clear();
    saveSystem();
    pendingScript = script;
    // Reproduz o vídeo de abertura (se existir) apenas na primeira
    // inicialização, antes do título, igual ao jogo original. Tenta nomes
    // conhecidos em cada instalação. Não inicia o script enquanto o filme
    // estiver tocando; ele começa quando o filme termina (tratado em update()).
    if (!openingPlayed) {
      std::vector<std::string> openingCandidates{
          "DISC1/opening.mpg", "DISC1/summer_op.mpg", "opening.mpg",
          "summer_op.mpg"};
      for (const auto &candidate : openingCandidates) {
        try {
          movie->open(assets->moviePath(candidate).string(), false, "BGM");
          text->hide();
          vm.waiting = true;
          pending = "OPENING";
          openingPlayed = true;
          return; // Não inicia o script ainda; espera o filme terminar.
        } catch (const std::exception &) {
          // Candidate não existe nesta instalação; tenta o próximo.
        }
      }
      openingPlayed = true; // Não há vídeo; não tentará novamente.
    }
    vm.start(script);
    // Keep the original boot warnings/logos; globals absent on first boot are
    // zero.
    vm.run();
  }
  void bind(Engine &e) {
    engine = &e;
    if (!stage) {
      auto &history = e.configureSharedScene<VNBacklog, SummerBacklog>(*assets);
      history.setVoiceReplay([this, history = &history](const std::string &id) {
        audio(history->replay, id, "Voice");
      });
      stage = std::make_unique<Stage>(e);
      text = std::make_unique<SummerText>(e, *assets);
      text->autoActive = [this] { return autoMode; };
      text->skipActive = [this] { return skipMode || skipHeld; };
      text->controls.buttons[0].action = [this] {
        skipMode = !skipMode;
        if (skipMode)
          autoMode = false;
      };
      text->controls.buttons[1].action = [this] {
        GetVNBacklogRuntime(*engine).open();
      };
      text->controls.buttons[2].action = [this] {
        autoMode = !autoMode;
        if (autoMode)
          skipMode = false;
        deadline = 0;
      };
      text->controls.buttons[3].action = [this] {
        GetInGameMenuRuntime(*engine).open();
      };
      text->controls.buttons[4].action = [this] { text->concealed = true; };
      text->controls.buttons[5].action = [this] {
        runtime.saveGame((saveDirectory() / "quicksave.vn").string());
      };
      choice = std::make_unique<SummerChoice>(e, *assets);
      movie = std::make_unique<MediaPlayer>(e.GetAudio());
      choice->nativeSound = [this](const std::string &id) {
        audio(se, "SOUND/" + id + ".ogg", "Se");
      };
      stage->draw = [this](RendererRef r, float scale) { draw(r, scale); };
      stage->event = [this](EventRef e) { return event(e); };
    }
    runtime.AddScene(stage.get());
    runtime.AddScene(text.get());
    runtime.AddScene(choice.get());
    runtime.AddScene(&GetVNBacklogRuntime(e));
    auto &menu = GetInGameMenuRuntime(e);
    menu.close();
    menu.SetOnSave([this] { openSaves(false); });
    menu.SetOnLoad([this] { openSaves(true); });
    menu.SetOnReturnToMenu([this] {
      GetInGameMenuRuntime(*engine).close();
      begin();
    });
    runtime.AddScene(&menu);
    if (!loaded) {
      loadSystem();
      loaded = true;
      begin();
    }
  }
  void options(const std::string &question, std::vector<std::string> labels,
               const std::string &kind, const std::vector<int> &ids = {}) {
    pending = kind;
    vm.waiting = true;
    text->hide();
    std::vector<ChoiceOption> options;
    for (size_t i = 0; i < labels.size(); ++i)
      options.push_back(
          {translated(labels[i]), ids.empty() ? int(i) : ids.at(i)});
    choice->setQuestion(question, options,
                        [this, kind](int index, const std::string &) {
                          choice->hide();
                          pending.clear();
                          if (kind == "SELECT") {
                            vm.markRead(vm.resource.children.at(1 + index * 2));
                            vm.variables[choiceTarget] = int64_t(index);
                            saveSystem();
                          } else if (kind == "UNIFORM") {
                            vm.variables["@制服番号"] = int64_t(index + 2);
                            vm.variables["@EV制服"] = int64_t(index + 24);
                          } else if (kind == "MAP")
                            vm.variables["@移動場所"] = int64_t(index);
                          vm.resume();
                        });
  }
  void message() {
    auto &r = vm.resource.children;
    if (r.size() < 5)
      throw std::runtime_error("Invalid MESSAGE resource");
    int actor = int(number(vm.evaluate(r[4])));
    std::string speaker =
        actor > 0 && actor < int(speakers.size()) ? speakers[actor] : "";

    currentRead = vm.hasRead(r[1]);
    const auto names = playerNames();
    if (actor == 1)
      speaker = names[1];
    text->setDialogue(
        translated(speaker),
        translated(substitutePlayerNames(string(vm.evaluate(r[2])), names)));
    text->show();
    text->concealed = false;
    if (!currentRead)
      skipMode = false;
    voice.Stop();
    if (r.size() > 5) {
      auto id = "VOICE/" + string(vm.evaluate(r[5])) + ".ogg";
      if (assets->contains(id)) {
        audio(voice, id, "Voice");
        GetVNBacklogRuntime(*engine).setLastVoice(id);
      }
    }
    vm.waiting = true;
    pending = "MESSAGE";
    deadline = 0;
  }
  void function(const std::string &name, const std::vector<Value> &args) {
    auto arg = [&](size_t i) -> Value {
      return i < args.size() ? args[i] : Value(int64_t(0));
    };
    if (name == "MESSAGE") {
      message();
      return;
    }
    if (name == "SELECT") {
      const auto &node =
          vm.programs().at(vm.position.script).code.at(vm.position.pc - 1).node;
      choiceTarget =
          node.children.empty()
              ? "@RESULT"
              : (node.children[0].attr("type") == "GLOBAL" ? "$" : "@") +
                    node.children[0].text;
      std::vector<std::string> labels;
      std::vector<int> ids;
      std::vector<bool> read;
      for (size_t i = 2; i < vm.resource.children.size() && ids.size() < 32;
           i += 2) {
        const auto index = int((i - 2) / 2);
        if (std::size_t(index + 1) < args.size() && !number(args[index + 1]))
          continue;
        labels.push_back(string(vm.evaluate(vm.resource.children[i])));
        ids.push_back(index);
        read.push_back(vm.hasRead(vm.resource.children[i - 1]));
      }
      if (labels.empty()) {
        vm.variables[choiceTarget] = int64_t(-1);
        return;
      }
      options("", std::move(labels), "SELECT", ids);
      choice->setReadStates(std::move(read));
      return;
    }
    if (name == "LOADBACK" || name == "LOADEVENT") {
      auto id = "GRAPHIC/" + string(arg(0));
      id += graphicSuffix(uint32_t(number(arg(1)))) +
            (name == "LOADBACK" ? ".BGD" : ".CGD");
      if (assets->contains(id)) {
        background = id;
        sprites.clear();
      } else
        warn("Missing picture: " + id);
      return;
    }
    if (name == "LOADCHAR" || name == "LOADADDCHAR") {
      auto id =
          "GRAPHIC/" +
          (name == "LOADADDCHAR" ? std::string("ADDCHAR/") : std::string()) +
          string(arg(0));
      id += graphicSuffix(uint32_t(number(arg(1)))) + ".CHR";
      if (assets->contains(id)) {
        if (!origins.count(id)) {
          auto img = assets->picture(id);
          origins[id] = {img.x, img.y};
        }
        auto &sprite =
            sprites[int(number(arg(2))) + (name == "LOADADDCHAR" ? 100 : 0)];
        if (sprite.id.empty())
          sprite.creationOrder = ++nextSpriteOrder;
        sprite.id = id;
        sprite.requestedPosition =
            args.size() > 3 ? int32_t(number(arg(3))) : -1;
      } else
        warn("Missing character: " + id);
      return;
    }
    if (name == "ERASECHAR") {
      sprites.erase(int(number(arg(0))));
      sprites.erase(int(number(arg(0))) + 100);
      return;
    }
    if (name == "PLAYBGM") {
      auto id = "SOUND/BGM/" + string(arg(0)) + ".ogg";
      if (music != id) {
        audio(bgm, id, "BGM", args.size() < 2 || number(arg(1)) != 0);
        music = id;
      }
      bgmFadeDuration = 0;
      return;
    }
    if (name == "STOPBGM") {
      bgm.Stop();
      music.clear();
      bgmFadeDuration = 0;
      return;
    }
    if (name == "FADEOUTBGM") {
      int speed = int(number(arg(1)));
      if (speed < 0 || speed >= 3)
        speed = 0;
      static const int duration[3] = {1000, 500, 2000};
      bgmFadeDuration = duration[speed];
      bgmFadeStart = SDL_GetTicks();
      nextMusic = args.empty() ? "" : "SOUND/BGM/" + string(arg(0)) + ".ogg";
      return;
    }
    if (name == "PLAYSE") {
      audio(se, "SOUND/" + string(arg(0)) + ".ogg", "Se");
      return;
    }
    if (name == "STOPSE") {
      se.Stop();
      return;
    }
    if (name == "WAITTIME" || name == "WAITSE") {
      waitSe = name == "WAITSE";
      waitSkippable = (number(arg(waitSe ? 1 : 1)) & 1) == 0;
      pending = "WAIT";
      vm.waiting = true;
      int64_t delay =
          waitSe ? (args.empty() || number(arg(0)) < 0 ? INT64_C(86400000)
                                                       : number(arg(0)))
                 : number(arg(0));
      deadline = SDL_GetTicks() + std::clamp<int64_t>(delay, 0, 86400000);
      return;
    }
    if (name == "EASYTITLE") {
      title = true;
      titleFocus = 0;
      pending = "TITLE";
      vm.waiting = true;
      deadline = 0;
      background = "GRAPHIC/TITLE/TITLE.CGD";
      sprites.clear();
      text->hide();
      loadEasyTitle(string(arg(0)));
      return;
    }
    if (name == "UNISELECT" || name == "MAPSELECT") {
      bool uniform = name == "UNISELECT";
      pending = uniform ? "UNIFORM" : "MAP";
      vm.waiting = true;
      text->hide();
      int mode = uniform ? 3 : int(number(arg(0)));
      vm.variables["@KOTONOHA_MAP_MODE"] = int64_t(mode);
      choice->setNative(mode, *assets,
                        [this, uniform](int value, const std::string &) {
                          if (!lastCanvas.rgba.empty())
                            presented = lastCanvas;
                          choice->hide();
                          pending.clear();
                          if (uniform) {
                            vm.variables["@制服番号"] = int64_t(value + 2);
                            vm.variables["@EV制服"] = int64_t(value + 24);
                          } else
                            vm.variables["@移動場所"] = int64_t(value);
                          vm.resume();
                        });
      return;
    }
    if (name == "MESSWINDOW") {
      if (number(arg(0)))
        text->show();
      else
        text->hide();
      return;
    }
    if (name == "WIPE") {
      text->hide();
      transitionType = int(number(arg(0)));
      transitionDuration = wipeDuration(transitionType);
      transitionStart = SDL_GetTicks();
      transitionFrom =
          presented.rgba.empty() ? image("GRAPHIC/黒.BGD") : presented;
      presented = composeScene();
      if (transitionDuration > 0) {
        pending = "WIPE";
        vm.waiting = true;
        deadline = transitionStart + transitionDuration;
      }
      return;
    }
    if (name == "GAMESTART") {
      // Global overlays (such as an open name form) may already be registered
      // for Gameplay; never register the same scene twice or it would receive
      // every event twice.
      for (auto *scene : runtime.GetScenes(GameState::Menu)) {
        const auto &gameplay = runtime.GetScenes(GameState::Gameplay);
        if (std::find(gameplay.begin(), gameplay.end(), scene) ==
            gameplay.end())
          runtime.AddScene(scene, GameState::Gameplay);
      }
      runtime.SetState(GameState::Gameplay);
      return;
    }
    if (name == "GAMEEND")
      return;
    if (name == "SETQUITCODE") {
      if (number(arg(0)) != 0)
        vm.ended = true;
      return;
    }
    if (name == "SETDEFAULTNAME") {
      setDefaultNames();
      return;
    }
    if (name == "NAMEENTRY") {
      enterNames();
      return;
    }
    if (name == "TOPLADY") {
      vm.variables["@FLAG020"] = int64_t(topLady(
          {number(vm.variables["@FLAG001"]), number(vm.variables["@FLAG002"]),
           number(vm.variables["@FLAG003"]),
           number(vm.variables["@FLAG004"])}));
      return;
    }
    if (name == "BYTEVIEW") {
      byteOverlay = byteViewPicture(int(number(vm.variables["@移動場所"])));
      if (!byteOverlay.empty()) {
        pending = "BYTEVIEW";
        vm.waiting = true;
        deadline = SDL_GetTicks() + byteViewDuration(500);
        text->hide();
      }
      return;
    }
    if (name == "PLAYMOVIE") {
      movie->open(assets->moviePath(string(arg(0))).string(), false, "BGM");
      text->hide();
      vm.waiting = true;
      pending = "MOVIE";
      return;
    }
    throw std::runtime_error("Unimplemented native function: " + name);
  }
  void picture(RendererRef r, const std::string &id, SDL_FRect target,
               const SDL_FRect *source = nullptr) {
    auto &texture = assets->texture(r, id);
    RenderUpscaledTexture(&r, &texture, source, &target);
  }
  const Image &image(const std::string &id) {
    auto it = decodedImages.find(id);
    if (it == decodedImages.end())
      it = decodedImages.emplace(id, assets->picture(id)).first;
    return it->second;
  }
  const std::vector<CharacterAnimation> &animations(const std::string &id) {
    auto it = characterAnimations.find(id);
    if (it == characterAnimations.end())
      it = characterAnimations
               .emplace(id, decodeCharacterAnimations(assets->read(id)))
               .first;
    return it->second;
  }
  Image composeScene() {
    Image canvas = image(background);
    std::vector<std::pair<int, Sprite *>> ordered;
    for (auto &[slot, sprite] : sprites)
      if (slot < 100)
        ordered.emplace_back(slot, &sprite);
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
      return a.second->creationOrder < b.second->creationOrder;
    });
    std::vector<CharacterPlacement> positions;
    for (const auto &[slot, sprite] : ordered)
      positions.push_back({sprite->requestedPosition, sprite->positionSlot});
    assignCharacterPositions(positions);
    static constexpr int offsets[] = {0, -200, 200};
    for (size_t i = 0; i < ordered.size(); ++i) {
      auto &[slot, pointer] = ordered[i];
      auto &sprite = *pointer;
      sprite.positionSlot = positions[i].previous;
      sprite.x = offsets[sprite.positionSlot];
      auto origin = origins.at(sprite.id);
      compositeImage(canvas, image(sprite.id), origin.first + sprite.x,
                     origin.second);
      compositeCharacterAnimations(canvas, animations(sprite.id), sprite.x);
      // Native additional characters are attached to their base actor and
      // rendered immediately after it using the same assigned x offset.
      const auto added = sprites.find(slot + 100);
      if (added != sprites.end()) {
        const auto addOrigin = origins.at(added->second.id);
        compositeImage(canvas, image(added->second.id),
                       addOrigin.first + sprite.x, addOrigin.second);
        compositeCharacterAnimations(canvas, animations(added->second.id),
                                     sprite.x);
      }
    }
    return canvas;
  }
  void draw(RendererRef r, float) {
    std::string base =
        choice->mode() >= 0 ? choice->nativeBackground() : background;
    std::ostringstream state;
    state << base << byteOverlay << ':' << transitionDuration << ':'
          << (transitionDuration ? (SDL_GetTicks() - transitionStart) / 8 : 0)
          << ':' << title << ':' << titleFocus << ':' << text->isVisible()
          << ':' << text->concealed << ':' << choice->mode() << ':'
          << choice->focused();
    for (auto &[slot, sprite] : sprites)
      state << slot << sprite.id << sprite.x;
    if (choice->mode() >= 0)
      for (const auto &[key, value] : vm.variables)
        if (key.compare(0, 6, "@CHAR_") == 0)
          state << key << string(value);
    if (!canvasTexture || canvasKey != state.str()) {
      if (decodedImages.size() > 96)
        decodedImages.clear();
      Image canvas =
          (choice->mode() >= 0 || title)
              ? image(base)
              : (presented.rgba.empty() ? composeScene() : presented);
      if (transitionDuration > 0 && choice->mode() < 0 && !title)
        canvas = wipeFrame(transitionFrom, presented, transitionType,
                           int(SDL_GetTicks() - transitionStart),
                           transitionDuration);
      if (!byteOverlay.empty())
        compositeImage(canvas, image(byteOverlay), 216, 161);
      if (text->isVisible() && !text->concealed &&
          assets->containsAsset("GRAPHIC/FRAME/MESSFRAME.TEXB"))
        compositeImageRegion(canvas, image("GRAPHIC/FRAME/MESSFRAME.TEXB"), 0,
                             391, 0, 0, 800, 209);
      choice->compose(
          canvas, [this](const auto &id) -> const Image & { return image(id); },
          vm);
      if (title) {
        if (assets->containsAsset(titleChipTexture))
          for (size_t i = 0; i < titleChips.size(); ++i) {
            const auto &chip = titleChips[i];
            const bool focused = i == size_t(titleFocus);
            compositeImageRegion(canvas, image(titleChipTexture), titleBaseX,
                                 titleBaseY + int(i) * titleChipSpacing,
                                 focused ? chip.atlasW : 0,
                                 int(i) * chip.atlasH, chip.atlasW,
                                 chip.atlasH);
          }
      }
      if (!canvasTexture)
        canvasTexture.reset(SDL_CreateTexture(
            &r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 800, 600));
      if (!canvasTexture || !SDL_UpdateTexture(canvasTexture.get(), nullptr,
                                               canvas.rgba.data(), 800 * 4))
        throw std::runtime_error(SDL_GetError());
      ++canvasContentSerial;
      lastCanvas = std::move(canvas);
      canvasKey = state.str();
    }
    SDL_FRect target{0, 0, 800, 600};
    RenderUpscaledTexture(&r, canvasTexture.get(), nullptr, &target,
                          canvasContentSerial);
    if (pending == "MOVIE" && movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, target);
    }
    if (pending == "OPENING" && movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, target);
    }
  }
  void titleAction(int index) {
    if (index == 0) {
      title = false;
      pending.clear();
      vm.variables["@RESULT"] = std::string("START");
      vm.resume();
    }
    if (index == 1)
      openSaves(true);
    if (index == 2)
      engine->openSettings();
    if (index == 3) {
      SDL_Event e{};
      e.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&e);
    }
  }
  bool event(EventRef e) {
    if (pending == "MESSAGE" && BackAction(*engine, e)) {
      GetInGameMenuRuntime(*engine).open();
      return true;
    }
    if (pending == "MESSAGE" && ShouldOpenBacklog(*engine, e)) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }
    // Controller Back/left shoulder maps to PAGEUP; accept it as history too.
    if (pending == "MESSAGE" && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        e.key.key == SDLK_PAGEUP) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }
    if (e.type == SDL_EVENT_KEY_UP &&
        (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL))
      skipHeld = false;
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
      skipHeld = false;
    if (engine->isStoryPaused() || choice->isActive())
      return false;
    if (title) {
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        const auto chipCount = titleChips.empty() ? 4 : titleChips.size();
        if (e.key.key == SDLK_DOWN)
          titleFocus = (titleFocus + 1) % int(chipCount);
        else if (e.key.key == SDLK_UP)
          titleFocus = (titleFocus + 3) % int(chipCount);
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)
          titleAction(titleFocus);
      }
      if (e.type == SDL_EVENT_MOUSE_MOTION)
        for (size_t i = 0; i < titleChips.size(); ++i) {
          const auto &chip = titleChips[i];
          SDL_FRect rect{float(titleBaseX),
                         float(titleBaseY + int(i) * titleChipSpacing),
                         float(chip.atlasW), float(chip.atlasH)};
          SDL_FPoint point{e.motion.x, e.motion.y};
          if (SDL_PointInRectFloat(&point, &rect))
            titleFocus = int(i);
        }
      for (size_t i = 0; i < titleChips.size(); ++i) {
        const auto &chip = titleChips[i];
        if (UI::clicked(e, {float(titleBaseX),
                            float(titleBaseY + int(i) * titleChipSpacing),
                            float(chip.atlasW), float(chip.atlasH)})) {
          titleAction(int(i));
          break;
        }
      }
      return true;
    }
    // The opening video plays once before the title. Pressing confirm or
    // clicking skips it and starts the script, matching the original behavior
    // where the opening can be bypassed.
    if (pending == "OPENING") {
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)) {
        movie->stop();
        pending.clear();
        vm.start(pendingScript);
        vm.run();
        return true;
      }
      if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
          e.button.button == SDL_BUTTON_LEFT) {
        movie->stop();
        pending.clear();
        vm.start(pendingScript);
        vm.run();
        return true;
      }
      return true;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL) {
        skipHeld = true;
        return true;
      }
      if (e.key.key == SDLK_ESCAPE) {
        GetInGameMenuRuntime(*engine).open();
        return true;
      }
      if (e.key.key == SDLK_B) {
        GetVNBacklogRuntime(*engine).open();
        return true;
      }
      if (e.key.key == SDLK_A) {
        autoMode = !autoMode;
        if (autoMode)
          skipMode = false;
        return true;
      }
      if (e.key.key == SDLK_S) {
        skipMode = !skipMode;
        if (skipMode)
          autoMode = false;
        return true;
      }
      if (e.key.key == SDLK_F5) {
        runtime.saveGame((saveDirectory() / "quicksave.vn").string());
        return true;
      }
      if (e.key.key == SDLK_F6) {
        runtime.loadGame((saveDirectory() / "quicksave.vn").string());
        return true;
      }
    }
    if (pending == "MESSAGE" && text->concealed &&
        (UI::clicked(e, {0, 0, 800, 600}) ||
         (e.type == SDL_EVENT_KEY_DOWN &&
          (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)))) {
      text->concealed = false;
      return true;
    }
    if (text->handleEvent(e))
      return true;
    bool advance = UI::clicked(e, {0, 0, 800, 600}) ||
                   (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                    (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE));
    if (advance && vm.waiting &&
        (pending == "MESSAGE" || pending == "WAIT" || pending == "BYTEVIEW" ||
         pending == "WIPE")) {
      if (pending == "MESSAGE" && !text->isDialogueComplete())
        text->completeDialogue();
      else {
        if (pending == "MESSAGE")
          finishMessage();
        else if (pending != "WAIT" || waitSkippable) {
          if (waitSe)
            se.Stop();
          transitionDuration = 0;
          byteOverlay.clear();
          pending.clear();
          vm.resume();
        }
      }
      return true;
    }
    return false;
  }
};
SummerRuntime::SummerRuntime(std::shared_ptr<SummerAssets> a)
    : Runtime("Summer Radish Vacation!!", "0.1.0",
              "me.hirameki.kotonoha.summerradishvacation"),
      impl(std::make_unique<Impl>(*this, std::move(a))) {}
SummerRuntime::~SummerRuntime() = default;
void SummerRuntime::shutdown() {
  if (impl) {
    try {
      impl->saveSystem();
    } catch (const std::exception &e) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Summer persistent data: %s",
                   e.what());
    }
    impl->bgm.Stop();
    impl->se.Stop();
    impl->voice.Stop();
    if (impl->movie)
      impl->movie->stop();
    if (impl->nameForm) {
      impl->nameForm->close();
      RemoveScene(impl->nameForm);
      impl->nameForm = nullptr;
    }
    impl->saves.reset();
    impl->choice.reset();
    impl->text.reset();
    impl->stage.reset();
    impl->canvasTexture.reset();
    impl->assets->clearCache();
  }
}
void SummerRuntime::initMenu(Engine &e) { impl->bind(e); }
void SummerRuntime::initGameplay(Engine &e) { impl->bind(e); }
SDL_FPoint SummerRuntime::sceneSize(const Scene &s) const {
  return s.designSpace() ? SDL_FPoint{1280, 720} : SDL_FPoint{800, 600};
}
void SummerRuntime::update() {
  Runtime::update();
  if (!impl->engine || !impl->loaded)
    return;
  auto &i = *impl;
  const auto now = SDL_GetTicks();
  const bool paused = i.engine->isStoryPaused();
  i.bgm.SetPaused(paused);
  i.se.SetPaused(paused);
  i.voice.SetPaused(paused);
  if (i.movie)
    i.movie->setPaused(paused);
  if (paused) {
    if (i.deadline && i.lastUpdate)
      i.deadline += now - i.lastUpdate;
    if (i.transitionDuration && i.lastUpdate)
      i.transitionStart += now - i.lastUpdate;
    if (i.bgmFadeDuration && i.lastUpdate)
      i.bgmFadeStart += now - i.lastUpdate;
    i.lastUpdate = now;
    return;
  }
  i.lastUpdate = now;
  if (i.pending == "MOVIE" && i.movie && i.movie->finished()) {
    i.movie->stop();
    i.pending.clear();
    i.vm.resume();
  }
  if (i.pending == "OPENING" && i.movie && i.movie->finished()) {
    i.movie->stop();
    i.pending.clear();
    i.title = true;
    i.titleFocus = 0;
    i.background = "GRAPHIC/TITLE/TITLE.CGD";
    i.sprites.clear();
    i.text->hide();
    i.vm.start(i.pendingScript);
    i.vm.run();
  }
  if (i.bgmFadeDuration > 0) {
    float progress =
        std::min(1.0f, float(now - i.bgmFadeStart) / i.bgmFadeDuration);
    i.bgm.SetGain(1 - progress);
    if (progress >= 1) {
      i.bgm.Stop();
      i.music.clear();
      i.bgmFadeDuration = 0;
      if (!i.nextMusic.empty()) {
        i.music = i.nextMusic;
        i.audio(i.bgm, i.music, "BGM", true);
        i.nextMusic.clear();
      }
    }
  }
  if ((i.pending == "WAIT" || i.pending == "BYTEVIEW" || i.pending == "WIPE") &&
      (now >= i.deadline ||
       (i.pending == "WAIT" && i.waitSe && !i.se.Playing()))) {
    if (i.waitSe)
      i.se.Stop();
    i.waitSe = false;
    i.transitionDuration = 0;
    i.byteOverlay.clear();
    i.pending.clear();
    i.vm.resume();
  }
  if (now - i.lastSystemWrite >= 5000) {
    i.saveSystem();
    i.lastSystemWrite = now;
  }
  if (i.pending == "MESSAGE" && !i.text->concealed &&
      (i.skipHeld || i.skipMode) && i.currentRead) {
    i.text->completeDialogue();
    i.finishMessage();
  }
  if (i.pending == "MESSAGE" && !i.text->concealed && i.autoMode &&
      i.text->isDialogueComplete() && !i.voice.Playing()) {
    if (!i.deadline)
      i.deadline = SDL_GetTicks() + 1500;
    if (SDL_GetTicks() >= i.deadline) {
      i.finishMessage();
    }
  }
  if (i.vm.ended)
    i.begin();
}
bool SummerRuntime::onEvent(EventRef e) {
  auto &i = *impl;
  // Shared modals (pause, save browser, history, name entry) and choices map
  // controller buttons in their own context. Pre-mapping would turn South into
  // held skip while a menu is open.
  for (auto *scene : GetActiveScenes())
    if (scene && scene->capturesInput()) {
      i.skipHeld = false;
      return Runtime::onEvent(e);
    }
  const bool dialogue = i.text && i.text->isVisible() && !i.text->concealed;
  // The message-box toolbar must see raw directions: while dialogue is open
  // the shared mapping turns Left/Right into text-speed keys, but here they
  // focus the on-screen buttons and Up releases focus back to the novel.
  if (dialogue &&
      (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
       e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
      i.text->handleEvent(e))
    return true;
  return Runtime::onEvent(MapControllerEvent(e, dialogue));
}
void SummerRuntime::saveGame(const std::string &path) {
  auto &i = *impl;
  if (!i.vm.waiting || i.pending == "WAIT" || i.pending == "WIPE" ||
      i.pending == "BYTEVIEW" || i.pending == "MOVIE" || i.title)
    throw std::runtime_error("Save at a dialogue or choice");
  std::ostringstream s;
  s << "SUMMER-PRESENTATION-2\n"
    << std::quoted(i.vm.save()) << '\n'
    << std::quoted(i.background) << ' ' << std::quoted(i.music) << ' '
    << std::quoted(i.pending) << ' ' << std::quoted(i.choiceTarget) << '\n'
    << i.sprites.size() << '\n';
  for (auto &[slot, picture] : i.sprites)
    s << slot << ' ' << std::quoted(picture.id) << ' ' << picture.x << ' '
      << picture.requestedPosition << ' ' << picture.positionSlot << ' '
      << picture.creationOrder << '\n';
  i.saveSystem();
  VNSave save;
  save.nodeId = 0;
  save.gameData = s.str();
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path().empty()
          ? "."
          : std::filesystem::path(path).parent_path());
  save.write(path);
}
void SummerRuntime::loadGame(const std::string &path) {
  auto &i = *impl;
  auto save = VNSave::read(path);
  std::istringstream s(save.gameData);
  std::string version, data, background, music, pending, target;
  size_t count;
  if (!(s >> version >> std::quoted(data) >> std::quoted(background) >>
        std::quoted(music) >> std::quoted(pending) >> std::quoted(target) >>
        count) ||
      (version != "SUMMER-PRESENTATION-1" &&
       version != "SUMMER-PRESENTATION-2") ||
      count > 64 || !i.assets->contains(background) ||
      (pending != "MESSAGE" && pending != "SELECT" && pending != "UNIFORM" &&
       pending != "MAP"))
    throw std::runtime_error("Invalid Summer presentation save");
  Machine restored = i.vm;
  restored.restore(data);
  for (auto &[key, value] : i.vm.variables)
    if (!key.empty() && key[0] == '$')
      restored.variables[key] = value;
  std::map<int, Sprite> sprites;
  uint64_t nextOrder = 0;
  while (count--) {
    int slot;
    Sprite sprite;
    if (!(s >> slot >> std::quoted(sprite.id) >> sprite.x) ||
        !i.assets->contains(sprite.id) || std::abs(int64_t(sprite.x)) > 10000)
      throw std::runtime_error("Invalid saved character");
    if (version == "SUMMER-PRESENTATION-2") {
      if (!(s >> sprite.requestedPosition >> sprite.positionSlot >>
            sprite.creationOrder) ||
          sprite.positionSlot >= 3 || sprite.creationOrder == UINT64_MAX)
        throw std::runtime_error("Invalid saved character placement");
    } else {
      sprite.positionSlot = sprite.x < 0 ? 1 : sprite.x > 0 ? 2 : 0;
      sprite.requestedPosition = int32_t(sprite.positionSlot);
      sprite.creationOrder = nextOrder + 1;
    }
    nextOrder = std::max(nextOrder, sprite.creationOrder);
    sprites[slot] = sprite;
    auto img = i.assets->picture(sprite.id);
    i.origins[sprite.id] = {img.x, img.y};
  }
  s >> std::ws;
  if (!s.eof())
    throw std::runtime_error("Trailing presentation data");
  i.vm = std::move(restored);
  if (i.movie)
    i.movie->stop();
  i.background = background;
  i.music = music;
  i.sprites = std::move(sprites);
  i.nextSpriteOrder = nextOrder;
  i.title = false;
  i.autoMode = i.skipMode = i.skipHeld = false;
  i.text->controls.reset();
  i.text->concealed = false;
  i.pending = pending;
  i.choiceTarget = target;
  i.deadline = 0;
  i.transitionDuration = 0;
  i.presented = i.composeScene();
  i.canvasKey.clear();
  i.choice->hide();
  i.text->hide();
  i.voice.Stop();
  i.se.Stop();
  i.bgm.Stop();
  if (!music.empty())
    i.audio(i.bgm, music, "BGM", true);
  if (pending == "MESSAGE")
    i.message();
  else if (pending == "SELECT") {
    const auto &node = i.vm.programs()
                           .at(i.vm.position.script)
                           .code.at(i.vm.position.pc - 1)
                           .node;
    std::vector<Value> args;
    for (const auto &part : node.children)
      args.push_back(i.vm.evaluate(part));
    i.function("SELECT", args);
  } else {
    bool uniform = pending == "UNIFORM";
    int mode = uniform ? 3 : int(number(i.vm.variables["@KOTONOHA_MAP_MODE"]));
    i.choice->setNative(mode, *i.assets,
                        [&i, uniform](int value, const std::string &) {
                          if (!i.lastCanvas.rgba.empty())
                            i.presented = i.lastCanvas;
                          i.choice->hide();
                          i.pending.clear();
                          if (uniform) {
                            i.vm.variables["@制服番号"] = int64_t(value + 2);
                            i.vm.variables["@EV制服"] = int64_t(value + 24);
                          } else
                            i.vm.variables["@移動場所"] = int64_t(value);
                          i.vm.resume();
                        });
  }
  GetInGameMenuRuntime(*i.engine).close();
  GetVNBacklogRuntime(*i.engine).close();
}
Translation::SourceTexts SummerRuntime::translationSources() const {
  Translation::SourceTexts result;
  int route = 0;
  for (auto &[name, script] : impl->vm.programs()) {
    for (auto &instruction : script.code)
      if (instruction.node.tag == "RESOURCE")
        for (auto &node : instruction.node.children)
          if (node.attr("type") == "STR")
            result[route].push_back(node.text);
    ++route;
  }
  return result;
}
int SummerRuntime::translationRoute() const { return impl->route(); }
std::string SummerRuntime::debugNode() const {
  return impl->vm.position.script + ":" + std::to_string(impl->vm.position.pc);
}
std::string SummerRuntime::nodeClockKey() const {
  // All positions within the same script share the node clock; only the
  // event clock resets between PCs of a script.
  return impl->vm.position.script;
}
std::vector<DebugNode> SummerRuntime::debugNodes() const {
  std::vector<DebugNode> nodes;
  for (auto &[name, s] : impl->vm.programs()) {
    nodes.push_back({name, name, {}});
    for (auto &[label, pc] : s.labels)
      nodes.push_back({name + "::" + label, label, {}});
  }
  return nodes;
}
void SummerRuntime::debugJumpToNode(const std::string &node) {
  if (impl->movie)
    impl->movie->stop();
  if (!impl->engine)
    throw std::runtime_error("Runtime has not started");
  impl->begin(node);
  SetState(GameState::Gameplay);
  for (auto *scene : GetScenes(GameState::Menu)) {
    const auto &gameplay = GetScenes(GameState::Gameplay);
    if (std::find(gameplay.begin(), gameplay.end(), scene) == gameplay.end())
      AddScene(scene, GameState::Gameplay);
  }
}
} // namespace SummerRadish
