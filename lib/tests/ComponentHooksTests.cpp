// The engine components are extended by overriding their protected hooks: the
// base classes keep input routing and state, and call back into the subclass.
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/ImageButtonBar.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/SurfaceBlend.hpp>
#include <filesystem>
#include <fstream>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct Calls {
  std::map<std::string, int> count;
  void operator()(const std::string &hook) { ++count[hook]; }
  int operator[](const std::string &hook) const {
    const auto it = count.find(hook);
    return it == count.end() ? 0 : it->second;
  }
};

SDL_Event key(SDL_Keycode code) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = code;
  event.key.down = true;
  return event;
}

SDL_Event click(float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.down = true;
  event.button.x = x;
  event.button.y = y;
  return event;
}

class HookTextBox : public Kotonoha::VNTextBox {
public:
  Calls calls;
  std::string recorded;
  explicit HookTextBox(Kotonoha::Engine &engine) : VNTextBox(engine, "", "") {}

protected:
  double charactersPerSecond() const override { return 1e9; }
  void recordDialogue(const std::string &speaker,
                      const std::string &text) override {
    calls("record");
    recorded = speaker + ":" + text;
  }
  void openMenu() override { calls("menu"); }
  void openBacklog() override { calls("backlog"); }
  void renderFrame(Kotonoha::RendererRef) override { calls("frame"); }
  void renderSpeaker(Kotonoha::RendererRef) override { calls("speaker"); }
  void renderDialogue(Kotonoha::RendererRef) override { calls("dialogue"); }
  void renderIndicator(Kotonoha::RendererRef) override { calls("indicator"); }
};

class HookChoice : public Kotonoha::ChoiceQuestionScene {
protected:
  SDL_FRect listArea() const override { return {0, 0, 100, 600}; }

public:
  Calls calls;
  explicit HookChoice(Kotonoha::Engine &engine)
      : ChoiceQuestionScene(engine, "", {}, nullptr) {}

protected:
  void layout() override {
    calls("layout");
    for (std::size_t i = 0; i < options.size(); ++i)
      options[i].hitBox = {0, 100 * float(i), 100, 50};
  }
  void select(std::size_t index) override {
    calls("select" + std::to_string(index));
    ChoiceQuestionScene::select(index);
  }
  void openMenu() override { calls("menu"); }
  void openBacklog() override { calls("backlog"); }
  void renderPanel(Kotonoha::RendererRef) override { calls("panel"); }
  void renderOption(Kotonoha::RendererRef, std::size_t, bool focused) override {
    calls(focused ? "focused" : "option");
  }
};

class HookTitle : public Kotonoha::GameMenu {
public:
  Calls calls;
  using GameMenu::GameMenu;

protected:
  int itemCount() const override { return 2; }
  void activate(int index) override {
    calls("activate" + std::to_string(index));
  }
  void renderTitle(Kotonoha::RendererRef) override { calls("title"); }
  void renderItem(Kotonoha::RendererRef, int, bool) override { calls("item"); }
};

class HookMenu : public Kotonoha::InGameMenu {
public:
  Calls calls;
  using InGameMenu::InGameMenu;
  void open() override {
    calls("open");
    InGameMenu::open();
  }

protected:
  int itemCount() const override { return 3; }
  void activate(int index) override {
    calls("activate" + std::to_string(index));
  }
  void renderPanel(Kotonoha::RendererRef) override { calls("panel"); }
  void renderItem(Kotonoha::RendererRef, int, bool) override { calls("item"); }
};

class HookBacklog : public Kotonoha::VNBacklog {
public:
  Calls calls;
  bool shown = false;
  using VNBacklog::VNBacklog;
  void addEntry(const std::string &, const std::string &) override {
    calls("add");
  }
  void open() override { shown = true; }
  void close() override { shown = false; }
  bool isVisible() const override { return shown; }

protected:
  void renderFrame(Kotonoha::RendererRef) override { calls("frame"); }
  void renderEntries(Kotonoha::RendererRef) override { calls("entries"); }
};

