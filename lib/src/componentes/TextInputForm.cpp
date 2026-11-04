#include <Kotonoha/componentes/TextInputForm.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <stdexcept>
namespace Kotonoha {
TextInputForm::~TextInputForm() {
  if (active)
    SDL_StopTextInput(&engine.GetWindow());
}
void TextInputForm::configure(std::string caption, std::vector<Field> values,
                              Confirm accept, std::function<void()> reject) {
  if (values.empty() || values.size() > 4 || !accept)
    throw std::invalid_argument(
        "A text form requires one to four fields and a submit callback");
  touchActivation.reset();
  title = std::move(caption);
  fields = std::move(values);
  confirm = std::move(accept);
  cancel = std::move(reject);
  focus = 0;
  error.clear();
  active = true;
}
void TextInputForm::close() {
  active = false;
  SDL_StopTextInput(&engine.GetWindow());
}
void TextInputForm::submit(const std::vector<std::string> &values) {
  if (!active || values.size() != fields.size())
    throw std::invalid_argument("Invalid text form submission");
  for (size_t i = 0; i < values.size(); ++i)
    fields[i].value = values[i];
  const auto callback = confirm;
  error = callback(values);
}
bool TextInputForm::onEvent(EventRef rawEvent) {
  if (!active)
    return false;
  const auto event = MapControllerEvent(rawEvent);
  if (!touchActivation.accept(event))
    return true;
  if (BackAction(engine, event) || UI::clicked(event, {850, 565, 180, 48})) {
    const auto callback = cancel;
    close();
    if (callback)
      callback();
    return true;
  }
  for (size_t i = 0; i < fields.size(); ++i)
    if (UI::clicked(event, {510, 230.0f + 70 * i, 500, 48}))
      focus = i;
  if (event.type == SDL_EVENT_TEXT_INPUT) {
    if (fields[focus].value.size() +
            std::char_traits<char>::length(event.text.text) <=
        255)
      fields[focus].value += event.text.text;
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_BACKSPACE) {
      eraseLastCharacter(fields[focus].value);
      return true;
    }
    if (event.key.key == SDLK_TAB || event.key.key == SDLK_DOWN) {
      focus = (focus + 1) % fields.size();
      return true;
    }
    if (event.key.key == SDLK_UP) {
      focus = (focus + fields.size() - 1) % fields.size();
      return true;
    }
  }
  if (UI::clicked(event, {250, 565, 210, 48})) {
    for (auto &field : fields)
      field.value = field.defaultValue;
    return true;
  }
  if (UI::clicked(event, {630, 565, 190, 48}) ||
      (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RETURN &&
       !event.key.repeat)) {
    std::vector<std::string> values;
    for (const auto &field : fields)
      values.push_back(field.value);
    submit(values);
    return true;
  }
  return false;
}
void TextInputForm::update() {
  if (active && !SDL_TextInputActive(&engine.GetWindow()))
    SDL_StartTextInput(&engine.GetWindow());
}
void TextInputForm::render(RendererRef renderer, float) {
  if (!active)
    return;
  auto &ui = engine.GetUI();
  ui.backdrop(renderer);
  ui.panel(renderer, {225, 145, 830, 490});
  ui.text(renderer, title, 250, 170, 780, {255, 215, 100, 255});
  for (size_t i = 0; i < fields.size(); ++i) {
    const float y = 230 + 70.0f * i;
    ui.text(renderer, fields[i].label, 250, y + 8, 245);
    ui.button(renderer, {510, y, 500, 48},
              fields[i].value + (i == focus ? "|" : ""), i == focus);
  }
  ui.text(renderer, error, 250, 520, 780, {255, 120, 120, 255});
  ui.button(renderer, {250, 565, 210, 48}, "Default");
  ui.button(renderer, {630, 565, 190, 48}, "OK [Enter]");
  ui.button(renderer, {850, 565, 180, 48}, "Cancel");
}
} // namespace Kotonoha
