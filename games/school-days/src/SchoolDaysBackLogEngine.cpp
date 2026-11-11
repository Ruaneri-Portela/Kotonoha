#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogEngine.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>
#include <SchoolDays/SchoolDaysAssets.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <stdexcept>
#include <string>

namespace Kotonoha {

namespace {

SchoolDaysBackLogModel::Variant BackLogVariant() {
  return SchoolDaysBackLogModel::Variant::WideFull;
}

} // namespace

SchoolDaysBackLogEngine::SchoolDaysBackLogEngine(
    Engine &engine, Kotonoha::Providers::SchoolDaysAssets &assets)
    : VNBacklog(engine), engine_(engine), assets_(assets) {}

SchoolDaysBackLogEngine::~SchoolDaysBackLogEngine() { close(); }

void SchoolDaysBackLogEngine::open() {
  close();
  std::string failure;
  TTF_Font *font = nullptr;
  try {
    font = TTF_OpenFont(
        FileResolver::font(engine_.GetSettings().fontPath).string().c_str(),
        SchoolDaysSubtitleTypography::PointSize);
  } catch (const std::exception &error) {
    SDL_LogWarn(LogUI, "School Days BackLog font unavailable: %s",
                error.what());
  }
  if (font &&
      backlogModel_.Open(assets_.root().string(), backlogStore_,
                         SchoolDaysBackLogModel::Orientation::Horizon,
                         BackLogVariant(), &failure) &&
      backlogPresenter_.Open(&engine_.GetRenderer(), assets_.root().string(),
                             backlogModel_.CurrentOrientation(), true, font,
                             nullptr, &failure)) {
    isOpen_ = true;
    VNBacklog::open();
    return;
  }
  if (!failure.empty())
    SDL_LogWarn(Kotonoha::LogUI, "School Days BackLog: %s", failure.c_str());
  backlogModel_.Close();
  backlogPresenter_.Close();
  if (font)
    TTF_CloseFont(font);
  // Fall back to the engine backlog when the retail skin is unavailable.
  VNBacklog::open();
  isOpen_ = true;
}

void SchoolDaysBackLogEngine::close() {
  if (!isOpen_)
    return;
  backlogModel_.Close();
  backlogPresenter_.Close();
  VNBacklog::close();
  isOpen_ = false;
}

void SchoolDaysBackLogEngine::addEntry(const std::string &speaker,
                                       const std::string &text) {
  // Replica na loja nativa para preservar ScriptChange/TruncateRaw.
  backlogStore_.AppendLogMessage(static_cast<int>(backlogStore_.Size()),
                                 Utf8ToUtf16(speaker), Utf8ToUtf16(text));
  // Replica no backlog do motor (scroll, toques, renderização base).
  VNBacklog::addEntry(speaker, text);
}

void SchoolDaysBackLogEngine::clear() {
  backlogStore_.ResetTransient();
  VNBacklog::clear();
}

bool SchoolDaysBackLogEngine::onEvent(EventRef event) {
  if (!isOpen_)
    return false;
  const auto mapped = MapControllerEvent(event, false, true);
  if (!touchActivation.accept(mapped))
    return true;
  // Fecha com Back/B ou área de fechar.
  if (BackAction(engine_, mapped) ||
      (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
       mapped.key.key == SDLK_B)) {
    close();
    return true;
  }
  // Delegação ao modelo nativo quando a skin retail está aberta.
  if (backlogModel_.IsOpen()) {
    if (mapped.type == SDL_EVENT_MOUSE_MOTION ||
        mapped.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      int x = -1, y = -1;
      const auto &view = engine_.GetRuntime().viewport();
      const auto frame = UI::designFrame(view);
      SDL_Event logical = mapped;
      if (!SDL_ConvertEventToRenderCoordinates(&engine_.GetRenderer(),
                                               &logical))
        throw std::runtime_error(SDL_GetError());
      const SDL_Event framed = UI::toFrame(logical, frame);
      x = int(framed.motion.x);
      y = int(framed.motion.y);
      if (mapped.type == SDL_EVENT_MOUSE_MOTION) {
        backlogModel_.SetSelectionFromPointer(x, y);
      } else {
        SchoolDaysBackLogModel::Action action;
        if (backlogModel_.ActivatePointer(x, y, &action) &&
            action.kind == SchoolDaysBackLogModel::ActionKind::Close) {
          close();
          return true;
        }
      }
    }
    return true;
  }
  return VNBacklog::onEvent(mapped);
}

void SchoolDaysBackLogEngine::update() {
  if (!isOpen_)
    return;
  VNBacklog::update();
}

void SchoolDaysBackLogEngine::render(RendererRef renderer, float scale) {
  if (!isOpen_)
    return;
  if (backlogModel_.IsOpen()) {
    std::string failure;
    if (!backlogPresenter_.Render(&renderer, backlogModel_, &failure)) {
      backlogModel_.Close();
      backlogPresenter_.Close();
      if (!failure.empty())
        SDL_LogWarn(Kotonoha::LogUI, "School Days BackLog render failed: %s",
                    failure.c_str());
      return;
    }
    return;
  }
  VNBacklog::render(renderer, scale);
}

} // namespace Kotonoha