void testTextBox(Kotonoha::Engine &engine) {
  HookTextBox box(engine);
  Kotonoha::VNTextBox &base = box;
  base.setDialogue("Ana", "Bom dia");
  require(box.calls["record"] == 1 && box.recorded == "Ana:Bom dia",
          "setDialogue records through the hook");
  SDL_Delay(2);
  base.update();
  require(base.isDialogueComplete(), "update paces with charactersPerSecond()");
  base.render(engine.GetRenderer(), 1);
  require(box.calls["frame"] == 1 && box.calls["speaker"] == 1 &&
              box.calls["dialogue"] == 1 && box.calls["indicator"] == 1,
          "render draws through the hooks");
  base.handleEvent(click(950, 520));
  base.handleEvent(click(1100, 520));
  require(box.calls["menu"] == 1 && box.calls["backlog"] == 1,
          "Menu and log buttons call the hooks");
}

void testChoice(Kotonoha::Engine &engine) {
  HookChoice choice(engine);
  Kotonoha::ChoiceQuestionScene &base = choice;
  int picked = -1;
  base.setQuestion("?", {{"a", 10}, {"b", 20}},
                   [&](int id, const std::string &) { picked = id; });
  base.render(engine.GetRenderer(), 1);
  require(choice.calls["panel"] == 1 && choice.calls["layout"] >= 1 &&
              choice.calls["focused"] == 1 && choice.calls["option"] == 1,
          "Choice renders through panel / layout / option hooks");
  base.onEvent(key(SDLK_B));
  base.onEvent(key(SDLK_ESCAPE));
  require(choice.calls["backlog"] == 1 && choice.calls["menu"] == 1,
          "Choice reaches the menus through hooks");
  base.onEvent(key(SDLK_DOWN));
  base.onEvent(key(SDLK_RETURN));
  require(choice.calls["select1"] == 1 && picked == 20 && !base.isActive(),
          "Keyboard picks through select()");
  picked = -1;
  base.setQuestion("?", {{"a", 10}, {"b", 20}},
                   [&](int id, const std::string &) { picked = id; });
  SDL_Event touch{};
  touch.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  touch.button.which = SDL_TOUCH_MOUSEID;
  touch.button.button = SDL_BUTTON_LEFT;
  touch.button.x = 50;
  touch.button.y = 125;
  base.onEvent(touch);
  require(picked == -1 && base.isActive(), "Touch waits for release");
  touch.type = SDL_EVENT_MOUSE_BUTTON_UP;
  base.onEvent(touch);
  require(picked == 20 && !base.isActive(), "Touch release selects the route");
}

void testMenus(Kotonoha::Engine &engine) {
  HookTitle title(engine);
  Kotonoha::GameMenu &titleBase = title;
  titleBase.render(engine.GetRenderer(), 1);
  require(title.calls["title"] == 1 && title.calls["item"] == 2,
          "GameMenu renders itemCount() items");
  titleBase.onEvent(key(SDLK_DOWN));
  titleBase.onEvent(key(SDLK_RETURN));
  require(title.calls["activate1"] == 1, "GameMenu activates through the hook");

  HookMenu menu(engine);
  Kotonoha::InGameMenu &menuBase = menu;
  menuBase.onEvent(key(SDLK_ESCAPE));
  require(menu.calls["open"] == 1 && menuBase.IsOpen() &&
              menuBase.pausesStory(),
          "Esc opens via open()");
  menuBase.render(engine.GetRenderer(), 1);
  require(menu.calls["panel"] == 1 && menu.calls["item"] == 3,
          "InGameMenu renders itemCount() items");
  menuBase.onEvent(key(SDLK_UP));
  menuBase.onEvent(key(SDLK_RETURN));
  require(menu.calls["activate2"] == 1,
          "InGameMenu activates through the hook");

  HookBacklog log(engine);
  Kotonoha::VNBacklog &logBase = log;
  logBase.addEntry("Ana", "Bom dia");
  logBase.open();
  require(log.calls["add"] == 1 && logBase.isVisible() &&
              logBase.capturesInput(),
          "Backlog visibility comes from isVisible()");
  logBase.render(engine.GetRenderer(), 1);
  require(log.calls["frame"] == 1 && log.calls["entries"] == 1,
          "Backlog renders through the hooks");
}

