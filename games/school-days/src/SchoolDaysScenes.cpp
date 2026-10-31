#include "SchoolDaysScenes.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace SchoolDays {
namespace {
struct Control {
  SDL_FRect hit, destination, source;
};

// WideFull hit masks and MenuBar_Chip atlas coordinates from the existing port.
constexpr std::array<Control, 15> controls{{
    {{10,5,169,30}, {9.6f,4.8f,169.6f,30.4f}, {1,67,106,19}},
    {{12,41,98,44}, {12.8f,41.6f,97.6f,43.2f}, {1,1,61,27}},
    {{127,41,67,44}, {128,41.6f,65.6f,43.2f}, {63,1,41,27}},
    {{199,41,65,44}, {198.4f,41.6f,65.6f,43.2f}, {105,1,41,27}},
    {{281,41,66,44}, {281.6f,41.6f,65.6f,43.2f}, {147,1,41,27}},
    {{366,41,66,44}, {366.4f,41.6f,65.6f,43.2f}, {189,1,41,27}},
    {{436,41,66,44}, {436.8f,41.6f,65.6f,43.2f}, {231,1,41,27}},
    {{505,41,66,44}, {505.6f,41.6f,65.6f,43.2f}, {273,1,41,27}},
    {{576,41,65,44}, {576,41.6f,65.6f,43.2f}, {314,1,41,27}},
    {{646,41,66,44}, {646.4f,41.6f,65.6f,43.2f}, {356,1,41,27}},
    {{760,41,97,44}, {760,41.6f,97.6f,43.2f}, {1,29,61,27}},
    {{862,41,98,44}, {862.4f,41.6f,97.6f,43.2f}, {63,29,61,27}},
    {{965,41,97,44}, {964.8f,41.6f,97.6f,43.2f}, {125,29,61,27}},
    {{1067,41,98,44}, {1067.2f,41.6f,97.6f,43.2f}, {187,29,61,27}},
    {{1170,41,97,44}, {1169.6f,41.6f,97.6f,43.2f}, {249,29,61,27}},
}};

bool enabled(int control, bool handoff) {
  if (control < 1 || (control >= 5 && control <= 9))
    return false;
  return !handoff || (control >= 10 && control <= 13);
}
bool inside(SDL_FPoint point, SDL_FRect area) {
  return SDL_PointInRectFloat(&point, &area);
}
} // namespace

LogoSplash::LogoSplash(Kotonoha::Engine &engine, Assets &source)
    : Scene(engine), assets(source) {
  for (const char *stem : {"System/SYSTEM/LOGO", "System/System/Logo"})
    try {
      logo = assets.resolve(stem, true);
      break;
    } catch (const std::exception &) {
    }
  if (logo.empty())
    SDL_LogWarn(Kotonoha::LogUI, "School Days logo System/SYSTEM/LOGO.png is unavailable");
  restart();
}

void LogoSplash::restart() {
  done = logo.empty();
  shown = false;
  started = SDL_GetTicksNS();
}

double LogoSplash::elapsed() const {
  return double(SDL_GetTicksNS() - started) / SDL_NS_PER_SECOND;
}

bool LogoSplash::handleEvent(Kotonoha::EventRef event) {
  if (done) return false;
  const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.which != SDL_TOUCH_MOUSEID;
  const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                   (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE ||
                    event.key.key == SDLK_ESCAPE);
  if (click || key || event.type == SDL_EVENT_FINGER_DOWN) done = true;
  return true;
}

void LogoSplash::update() {
  // The timeout also covers a logo that never decodes.
  if (!done && elapsed() >= displaySeconds + (shown ? 0.0 : fadeSeconds)) done = true;
}

void LogoSplash::render(Kotonoha::RendererRef renderer, float) {
  if (done) return;
  const SDL_FRect area{0, 0, 1280, 720};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(&renderer, &area);
  SDL_Texture *texture = nullptr;
  try { texture = &assets.texture(renderer, logo); }
  catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days logo: %s", error.what());
    done = true;
    return;
  }
  if (!shown) {
    shown = true;
    started = SDL_GetTicksNS();
  }
  float width = 0, height = 0;
  SDL_GetTextureSize(texture, &width, &height);
  if (width <= 0 || height <= 0) return;
  const float fit = std::min(area.w / width, area.h / height);
  const SDL_FRect target{(area.w - width * fit) / 2, (area.h - height * fit) / 2,
                         width * fit, height * fit};
  const double time = elapsed();
  const double alpha = std::clamp(std::min(time, displaySeconds - time) / fadeSeconds, 0.0, 1.0);
  SDL_SetTextureAlphaModFloat(texture, float(alpha));
  SDL_RenderTexture(&renderer, texture, nullptr, &target);
  SDL_SetTextureAlphaModFloat(texture, 1.0f);
}

