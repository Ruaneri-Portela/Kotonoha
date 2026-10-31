// The engine components are extended by overriding their protected hooks: the
// base classes keep input routing and state, and call back into the subclass.
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Runtime.hpp>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
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
  void recordDialogue(const std::string &speaker, const std::string &text) override {
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
public:
  Calls calls;
  explicit HookChoice(Kotonoha::Engine &engine) : ChoiceQuestionScene(engine, "", {}, nullptr) {}

protected:
  void layout() override {
    calls("layout");
    for (std::size_t i = 0; i < options.size(); ++i) options[i].hitBox = {0, 100 * float(i), 100, 50};
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
  void activate(int index) override { calls("activate" + std::to_string(index)); }
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
  void activate(int index) override { calls("activate" + std::to_string(index)); }
  void renderPanel(Kotonoha::RendererRef) override { calls("panel"); }
  void renderItem(Kotonoha::RendererRef, int, bool) override { calls("item"); }
};

class HookBacklog : public Kotonoha::VNBacklog {
public:
  Calls calls;
  bool shown = false;
  using VNBacklog::VNBacklog;
  void addEntry(const std::string &, const std::string &) override { calls("add"); }
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
  require(box.calls["record"] == 1 && box.recorded == "Ana:Bom dia", "setDialogue records through the hook");
  SDL_Delay(2);
  base.update();
  require(base.isDialogueComplete(), "update paces with charactersPerSecond()");
  base.render(engine.GetRenderer(), 1);
  require(box.calls["frame"] == 1 && box.calls["speaker"] == 1 && box.calls["dialogue"] == 1 &&
              box.calls["indicator"] == 1,
          "render draws through the hooks");
  base.handleEvent(click(950, 520));
  base.handleEvent(click(1100, 520));
  require(box.calls["menu"] == 1 && box.calls["backlog"] == 1, "Menu and log buttons call the hooks");
}

void testChoice(Kotonoha::Engine &engine) {
  HookChoice choice(engine);
  Kotonoha::ChoiceQuestionScene &base = choice;
  int picked = -1;
  base.setQuestion("?", {{"a", 10}, {"b", 20}}, [&](int id, const std::string &) { picked = id; });
  base.render(engine.GetRenderer(), 1);
  require(choice.calls["panel"] == 1 && choice.calls["layout"] >= 1 && choice.calls["focused"] == 1 &&
              choice.calls["option"] == 1,
          "Choice renders through panel / layout / option hooks");
  base.onEvent(key(SDLK_B));
  base.onEvent(key(SDLK_ESCAPE));
  require(choice.calls["backlog"] == 1 && choice.calls["menu"] == 1, "Choice reaches the menus through hooks");
  base.onEvent(key(SDLK_DOWN));
  base.onEvent(key(SDLK_RETURN));
  require(choice.calls["select1"] == 1 && picked == 20 && !base.isActive(), "Keyboard picks through select()");
}

void testMenus(Kotonoha::Engine &engine) {
  HookTitle title(engine);
  Kotonoha::GameMenu &titleBase = title;
  titleBase.render(engine.GetRenderer(), 1);
  require(title.calls["title"] == 1 && title.calls["item"] == 2, "GameMenu renders itemCount() items");
  titleBase.onEvent(key(SDLK_DOWN));
  titleBase.onEvent(key(SDLK_RETURN));
  require(title.calls["activate1"] == 1, "GameMenu activates through the hook");

  HookMenu menu(engine);
  Kotonoha::InGameMenu &menuBase = menu;
  menuBase.onEvent(key(SDLK_ESCAPE));
  require(menu.calls["open"] == 1 && menuBase.IsOpen() && menuBase.pausesStory(), "Esc opens via open()");
  menuBase.render(engine.GetRenderer(), 1);
  require(menu.calls["panel"] == 1 && menu.calls["item"] == 3, "InGameMenu renders itemCount() items");
  menuBase.onEvent(key(SDLK_UP));
  menuBase.onEvent(key(SDLK_RETURN));
  require(menu.calls["activate2"] == 1, "InGameMenu activates through the hook");

  HookBacklog log(engine);
  Kotonoha::VNBacklog &logBase = log;
  logBase.addEntry("Ana", "Bom dia");
  logBase.onEvent(key(SDLK_B));
  require(log.calls["add"] == 1 && logBase.isVisible() && logBase.capturesInput(),
          "Backlog visibility comes from isVisible()");
  logBase.render(engine.GetRenderer(), 1);
  require(log.calls["frame"] == 1 && log.calls["entries"] == 1, "Backlog renders through the hooks");
}

class EmptyRuntime : public Kotonoha::Runtime {
public:
  EmptyRuntime() : Runtime("Hooks", "1", "hooks") {}
};

} // namespace

int main() {
  try {
    EmptyRuntime runtime;
    Kotonoha::Engine engine(runtime);
    testTextBox(engine);
    testChoice(engine);
    testMenus(engine);
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Component hook tests passed\n";
  return 0;
}
