#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/UI.hpp>
#include <algorithm>
#include <stdexcept>

namespace Kotonoha {

ChoiceQuestionScene::ChoiceQuestionScene(
    Engine &engineRef, std::string question, std::vector<ChoiceOption> choices,
    std::function<void(int, const std::string &)> callback)
    : Scene(engineRef), questionText(std::move(question)),
      options(std::move(choices)), onChoiceSelected(std::move(callback)) {
  active = !options.empty();
}

void ChoiceQuestionScene::layout() {
  const auto area = listArea();
  float y = area.y - scroll;
  for (auto &option : options) {
    const float h =
        std::max(48.0f, engine.GetUI().textHeight(engine.GetRenderer(),
                                                  option.label, 796) +
                            20);
    option.hitBox = {area.x, y, area.w, h};
    y += h + 12;
  }
}

void ChoiceQuestionScene::select(std::size_t index) {
  const auto option = options.at(index);
  const auto callback = onChoiceSelected;
  active = false;
  if (callback)
    callback(option.optionId, option.label);
}

void ChoiceQuestionScene::openMenu() { GetInGameMenuRuntime(engine).open(); }
void ChoiceQuestionScene::openBacklog() { GetVNBacklogRuntime(engine).open(); }

void ChoiceQuestionScene::onEvent(EventRef event) {
  if (!active || options.empty())
    return;
  layout();
  const auto area = listArea();
  const float top = area.y, bottom = area.y + area.h;
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_B) {
      openBacklog();
      return;
    }
    if (event.key.key == SDLK_ESCAPE) {
      openMenu();
      return;
    }
    if (event.key.key == SDLK_DOWN)
      selected = (selected + 1) % options.size();
    else if (event.key.key == SDLK_UP)
      selected = (selected + options.size() - 1) % options.size();
    else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
      select(selected);
      return;
    }
    if (options[selected].hitBox.y < top)
      scroll += options[selected].hitBox.y - top;
    if (options[selected].hitBox.y + options[selected].hitBox.h > bottom)
      scroll += options[selected].hitBox.y + options[selected].hitBox.h - bottom;
    scroll = std::max(0.0f, scroll);
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    const auto &last = options.back().hitBox;
    const float maxScroll = std::max(0.0f, last.y + last.h + scroll - bottom);
    scroll = std::clamp(scroll - event.wheel.y * 60, 0.0f, maxScroll);
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.y >= top &&
      event.button.y < bottom) {
    for (std::size_t i = 0; i < options.size(); ++i)
      if (UI::clicked(event, options[i].hitBox)) {
        select(i);
        return;
      }
  }
}

void ChoiceQuestionScene::update() {}

void ChoiceQuestionScene::render(RendererRef renderer, float) {
  if (!active)
    return;
  renderPanel(renderer);
  layout();
  const auto area = listArea();
  const SDL_Rect clip{static_cast<int>(area.x), static_cast<int>(area.y),
                      static_cast<int>(area.w), static_cast<int>(area.h)};
  SDL_SetRenderClipRect(&renderer, &clip);
  for (std::size_t i = 0; i < options.size(); ++i)
    renderOption(renderer, i, selected == i);
  SDL_SetRenderClipRect(&renderer, nullptr);
}

void ChoiceQuestionScene::renderPanel(RendererRef renderer) {
  auto &ui = engine.GetUI();
  ui.panel(renderer, {210, 80, 860, 570});
  ui.text(renderer, questionText, 230, 100, 820, {255, 215, 100, 255});
}

void ChoiceQuestionScene::renderOption(RendererRef renderer,
                                       std::size_t index, bool focused) {
  engine.GetUI().button(renderer, options[index].hitBox, options[index].label,
                        focused);
}

ChoiceQuestionScene &GetChoiceQuestionRuntime(
    Engine &engineRef, const std::string &question,
    const std::vector<ChoiceOption> &options,
    std::function<void(int, const std::string &)> callback) {
  static ChoiceQuestionScene scene{engineRef, question, options, callback};
  return scene;
}

} // namespace Kotonoha