void Subtitles::bind(const Kotonoha_orsData &script) {
  std::vector<Kotonoha::SubtitleCue> cues;
  for (auto *event = script.data; event; event = event->next)
    if (event->command == PRINT_TEXT && event->data.print_text &&
        event->data.print_text->text && event->endTick > event->startTick) {
      const auto &text = *event->data.print_text;
      cues.push_back({event->startTick / 24.0, event->endTick / 24.0,
          text.text, text.character ? text.character : ""});
    }
  setCues(cues, 46);
  setPosition(0);
}

GameScene::GameScene(Kotonoha::Engine &engine, Runtime &runtime)
    : Scene(engine), runtime(runtime) {
  try {
    bar = runtime.assets->resolve("System/MenuBar/MenuBar", true);
    chips = runtime.assets->resolve("System/MenuBar/MenuBar_Chip", true);
  } catch (const std::exception &error) {
    bar.clear();
    chips.clear();
    SDL_LogWarn(Kotonoha::LogUI, "School Days menu artwork unavailable: %s", error.what());
  }
}

void GameScene::syncChoice() {
  if (runtime.choice != lastChoice) {
    lastChoice = runtime.choice;
    selectedChoice = -1;
  }
}
std::vector<int> GameScene::choices() const {
  std::vector<int> result;
  if (runtime.choice && !runtime.committed && runtime.tick < runtime.choice->endTick)
    for (Uint64 i = 0; i < runtime.choice->data.set_select->size; ++i) {
      const char *text = runtime.choice->data.set_select->options[i];
      if (text && *text)
        result.push_back(static_cast<int>(i));
    }
  return result;
}
SDL_FRect GameScene::choiceArea(int index, int count) const {
  // The retail English selector uses top/bottom choices, not a dialog list.
  const float center = count == 1 ? 360 : 720.0f * (index + 0.5f) / count;
  return {100, center - 55, 1080, 110};
}

void GameScene::update() {
  const Uint64 now = SDL_GetTicks();
  const float delta = lastUpdate ? (now - lastUpdate) / 200.0f : 0;
  lastUpdate = now;
  barAlpha = std::clamp(barAlpha + (pointerInBar ? delta : -delta), 0.0f, 1.0f);
  syncChoice();
}

bool GameScene::handleEvent(Kotonoha::EventRef event) {
  syncChoice();
  if (runtime.modal())
    return false;
  const auto visible = choices();
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !visible.empty()) {
    if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
      const auto index = static_cast<std::size_t>(event.key.key - SDLK_1);
      if (index < visible.size()) runtime.choose(visible[index]);
      return true;
    }
    if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
      if (selectedChoice < 0)
        selectedChoice = event.key.key == SDLK_UP ? int(visible.size()) - 1 : 0;
      else
        selectedChoice = (selectedChoice + int(visible.size()) +
            (event.key.key == SDLK_UP ? -1 : 1)) % int(visible.size());
      return true;
    }
    if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) && selectedChoice >= 0) {
      runtime.choose(visible[selectedChoice]);
      return true;
    }
  }
  if (event.type != SDL_EVENT_MOUSE_MOTION && event.type != SDL_EVENT_MOUSE_BUTTON_UP)
    return false;
  const SDL_FPoint point = event.type == SDL_EVENT_MOUSE_MOTION
      ? SDL_FPoint{event.motion.x, event.motion.y}
      : SDL_FPoint{event.button.x, event.button.y};
  const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                     event.button.button == SDL_BUTTON_LEFT;
  pointerInBar = point.y >= 0 && point.y < 120 && point.x >= 0 && point.x < 1280;
  hovered = -1;
  if (pointerInBar) {
    if (bar.empty()) {
      constexpr int indices[]{1, 2, 3, 10, 11, 12, 13, 14};
      for (int i = 0; i < 8; ++i)
        if (enabled(indices[i], runtime.bridge.HasPendingHandoff()) &&
            inside(point, {float(i * 160), 41, 154, 44}))
          hovered = indices[i];
    } else {
      for (int i = 0; i < int(controls.size()); ++i)
        if (enabled(i, runtime.bridge.HasPendingHandoff()) && inside(point, controls[i].hit))
          hovered = i;
    }
    if (click && hovered >= 0)
      runtime.activateControl(hovered);
    return true;
  }
  for (int i = 0; i < int(visible.size()); ++i)
    if (inside(point, choiceArea(i, int(visible.size())))) {
      selectedChoice = i;
      if (click)
        runtime.choose(visible[i]);
      return true;
    }
  if (click && runtime.bridge.HasPendingHandoff()) {
    runtime.continueHandoff();
    return true;
  }
  return false;
}

