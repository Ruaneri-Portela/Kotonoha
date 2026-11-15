#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

void TextInputDialog::submitText(const std::string &text) {
  if (text.size() > 255)
    throw std::invalid_argument("Text input limit is 255 bytes");
  input = text;
  SDL_Event confirm{};
  confirm.type = SDL_EVENT_KEY_DOWN;
  confirm.key.key = SDLK_RETURN;
  onEvent(confirm);
}

TextInputDialog::TextInputDialog(
    Engine &engineRef, std::string prompt,
    std::function<void(const std::string &)> confirmCb,
    std::function<void()> cancelCb, const std::string &initialText)
    : Scene(engineRef) {
  configure(prompt, std::move(confirmCb), std::move(cancelCb), initialText);
}

void TextInputDialog::configure(
    const std::string &prompt,
    std::function<void(const std::string &)> confirmCb,
    std::function<void()> cancelCb, const std::string &initialText) {
  touchActivation.reset();
  promptText = prompt;
  onConfirm = std::move(confirmCb);
  onCancel = std::move(cancelCb);
  input = initialText;
  editing = false;
}

bool TextInputDialog::onEvent(EventRef rawEvent) {
  const auto event = MapControllerEvent(rawEvent);
  if (!touchActivation.accept(event))
    return true;
  if (event.type == SDL_EVENT_TEXT_INPUT) {
    if (input.size() + std::char_traits<char>::length(event.text.text) <= 255)
      input += event.text.text;
    else
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Text input limit is 255 bytes");
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_BACKSPACE) {
    eraseLastCharacter(input);
    return true;
  }
  const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
  if ((key && event.key.key == SDLK_RETURN) ||
      UI::clicked(event, {400, 402, 200, 48})) {
    // Stop this editor before calling user code: the callback can destroy this
    // dialog, reconfigure it, or start text input for a different modal.
    const auto callback = onConfirm;
    const auto value = input;
    if (!SDL_StopTextInput(&engine.GetWindow()))
      throw std::runtime_error(SDL_GetError());
    editing = false;
    if (callback)
      callback(value);
    return true;
  } else if (BackAction(engine, event) ||
             UI::clicked(event, {620, 402, 240, 48})) {
    const auto callback = onCancel;
    if (!SDL_StopTextInput(&engine.GetWindow()))
      throw std::runtime_error(SDL_GetError());
    editing = false;
    if (callback)
      callback();
    return true;
  }
  const auto &scenes = engine.GetRuntime().GetActiveScenes();
  if (std::find(scenes.begin(), scenes.end(), this) == scenes.end()) {
    if (!SDL_StopTextInput(&engine.GetWindow()))
      throw std::runtime_error(SDL_GetError());
    editing = false;
  }
  return false;
}

void TextInputDialog::update() {
  if (!editing || !SDL_TextInputActive(&engine.GetWindow())) {
    if (!SDL_StartTextInput(&engine.GetWindow()))
      throw std::runtime_error(SDL_GetError());
    editing = true;
  }
  float left = 0, top = 0, right = 0, bottom = 0;
  if (!SDL_RenderCoordinatesToWindow(&engine.GetRenderer(), 400, 330, &left,
                                     &top) ||
      !SDL_RenderCoordinatesToWindow(&engine.GetRenderer(), 880, 378, &right,
                                     &bottom))
    throw std::runtime_error(SDL_GetError());
  const SDL_Rect area{static_cast<int>(std::floor(left)),
                      static_cast<int>(std::floor(top)),
                      static_cast<int>(std::ceil(right - left)),
                      static_cast<int>(std::ceil(bottom - top))};
  if (!SDL_SetTextInputArea(&engine.GetWindow(), &area, 0))
    throw std::runtime_error(SDL_GetError());
}

void TextInputDialog::render(RendererRef renderer, float) {
  auto &ui = engine.GetUI();
  ui.backdrop(renderer);
  ui.panel(renderer, {380, 240, 520, 230});
  ui.text(renderer, promptText, 400, 260, 480, {255, 215, 100, 255});
  ui.panel(renderer, {400, 330, 480, 48}, {45, 50, 70, 255});
  SDL_Rect clip{410, 335, 460, 38};
  SDL_SetRenderClipRect(&renderer, &clip);
  ui.text(renderer, input + "|", 410, 338, 460);
  SDL_SetRenderClipRect(&renderer, nullptr);
  ui.button(renderer, {400, 402, 200, 48}, "OK [Enter]");
  ui.button(renderer, {620, 402, 240, 48}, "Cancel [ESC]");
}

TextInputDialog &
GetTextInputDialogRuntime(Engine &engineRef, const std::string &prompt,
                          std::function<void(const std::string &)> confirmCb,
                          std::function<void()> cancelCb,
                          const std::string &initialText) {
  auto &dialog = engineRef.sharedScene<TextInputDialog>("", nullptr, nullptr);
  if (!prompt.empty() || confirmCb || cancelCb)
    dialog.configure(prompt, std::move(confirmCb), std::move(cancelCb),
                     initialText);
  return dialog;
}

} // namespace Kotonoha
