#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/core/UI.hpp>

namespace Kotonoha {

GameMenu::GameMenu(Engine &engineRef) : Scene(engineRef) { isOpen = true; }

std::string GameMenu::itemLabel(int index) const {
  static const char *labels[]{"New Game", "Load Game", "Settings",
                              "Quit Game"};
  return index >= 0 && index < 4 ? labels[index] : "";
}

SDL_FRect GameMenu::itemRect(int index) const {
  return {460, 250.0f + index * 60, 360, 48};
}

void GameMenu::activate(int index) {
  if (index == 0) {
    engine.GetRuntime().SetState(GameState::Gameplay);
    engine.GetRuntime().init(engine);
  } else if (index == 1) {
    if (onResumeCb)
      onResumeCb();
    else
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No load handler registered");
  } else if (index == 2) {
    if (onSettingsCb)
      onSettingsCb();
    else
      engine.openSettings();
  } else if (index == 3) {
    if (onQuitCb)
      onQuitCb();
    else {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
  }
}

void GameMenu::onEvent(EventRef event) {
  if (!isVisible())
    return;
  const int count = itemCount();
  if (count <= 0)
    return;
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_DOWN)
      selected = (selected + 1) % count;
    if (event.key.key == SDLK_UP)
      selected = (selected + count - 1) % count;
    if (event.key.key == SDLK_RETURN) {
      activate(selected);
      return;
    }
  }
  for (int i = 0; i < count; ++i)
    if (UI::clicked(event, itemRect(i))) {
      activate(i);
      return;
    }
}

void GameMenu::update() {}

void GameMenu::render(RendererRef renderer, float) {
  if (!isVisible())
    return;
  renderTitle(renderer);
  for (int i = 0; i < itemCount(); ++i)
    renderItem(renderer, i, selected == i);
}

void GameMenu::renderTitle(RendererRef renderer) {
  engine.GetUI().text(renderer, "Kotonoha", 460, 170, 360,
                      {255, 215, 100, 255});
}

void GameMenu::renderItem(RendererRef renderer, int index, bool focused) {
  engine.GetUI().button(renderer, itemRect(index), itemLabel(index), focused);
}

GameMenu &GetGameMenuRuntime(Engine &engineRef) {
  static GameMenu menu{engineRef};
  return menu;
}

} // namespace Kotonoha