void testMenuBackdrop(Kotonoha::Engine &engine) {
  auto &renderer = engine.GetRenderer();
  Kotonoha::TexturePtr target(SDL_CreateTexture(
      &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 960, 720));
  require(bool(target), "Create 4:3 game canvas");
  auto *previous = SDL_GetRenderTarget(&renderer);
  SDL_SetRenderTarget(&renderer, target.get());
  SDL_SetRenderLogicalPresentation(&renderer, 960, 720,
                                   SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_SetRenderScale(&renderer, 1, 1);
  SDL_SetRenderViewport(&renderer, nullptr);
  SDL_SetRenderClipRect(&renderer, nullptr);
  SDL_SetRenderDrawColor(&renderer, 255, 255, 255, 255);
  SDL_RenderClear(&renderer);
  Kotonoha::InGameMenu menu(engine);
  menu.open();
  {
    Kotonoha::UI::FrameScope scope(renderer,
                                   Kotonoha::UI::designFrame({960, 720}));
    float beforeX, beforeY, afterX, afterY;
    SDL_Rect before{}, after{};
    SDL_GetRenderScale(&renderer, &beforeX, &beforeY);
    SDL_GetRenderViewport(&renderer, &before);
    menu.render(renderer, 1);
    SDL_GetRenderScale(&renderer, &afterX, &afterY);
    SDL_GetRenderViewport(&renderer, &after);
    require(beforeX == afterX && beforeY == afterY && before.x == after.x &&
                before.y == after.y && before.w == after.w &&
                before.h == after.h,
            "Backdrop preserves the fitted menu layout");
  }
  Kotonoha::SurfacePtr pixels(SDL_RenderReadPixels(&renderer, nullptr));
  require(bool(pixels), "Read dimmed canvas");
  for (const auto point :
       {SDL_Point{10, 10}, SDL_Point{950, 710}, SDL_Point{10, 360}}) {
    Uint8 red, green, blue, alpha;
    require(SDL_ReadSurfacePixel(pixels.get(), point.x, point.y, &red, &green,
                                 &blue, &alpha),
            "Read backdrop pixel");
    require(red >= 93 && red <= 96 && green == red && blue == red,
            "Pause backdrop covers the entire 4:3 canvas, including fitted "
            "margins");
  }
  SDL_SetRenderTarget(&renderer, previous);
}

void testLiveTranslation(Kotonoha::Engine &engine) {
  HookTextBox text(engine);
  auto &runtime = engine.GetRuntime();
  runtime.AddScene(&text);
  text.setDialogue("Speaker", "Original");
  text.completeDialogue();
  const auto history = text.calls["record"];
  engine.setTranslation("7", "Original", "Português ao vivo");
  require(text.getText() == "Português ao vivo" &&
              text.getVisibleText() == text.getText(),
          "Live translation updates the visible VN subclass");
  engine.setTranslation("7", "Original", "Segunda edição");
  require(text.getText() == "Segunda edição",
          "Repeated edits retain source binding");
  require(text.calls["record"] == history,
          "Live editing does not append backlog entries");
  runtime.RemoveScene(&text);
}

class EmptyRuntime : public Kotonoha::Runtime {
public:
  EmptyRuntime() : Runtime("Hooks", "1", "hooks") {}
};

} // namespace

