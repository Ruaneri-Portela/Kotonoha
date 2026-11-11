#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/SettingsMenu.hpp>
#include <Kotonoha/components/SplashScreen.hpp>
#include <Kotonoha/components/TextInputDialog.hpp>
#include <Kotonoha/components/TextInputForm.hpp>
#include <Kotonoha/components/VNManager.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Kotonoha;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
SDL_Event pad(Uint8 button) {
  SDL_Event event{};
  event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  event.gbutton.button = button;
  event.gbutton.down = true;
  return event;
}
SDL_Event key(SDL_Keycode code) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = code;
  return event;
}
SDL_Event finger(Uint32 type, float x, float y, SDL_FingerID id = 1) {
  SDL_Event event{};
  event.type = type;
  event.tfinger.touchID = 1;
  event.tfinger.fingerID = id;
  event.tfinger.x = x;
  event.tfinger.y = y;
  return event;
}
SDL_Event release(float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.which = SDL_TOUCH_MOUSEID;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  return event;
}
class TestRuntime : public Runtime {
public:
  TestRuntime() : Runtime("Native input", "1", "native-input-test") {}
};
class TextBox : public VNTextBox {
public:
  int menus = 0, logs = 0;
  explicit TextBox(Engine &engine) : VNTextBox(engine, "", "Text") {}

protected:
  void openMenu() override { ++menus; }
  void openBacklog() override { ++logs; }
};
void choices(Engine &engine) {
  int choice = -1;
  ChoiceQuestionScene scene(
      engine, "Choose", {{"First", 0}, {"Second", 1}},
      [&](int value, const std::string &) { choice = value; });
  scene.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
  scene.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST));
  require(choice == 1, "Raw controller navigates and confirms native choices");
  auto reset = [&] {
    choice = -1;
    scene.setQuestion("Choose", {{"First", 0}, {"Second", 1}},
                      [&](int value, const std::string &) { choice = value; });
  };
  reset();
  scene.onEvent(finger(SDL_EVENT_FINGER_DOWN, .3f, .3f));
  scene.onEvent(finger(SDL_EVENT_FINGER_MOTION, .3f, .1f));
  scene.onEvent(finger(SDL_EVENT_FINGER_UP, .3f, .1f));
  scene.onEvent(release(250, 205));
  require(choice == -1 && scene.isActive(),
          "A touch scroll cannot select a choice on release");
  scene.onEvent(finger(SDL_EVENT_FINGER_DOWN, .3f, .3f));
  scene.onEvent(finger(SDL_EVENT_FINGER_UP, .3f, .3f));
  scene.onEvent(release(250, 205));
  require(choice == 0, "A fresh touch tap selects after a canceled scroll");
  reset();
  scene.onEvent(finger(SDL_EVENT_FINGER_DOWN, .3f, .3f));
  scene.onEvent(finger(SDL_EVENT_FINGER_CANCELED, .3f, .3f));
  scene.onEvent(release(250, 205));
  require(choice == -1, "Canceled touch never confirms a choice");
  scene.hide();
  require(!scene.onEvent(release(250, 205)),
          "An inactive choice cannot consume a canceled touch release");
}
void settings(Engine &engine) {
  auto &menu = engine.GetSettingsMenu();
  const float original = engine.GetSettings().masterVolume;
  menu.open();
  menu.onEvent(key(SDLK_TAB)); // Sound page, selected master volume.
  menu.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
  menu.onEvent(key(SDLK_END)); // Apply and save.
  menu.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST));
  require(!menu.isOpen(),
          "Controller confirmation applies and closes settings");
  require(std::abs(engine.GetSettings().masterVolume -
                   std::max(0.f, original - .05f)) < .001f,
          "Controller left adjusts native settings sliders");
  menu.open();
  menu.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH));
  require(!menu.isOpen(), "Controller cancel closes settings");
}
void story(Engine &engine) {
  VNManager manager(engine);
  ScriptNode line;
  line.id = 0;
  line.text = "A line";
  line.nextNodeId = -1;
  manager.loadScript({line});
  manager.startScript();
  GetVNTextBoxRuntime(engine, "", "", 30).completeDialogue();
  manager.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST));
  require(manager.isFinished(),
          "Raw controller confirms the generic VN script");
  manager.startScript();
  GetVNTextBoxRuntime(engine, "", "", 30).completeDialogue();
  manager.onEvent(finger(SDL_EVENT_FINGER_DOWN, .3f, .3f));
  auto down = release(250, 205);
  down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  manager.onEvent(down);
  require(!manager.isFinished(),
          "Touch press waits for release before story advance");
  manager.onEvent(finger(SDL_EVENT_FINGER_MOTION, .3f, .1f));
  manager.onEvent(finger(SDL_EVENT_FINGER_UP, .3f, .1f));
  manager.onEvent(release(250, 205));
  require(!manager.isFinished(),
          "Touch scrolling cannot advance the generic VN story");
  manager.onEvent(finger(SDL_EVENT_FINGER_DOWN, .3f, .3f));
  manager.onEvent(finger(SDL_EVENT_FINGER_UP, .3f, .3f));
  manager.onEvent(release(250, 205));
  require(manager.isFinished(), "Touch tap advances the generic VN story once");
  SplashScreen splash(engine, {"first.png", "second.png"});
  splash.handleEvent(pad(SDL_GAMEPAD_BUTTON_EAST));
  require(!splash.finished(),
          "Splash controller confirmation advances only one item");
  splash.handleEvent(key(SDLK_RETURN));
  require(splash.finished(),
          "Keyboard and controller share splash confirmation");
}
void dialogs(Engine &engine) {
  std::vector<std::string> submitted;
  TextInputForm form(engine);
  form.configure(
      "Names", {{"First", "A", "A"}, {"Second", "B", "B"}},
      [&](const auto &values) {
        submitted = values;
        return std::string{};
      },
      [] {});
  form.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
  SDL_Event text{};
  text.type = SDL_EVENT_TEXT_INPUT;
  text.text.text = "!";
  require(form.onEvent(text), "Text input is consumed by the focused form");
  form.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST));
  require(
      submitted == std::vector<std::string>({"A", "B!"}),
      "Controller changes the text form field and submits its complete values");
  form.close();
  bool canceled = false;
  TextInputDialog dialog(
      engine, "Save", [](const auto &) {}, [&] { canceled = true; });
  dialog.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH));
  require(canceled, "Controller cancel reaches the native text dialog");
  bool stoppedBeforeCallback = false;
  dialog.configure(
      "Save",
      [&](const auto &) {
        stoppedBeforeCallback = !SDL_TextInputActive(&engine.GetWindow());
        // A following dialog may start editing synchronously in this callback.
        SDL_StartTextInput(&engine.GetWindow());
      },
      [] {});
  dialog.update();
  dialog.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST));
  require(stoppedBeforeCallback && SDL_TextInputActive(&engine.GetWindow()),
          "Submitting stops the old editor without stopping one opened by its "
          "callback");
  SDL_StopTextInput(&engine.GetWindow());
  TextBox box(engine);
  box.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH));
  box.handleEvent(pad(SDL_GAMEPAD_BUTTON_BACK));
  require(box.menus == 1 && box.logs == 1,
          "Both textbox event APIs use native controller navigation");
}
} // namespace
int main() {
  try {
    TestRuntime runtime;
    EngineOptions options;
    options.persistSettings = false;
    options.rendererOverride = "software";
    Engine engine(runtime, options);
    choices(engine);
    settings(engine);
    dialogs(engine);
    story(engine);
    std::cout << "Native controller and touch input passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
