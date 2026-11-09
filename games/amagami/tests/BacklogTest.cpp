#include "AmagamiRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
class TestRuntime final : public Kotonoha::Runtime {
public:
  TestRuntime() : Runtime("AmagamiBacklogTest", "1", "amagami-backlog-test") {}
};
void key(Amagami::AmagamiBacklog &scene, SDL_Keycode value) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = value;
  scene.onEvent(event);
}
} // namespace

int main() {
  try {
    TestRuntime runtime;
    Kotonoha::EngineOptions options;
    options.persistSettings = false;
    options.rendererOverride = "software";
    Kotonoha::Engine engine(runtime, options);
    runtime.SetState(Kotonoha::GameState::Gameplay);
    Amagami::AmagamiBacklog backlog(engine);
    runtime.AddScene(&backlog, Kotonoha::GameState::Gameplay);
    std::vector<Amagami::Dialogue> history;
    for (int i = 0; i < 80; ++i) {
      Amagami::Dialogue line;
      line.speaker = "Miya";
      line.original = "V1858original line " + std::to_string(i);
      line.text = "History line " + std::to_string(i);
      line.voice = 1858 + i;
      line.voiceCode = "MIY";
      history.push_back(std::move(line));
    }
    backlog.setHistory(history, 1227);
    std::string replay;
    backlog.setVoiceReplay([&](const auto &voice) { replay = voice; });
    int closed = 0;
    backlog.onClose = [&] { ++closed; };
    backlog.open();
    require(backlog.getEntries().size() == 80,
            "Native backlog retains the full history instead of four entries");
    require(backlog.capturesInput() && backlog.pausesStory(),
            "Native backlog uses engine modal input and story pause");
    require(backlog.focusedEntry() == 79,
            "History initially focuses the newest page");
    backlog.render(engine.GetRenderer(), 1);
    const float newestScroll = backlog.scrollTarget();
    key(backlog, SDLK_UP);
    require(backlog.focusedEntry() == 78 &&
                backlog.scrollTarget() < newestScroll,
            "Up scrolls the history even when the cursor is already visible");
    const float afterUpScroll = backlog.scrollTarget();
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1;
    wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    backlog.onEvent(wheel);
    require(backlog.focusedEntry() == 77 &&
                backlog.scrollTarget() < afterUpScroll,
            "Mouse wheel scrolls the history on every selection step");
    const float afterWheelScroll = backlog.scrollTarget();
    SDL_Event pad{};
    pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    pad.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    backlog.onEvent(pad);
    require(backlog.focusedEntry() == 76 &&
                backlog.scrollTarget() < afterWheelScroll,
            "Directional pad keeps scrolling as the selection moves upward");
    const float afterPadScroll = backlog.scrollTarget();
    SDL_Event analog{};
    analog.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    analog.gaxis.which = 9;
    analog.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTY;
    analog.gaxis.value = -32768;
    engine.Event(&analog);
    require(backlog.focusedEntry() == 75 &&
                backlog.scrollTarget() < afterPadScroll,
            "Left analog stick scrolls the backlog through the engine");
    analog.gaxis.value = 0;
    engine.Event(&analog);
    require(backlog.focusedEntry() == 75,
            "Returning the analog stick to center does not scroll again");
    key(backlog, SDLK_HOME);
    require(backlog.focusedEntry() == 0, "Oldest dialogue remains reachable");
    key(backlog, SDLK_DOWN);
    require(backlog.focusedEntry() == 1 && backlog.scrollTarget() > 0,
            "Moving down scrolls back toward the newer pages");
    key(backlog, SDLK_HOME);
    key(backlog, SDLK_RETURN);
    require(replay == "voice/MIY/1858" && backlog.isVisible(),
            "Confirm replays selected voice without dismissing the history");
    key(backlog, SDLK_UP);
    require(backlog.focusedEntry() == 0,
            "Cursor cannot move before first page");
    key(backlog, SDLK_END);
    key(backlog, SDLK_DOWN);
    require(backlog.focusedEntry() == 79, "Cursor cannot move after last page");
    key(backlog, SDLK_PAGEUP);
    require(backlog.isVisible() && backlog.focusedEntry() == 76,
            "PageUp scrolls history rather than closing it");
    SDL_Event confirm{};
    confirm.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    confirm.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    backlog.onEvent(confirm);
    require(replay == "voice/MIY/1934",
            "Original Circle button replays the focused historical page");
    engine.setTranslation(
        "1227", history.back().original,
        "A longer translated line in the native history window.");
    backlog.render(engine.GetRenderer(), 1);
    SDL_Event closePad{};
    closePad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    closePad.gbutton.button = SDL_GAMEPAD_BUTTON_START;
    backlog.onEvent(closePad);
    require(!backlog.isVisible() && !backlog.capturesInput() && closed == 1,
            "Controller Start closes the native backlog");
    backlog.open();
    key(backlog, SDLK_HOME);
    // A touchscreen contact selects on release, so dragging can scroll before
    // any voice is replayed. A physical mouse retains immediate activation.
    int replays = 0;
    backlog.setVoiceReplay([&](const auto &) { ++replays; });
    SDL_Event touch{};
    touch.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    touch.button.button = SDL_BUTTON_LEFT;
    touch.button.which = SDL_TOUCH_MOUSEID;
    touch.button.x = 50;
    touch.button.y = 20;
    backlog.onEvent(touch);
    require(replays == 0, "Touch-down does not replay a history entry");
    touch.type = SDL_EVENT_MOUSE_BUTTON_UP;
    backlog.onEvent(touch);
    require(replays == 1, "Touch release replays the selected entry once");
    touch.button.which = 0;
    backlog.onEvent(touch);
    require(replays == 1, "Physical mouse release does not replay an entry");
    touch.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    backlog.onEvent(touch);
    require(replays == 2, "Physical mouse press replays an entry immediately");
    touch.button.which = SDL_TOUCH_MOUSEID;
    touch.type = SDL_EVENT_MOUSE_BUTTON_UP;
    SDL_Event drag{};
    drag.type = SDL_EVENT_FINGER_DOWN;
    drag.tfinger.touchID = 1;
    drag.tfinger.fingerID = 9;
    drag.tfinger.x = .2f;
    drag.tfinger.y = .3f;
    backlog.onEvent(drag);
    drag.type = SDL_EVENT_FINGER_MOTION;
    drag.tfinger.y = .5f;
    backlog.onEvent(drag);
    drag.type = SDL_EVENT_FINGER_UP;
    backlog.onEvent(drag);
    backlog.onEvent(touch);
    require(replays == 2,
            "Scrolling history never replays a voice on synthetic release");
    key(backlog, SDLK_ESCAPE);
    backlog.setHistory({}, 1227);
    backlog.open();
    key(backlog, SDLK_RETURN);
    backlog.render(engine.GetRenderer(), 1);
    key(backlog, SDLK_ESCAPE);
    runtime.RemoveScene(&backlog);
    std::cout << "Amagami native backlog scroll, focus, voice replay and "
                 "translation passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