void GameScene::renderBar(Kotonoha::RendererRef renderer) {
  if (barAlpha <= 0)
    return;
  auto &ui = engine.GetUI();
  if (bar.empty()) {
    constexpr const char *labels[]{"Pause", "Back", "Skip", "Save", "Load", "Backlog", "Config", "Title"};
    constexpr int indices[]{1, 2, 3, 10, 11, 12, 13, 14};
    for (int i = 0; i < 8; ++i)
      ui.button(renderer, {float(i * 160), 41, 154, 44}, labels[i], hovered == indices[i]);
    return;
  }
  auto &base = runtime.assets->texture(renderer, bar);
  auto &atlas = runtime.assets->texture(renderer, chips);
  const auto alpha = static_cast<Uint8>(barAlpha * 255);
  if (!SDL_SetTextureAlphaMod(&base, alpha) || !SDL_SetTextureAlphaMod(&atlas, alpha))
    throw std::runtime_error(SDL_GetError());
  const auto draw = [&](SDL_FRect source, SDL_FRect destination) {
    if (!SDL_RenderTexture(&renderer, &atlas, &source, &destination))
      throw std::runtime_error(SDL_GetError());
  };
  const SDL_FRect source{0, 0, 800, 75}, destination{0, 0, 1280, 120};
  if (!SDL_RenderTexture(&renderer, &base, &source, &destination))
    throw std::runtime_error(SDL_GetError());
  draw({1,346,538,22}, {185.6f,4.8f,860.8f,35.2f});
  draw({1,183,106,19}, controls[0].destination);
  draw({runtime.paused ? 1.0f : 311.0f, runtime.paused ? 127.0f : 155.0f,61,27},
       controls[1].destination);
  draw({43,290,216,27}, {366.4f,41.6f,345.6f,43.2f});
  for (int i = 10; i <= 14; ++i)
    draw({float(1 + (i - 10) * 62),155,61,27}, controls[i].destination);
  if (hovered >= 0)
    draw(controls[hovered].source, controls[hovered].destination);
  if (!SDL_SetTextureAlphaMod(&base, 255) || !SDL_SetTextureAlphaMod(&atlas, 255))
    throw std::runtime_error(SDL_GetError());
}

void GameScene::render(Kotonoha::RendererRef renderer, float) {
  auto &ui = engine.GetUI();
  const auto visible = choices();
  for (int i = 0; i < int(visible.size()); ++i) {
    const auto area = choiceArea(i, int(visible.size()));
    ui.panel(renderer, area, selectedChoice == i ? SDL_Color{56, 70, 105, 175}
                                                : SDL_Color{10, 16, 28, 110});
    const auto &text = runtime.choice->data.set_select->options[visible[i]];
    const std::string label = std::to_string(i + 1) + ". " + text;
    const float height = ui.textHeight(renderer, label, area.w - 48);
    ui.text(renderer, label, area.x + 24, area.y + (area.h - height) / 2, area.w - 48);
  }
  if (!visible.empty()) {
    const float duration = float(runtime.choice->endTick - runtime.choice->startTick);
    const float remaining = std::clamp(float(runtime.choice->endTick - runtime.tick) / duration, 0.0f, 1.0f);
    ui.panel(renderer, {100, 680, 1080, 6}, {15, 20, 35, 180});
    ui.panel(renderer, {100, 680, 1080 * remaining, 6}, {200, 220, 255, 220});
  }
  if (runtime.bridge.HasPendingHandoff()) {
    ui.panel(renderer, {350, 290, 580, 120});
    ui.text(renderer, "Chapter complete. Click to continue.", 380, 330, 520);
  } else if (runtime.paused && visible.empty())
    ui.text(renderer, "Paused - Space to resume", 460, 340, 420);
  renderBar(renderer);
}
} // namespace SchoolDays