void testSaveSuggestions(Kotonoha::Engine &engine) {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("kotonoha-save-names-" + std::to_string(SDL_GetTicksNS()));
  std::filesystem::create_directories(directory);
  Kotonoha::VNSaveSlots slots(directory.string());
  const auto first = slots.nextPath("20 de Julho (Qui)");
  require(std::filesystem::path(first).filename() == "20 de Julho (Qui).vn",
          "The novel's suggested save name is retained");
  std::ofstream(first).put('x');
  const auto second = slots.nextPath("20 de Julho (Qui)");
  require(std::filesystem::path(second).filename() ==
              "20 de Julho (Qui) (2).vn",
          "Suggested names never overwrite an existing save");
  std::string accepted;
  Kotonoha::TextInputDialog dialog(
      engine, "Save name", [&](const auto &name) { accepted = name; }, nullptr,
      "20 de Julho (Qui)");
  dialog.onEvent(key(SDLK_RETURN));
  require(accepted == "20 de Julho (Qui)",
          "The suggested name pre-fills the editable dialog");
  std::filesystem::remove_all(directory);
}

void testSurfaceOpacity() {
  using namespace Kotonoha;
  SurfacePtr source(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
  SurfacePtr destination(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
  require(bool(source) && bool(destination), "Create blend fixture");
  SDL_FillSurfaceRect(source.get(), nullptr,
                      SDL_MapSurfaceRGBA(source.get(), 255, 0, 0, 0));
  auto reset = [&] {
    SDL_FillSurfaceRect(destination.get(), nullptr,
                        SDL_MapSurfaceRGBA(destination.get(), 0, 0, 255, 255));
  };
  Uint8 r, g, b, a;
  reset();
  BlitSurfaceOpacity(*source, *destination, nullptr, nullptr, 128, false);
  SDL_ReadSurfacePixel(destination.get(), 0, 0, &r, &g, &b, &a);
  require(
      r >= 126 && r <= 129 && b >= 126 && b <= 129 && a == 255,
      "RGB copy blends global opacity over background, ignoring source mask");
  reset();
  BlitSurfaceOpacity(*source, *destination, nullptr, nullptr, 128, true);
  SDL_ReadSurfacePixel(destination.get(), 0, 0, &r, &g, &b, &a);
  require(r == 0 && b == 255, "Masked transparent pixel preserves background");
  BlitSurfaceOpacity(*source, *destination, nullptr, nullptr, 0, false);
  SDL_ReadSurfacePixel(destination.get(), 0, 0, &r, &g, &b, &a);
  require(r == 0 && b == 255, "Zero global opacity preserves background");
  BlitSurfaceOpacity(*source, *destination, nullptr, nullptr, 255, false);
  SDL_ReadSurfacePixel(destination.get(), 0, 0, &r, &g, &b, &a);
  require(r == 255 && b == 0 && a == 0,
          "Unblended copies preserve alpha for later masked copies");
  SDL_ReadSurfacePixel(source.get(), 0, 0, &r, &g, &b, &a);
  require(a == 0, "RGB view preserves original pixels");
  reset();
  SDL_FillSurfaceRect(source.get(), nullptr,
                      SDL_MapSurfaceRGBA(source.get(), 255, 0, 0, 128));
  BlitSurfaceOpacity(*source, *destination, nullptr, nullptr, 128, true);
  SDL_ReadSurfacePixel(destination.get(), 0, 0, &r, &g, &b, &a);
  require(r >= 62 && r <= 66 && b >= 189 && b <= 193,
          "Masked blits multiply per-pixel alpha by global opacity");
  BlitSurfaceOpacity(*source, *source, nullptr, nullptr, 255, false);
  SDL_ReadSurfacePixel(source.get(), 0, 0, &r, &g, &b, &a);
  require(r == 255 && b == 0 && a == 128,
          "Aliased unblended copy preserves the source mask");
}

void testNativeButtons() {
  Kotonoha::ImageButtonBar bar;
  int activated = 0;
  bar.buttons = {{{20, 20, 12, 7}, [&] { ++activated; }}};
  auto down = click(25, 23);
  require(bar.handle(down) && bar.state(0) == 2 && activated == 0,
          "Native sprite controls show press state without activating early");
  auto up = down;
  up.type = SDL_EVENT_MOUSE_BUTTON_UP;
  up.button.x = 100;
  require(bar.handle(up) && activated == 0,
          "Releasing outside cancels a native control");
  down.button.which = SDL_TOUCH_MOUSEID;
  down.button.y = 33;
  require(bar.handle(down),
          "Small original controls have a larger touch target");
  up = down;
  up.type = SDL_EVENT_MOUSE_BUTTON_UP;
  require(bar.handle(up) && activated == 1 && bar.hovered == -1,
          "Touch activates once and releases the sprite hover state");
  bar.handle(down);
  SDL_Event lost{};
  lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  bar.handle(lost);
  bar.handle(up);
  require(activated == 1, "Focus loss cancels a pending sprite activation");
  SDL_Event confirm{};
  confirm.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  confirm.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
  require(!bar.handle(confirm),
          "An unfocused sprite bar leaves dialogue confirmation to the story");
  SDL_Event navigate = confirm;
  navigate.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
  require(bar.handle(navigate) && bar.hovered == 0,
          "Controller navigation focuses native sprite controls");
  require(bar.handle(confirm) && activated == 2,
          "Controller confirmation activates the focused sprite control once");
  confirm.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
  require(!bar.handle(confirm) && activated == 2,
          "Controller release never activates sprite controls twice");
}

void testStableReveal() {
  auto measure = [](const std::string &s) { return float(s.size()); };
  const std::string full = "one lengthyword end";
  const auto before =
      Kotonoha::VNTextBoxUtility::RevealLines(full, 7, measure, 10);
  const auto after =
      Kotonoha::VNTextBoxUtility::RevealLines(full, 10, measure, 10);
  require(before.size() == 2 && after.size() == 2 && before[0] == after[0] &&
              before[1] == "len" && after[1] == "length",
          "Revealing a word must not move already visible text between lines");
}

class NativeHistory final : public Kotonoha::VNBacklog {
public:
  int marker;
  NativeHistory(Kotonoha::Engine &engine, int value)
      : VNBacklog(engine), marker(value) {}
};

void testNativeHistoryInjection(Kotonoha::Engine &engine) {
  auto &native =
      engine.configureSharedScene<Kotonoha::VNBacklog, NativeHistory>(42);
  require(&Kotonoha::GetVNBacklogRuntime(engine) == &native &&
              native.marker == 42,
          "Generic history helpers resolve the game's injected native "
          "implementation");
  require(&engine.configureSharedScene<Kotonoha::VNBacklog, NativeHistory>(7) ==
                  &native &&
              native.marker == 42,
          "Repeated native component binding preserves the active component");
  native.addEntry("Ana", "A voiced native page");
  native.setLastVoice("voice/page.ogg");
  std::string replayed;
  native.setVoiceReplay([&](const std::string &id) { replayed = id; });
  require(native.replayEntry(0) && replayed == "voice/page.ogg",
          "History entries retain voice identity for native replay controls");
  native.addEntry("Ana", "An unvoiced page");
  require(!native.replayEntry(1) && !native.replayEntry(2),
          "Voice replay rejects unvoiced and out-of-range pages");
  native.setVoiceReplay({});
  native.clear();
  require(native.getEntries().empty() && !native.replayEntry(0),
          "Clearing native history also removes voice references");
  bool rejected = false;
  try {
    engine.configureSharedScene<Kotonoha::VNBacklog, HookBacklog>();
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected && &Kotonoha::GetVNBacklogRuntime(engine) == &native,
          "An active shared component cannot be replaced with a dangling "
          "implementation");
}

int main() {
  try {
    {
      EmptyRuntime runtime;
      Kotonoha::Engine engine(runtime);
      testNativeHistoryInjection(engine);
      testSaveSuggestions(engine);
      testNativeButtons();
      testSurfaceOpacity();
      testStableReveal();
      testTextBox(engine);
      testLiveTranslation(engine);
      testChoice(engine);
      testMenus(engine);
      testMenuBackdrop(engine);
      Kotonoha::GetInGameMenuRuntime(engine).open();
    }
    {
      EmptyRuntime runtime;
      Kotonoha::Engine engine(runtime);
      require(!Kotonoha::GetInGameMenuRuntime(engine).IsOpen(),
              "A new game session must not inherit a previous engine's menu");
    }
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Component hook tests passed\n";
  return 0;
}
