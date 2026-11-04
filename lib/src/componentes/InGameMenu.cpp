#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

namespace Kotonoha {

InGameMenu::InGameMenu(Engine &engineRef) : Scene(engineRef) {}

std::string InGameMenu::itemLabel(int index) const {
  static const char *labels[]{"Continue", "History [B]", "Settings",
                              "Save...",  "Load...",     "Main Menu"};
  if (index >= 0 && index < 6)
    return labels[index];
  if (index == 6)
    return manualUnpause ? "Game Pause: [Off]" : "Game Pause: [On]";
  return "";
}

SDL_FRect InGameMenu::itemRect(int index) const {
  if (index == 6)
    return {460, 500, 360, 44};
  return {460, 160.0f + index * 54, 360, 44};
}

void InGameMenu::openBacklog() { GetVNBacklogRuntime(engine).open(); }

void InGameMenu::activate(int index) {
  if (index == 6) {
    manualUnpause = !manualUnpause;
    return;
  }
  if (index == 0) {
    close();
    if (onResumeCb)
      onResumeCb();
  } else if (index == 1) {
    close();
    openBacklog();
  } else if (index == 2) {
    if (onSettingsCb)
      onSettingsCb();
    else
      engine.openSettings();
  } else if (index == 3 || index == 4) {
    const auto callback = index == 3 ? onSaveCb : onLoadCb;
    if (callback)
      callback();
    else
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "No save/load handler registered");
  } else {
    close();
    engine.GetTimeline().stop();
    if (onReturnToMenuCb)
      onReturnToMenuCb();
    else {
      engine.GetRuntime().SetState(GameState::Menu);
      engine.GetRuntime().init(engine);
    }
  }
}

bool InGameMenu::onEvent(EventRef rawEvent) {
  const auto event = MapControllerEvent(rawEvent);
  if (!touchActivation.accept(event))
    return true;
  const int count = itemCount();

  if (BackAction(engine, event)) {
    toggle();
    return true;
  }

  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (!IsOpen() || count <= 0)
      return false;
    if (event.key.key == SDLK_B) {
      activate(1);
      return true;
    }
    if (event.key.key == SDLK_DOWN) {
      selected = (selected + 1) % count;
      return true;
    }
    if (event.key.key == SDLK_UP) {
      selected = (selected + count - 1) % count;
      return true;
    }
    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
      activate(selected);
      return true;
    }
  }
  if (!IsOpen())
    return false;
  for (int i = 0; i < count; ++i)
    if (UI::clicked(event, itemRect(i))) {
      activate(i);
      return true;
    }
  return false;
}

void InGameMenu::update() {}

void InGameMenu::render(RendererRef renderer, float) {
  if (!IsOpen())
    return;
  renderPanel(renderer);
  for (int i = 0; i < itemCount(); ++i)
    renderItem(renderer, i, selected == i);
}

void InGameMenu::renderPanel(RendererRef renderer) {
  auto &ui = engine.GetUI();
  ui.backdrop(renderer, {0, 0, 0, 160});
  ui.panel(renderer, {440, 130, 400, 440});
  ui.text(renderer, "Paused", 460, 150, 360, {255, 215, 100, 255});
}

void InGameMenu::renderItem(RendererRef renderer, int index, bool focused) {
  engine.GetUI().button(renderer, itemRect(index), itemLabel(index), focused);
}

InGameMenu &GetInGameMenuRuntime(Engine &engineRef) {
  auto &menu = engineRef.sharedScene<InGameMenu>();
  return menu;
}

} // namespace Kotonoha
