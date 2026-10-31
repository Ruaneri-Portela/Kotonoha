#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

namespace Kotonoha {

InGameMenu::InGameMenu(Engine &engineRef) : Scene(engineRef) {}

std::string InGameMenu::itemLabel(int index) const {
  static const char *labels[]{"Continue", "History [B]", "Settings",
                              "Save...",  "Load...",     "Main Menu"};
  return index >= 0 && index < 6 ? labels[index] : "";
}

SDL_FRect InGameMenu::itemRect(int index) const {
  return {460, 200.0f + index * 60, 360, 48};
}

void InGameMenu::openBacklog() { GetVNBacklogRuntime(engine).open(); }

void InGameMenu::activate(int index) {
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

void InGameMenu::onEvent(EventRef event) {
  const int count = itemCount();
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE) {
      toggle();
      return;
    }
    if (!IsOpen() || count <= 0)
      return;
    if (event.key.key == SDLK_B) {
      activate(1);
      return;
    }
    if (event.key.key == SDLK_DOWN)
      selected = (selected + 1) % count;
    if (event.key.key == SDLK_UP)
      selected = (selected + count - 1) % count;
    if (event.key.key == SDLK_RETURN) {
      activate(selected);
      return;
    }
  }
  if (!IsOpen())
    return;
  for (int i = 0; i < count; ++i)
    if (UI::clicked(event, itemRect(i))) {
      activate(i);
      return;
    }
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
  ui.panel(renderer, {0, 0, UI::width, UI::height}, {0, 0, 0, 160});
  ui.panel(renderer, {440, 130, 400, 440});
  ui.text(renderer, "Paused", 460, 150, 360, {255, 215, 100, 255});
}

void InGameMenu::renderItem(RendererRef renderer, int index, bool focused) {
  engine.GetUI().button(renderer, itemRect(index), itemLabel(index), focused);
}

InGameMenu &GetInGameMenuRuntime(Engine &engineRef) {
  static InGameMenu menu{engineRef};
  return menu;
}

} // namespace Kotonoha
