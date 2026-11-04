#include "AmagamiRuntime.hpp"
#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace Amagami {
void AmagamiTextBox::setOriginalKey(const std::string &key) {
  originalKey_ = key;
  waits_.clear();
  nextWait_ = 0;
  pauseSeconds_ = 0;
  for (size_t i = 0; i + 1 < key.size(); ++i) {
    if (key[i] != 'W' || key[i + 1] < '0' || key[i + 1] > '9')
      continue;
    size_t at = i + 1;
    int frames = 0;
    while (at < key.size() && key[at] >= '0' && key[at] <= '9')
      frames = std::min(36000, frames * 10 + key[at++] - '0');
    waits_.emplace_back(Kotonoha::VNTextBoxUtility::CountUtf8Chars(
                            DisplayText(key.substr(0, i))),
                        frames / 60.0);
    i = at - 1;
  }
}
void AmagamiTextBox::completeDialogue() {
  nextWait_ = waits_.size();
  pauseSeconds_ = 0;
  VNTextBox::completeDialogue();
}
void AmagamiTextBox::update() {
  const double delta = clock.seconds();
  if (!visible || engine.isStoryPaused()) {
    VNTextBox::update();
    return;
  }
  if (pauseSeconds_ > 0) {
    clock.reset();
    pauseSeconds_ = std::max(0.0, pauseSeconds_ - delta);
    return;
  }
  VNTextBox::update();
  if (nextWait_ < waits_.size() && visibleChars >= waits_[nextWait_].first) {
    const auto [at, seconds] = waits_[nextWait_++];
    pauseSeconds_ =
        std::max(0.0, seconds - (visibleChars - at) / charactersPerSecond());
    visibleChars = double(at);
    visibleDialogue =
        Kotonoha::VNTextBoxUtility::GetSubStringUtf8(fullDialogue, at);
    isComplete = false;
  }
}
void AmagamiTextBox::translationChanged(const std::string &original,
                                        const std::string &previous,
                                        const std::string &translated) {
  if (original == originalKey_) {
    const auto replacement =
        translated == originalKey_ ? DisplayText(originalKey_) : translated;
    replaceTranslation(fullDialogue, fullDialogue, replacement);
  } else
    VNTextBox::translationChanged(original, previous, translated);
}
void AmagamiTextBox::renderSpeaker(Kotonoha::RendererRef r) {
  auto name = speakerName;
  auto n = Kotonoha::VNTextBoxUtility::CountUtf8Chars(name);
  if (n == 1)
    name = "　" + name + "　";
  else if (n == 2)
    name.insert(Kotonoha::VNTextBoxUtility::NextUtf8Char(name, 0), "　");
  float nameSize = 24;
  while (nameSize > 14 &&
         engine.GetVNUI().measureSized(name, 2000, nameSize).x > 90)
    nameSize -= 1;
  engine.GetVNUI().textSized(r, name, 44, 288, 90, nameSize,
                             engine.GetSettings().vnTextColorOverride
                                 ? engine.GetSettings().vnSpeakerColor
                                 : SDL_Color{240, 240, 245, 255});
}
void AmagamiTextBox::renderDialogue(Kotonoha::RendererRef r) {
  const float x = speakerName.empty() ? 44 : 136;
  const auto key = speakerName + '\0' + fullDialogue + '\0' +
                   engine.GetSettings().vnFontPath + '\0' +
                   engine.GetSettings().fontPath;
  if (key != layoutKey_) {
    layoutKey_ = key;
    for (layoutFont_ = 24; layoutFont_ >= 14; layoutFont_ -= 2) {
      auto width = [&](const std::string &text) {
        return engine.GetVNUI().measureSized(text, 2000, layoutFont_).x;
      };
      const auto count = Kotonoha::VNTextBoxUtility::RevealLines(
                             fullDialogue, fullDialogue.size(), width, 596 - x)
                             .size();
      layoutStep_ = count <= 3 ? 44 : 128.0f / float(count);
      if (layoutStep_ >= layoutFont_ * 1.1f || layoutFont_ == 14)
        break;
    }
  }
  auto measure = [&](const std::string &text) {
    return engine.GetVNUI().measureSized(text, 2000, layoutFont_).x;
  };
  const auto lines = Kotonoha::VNTextBoxUtility::RevealLines(
      fullDialogue, visibleDialogue.size(), measure, 596 - x);
  SDL_Rect clip{40, 284, 560, 132};
  SDL_SetRenderClipRect(&r, &clip);
  for (size_t i = 0; i < lines.size(); ++i)
    engine.GetVNUI().textSized(r, lines[i], x, 288 + float(i) * layoutStep_,
                               596 - x, layoutFont_,
                               engine.GetSettings().vnTextColorOverride
                                   ? engine.GetSettings().vnTextColor
                                   : SDL_Color{240, 240, 245, 255});
  SDL_SetRenderClipRect(&r, nullptr);
}
void AmagamiTextBox::renderIndicator(Kotonoha::RendererRef) {}
void AmagamiChoices::layout() {
  for (size_t i = 0; i < options.size(); ++i)
    options[i].hitBox = {40, 284 + float(i) * 44, 560, 36};
}
void AmagamiChoices::renderPanel(Kotonoha::RendererRef r) {
  auto draw = [&](const std::string &id, SDL_FRect rect) {
    auto &tex = assets_.texture(r, id);
    Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &rect);
  };
  draw("sysgraph/win1.tm2", {24, 266, 592, 160});
  draw("sysgraph/win1_base.tm2", {40, 243, 144, 24});
  const auto label = engine.GetTranslation().text(-1, "選択肢");
  if (label == "選択肢")
    draw("sysgraph/win1_text.tm2", {40, 243, 144, 24});
  else
    engine.GetVNUI().textSized(r, label, 48, 245, 128, 18);
}
void AmagamiChoices::renderOption(Kotonoha::RendererRef r, size_t i,
                                  bool focused) {
  auto b = options[i].hitBox;
  if (focused) {
    auto &tex = assets_.texture(r, "sysgraph/cursor_base.tm2");
    SDL_SetTextureColorMod(&tex, 255, 145, 165);
    SDL_SetTextureAlphaMod(&tex, 140);
    Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &b);
    SDL_SetTextureColorMod(&tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(&tex, 255);
  }
  engine.GetVNUI().textSized(r, options[i].label, b.x + 4, b.y + 3, b.w - 8,
                             24);
}
void AmagamiBacklog::setHistory(const std::vector<Dialogue> &history,
                                int route) {
  clear();
  originals_.clear();
  originals_.reserve(history.size());
  route_ = route;
  for (const auto &line : history) {
    VNBacklog::addEntry(line.speaker, line.text);
    if (line.text.empty())
      continue;
    originals_.push_back(line.original);
    if (line.voice >= 0 && !line.voiceCode.empty())
      setLastVoice("voice/" + line.voiceCode + "/" +
                   std::to_string(line.voice));
  }
  layoutDirty_ = true;
  focused_ = entries.empty() ? 0 : entries.size() - 1;
}
void AmagamiBacklog::open() {
  VNBacklog::open();
  focused_ = entries.empty() ? 0 : entries.size() - 1;
}
void AmagamiBacklog::close() {
  VNBacklog::close();
  if (onClose)
    onClose();
}
void AmagamiBacklog::ensureLayout() {
  const auto fontKey =
      engine.GetSettings().vnFontPath + '\0' + engine.GetSettings().fontPath;
  if (!layoutDirty_ && fontKey_ == fontKey)
    return;
  fontKey_ = fontKey;
  layoutDirty_ = false;
  rows_.clear();
  rows_.reserve(entries.size());
  float top = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    const auto speaker = engine.GetTranslation().text(route_, entry.speaker);
    auto text = engine.GetTranslation().text(route_, originals_[i]);
    if (text.empty() || text == originals_[i])
      text = entry.text;
    Row row;
    row.text = speaker.empty() ? text : speaker + "　" + text;
    auto measure = [&](const std::string &value) {
      return engine.GetVNUI().measureSized(value, 2000, 24).x;
    };
    row.lines = Kotonoha::VNTextBoxUtility::RevealLines(
        row.text, row.text.size(), measure, 552);
    row.height = std::max(132.0f, float(row.lines.size()) * 44);
    row.top = top;
    top += row.height;
    rows_.push_back(std::move(row));
  }
  maxScroll = std::max(0.0f, top - 416);
  targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
  scroll = std::clamp(scroll, 0.0f, maxScroll);
}
void AmagamiBacklog::focus(size_t index) {
  if (entries.empty())
    return;
  ensureLayout();
  const auto next = std::min(index, entries.size() - 1);
  if (next != focused_ && soundEffect)
    soundEffect(31); // LogList.moveCursorU/D
  focused_ = next;
  const auto &row = rows_[focused_];
  if (row.top < targetScroll)
    targetScroll = row.top;
  else if (row.top + row.height > targetScroll + 416)
    targetScroll = row.top + row.height - 416;
  targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
  scrollToBottom = false;
}
void AmagamiBacklog::stepFocus(size_t index) {
  if (entries.empty())
    return;
  const size_t previous = focused_;
  focus(index);
  if (focused_ == previous)
    return;
  // Keep navigation visible on each step by following the selected entry.
  // Original LogPage grouping and its six-frame transition remain separate
  // work.
  const auto &row = rows_[focused_];
  if (focused_ < previous)
    targetScroll = row.top + row.height - 416;
  else
    targetScroll = row.top;
  targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
  scrollToBottom = false;
}
bool AmagamiBacklog::onEvent(Kotonoha::EventRef input) {
  if (!isVisible())
    return false;
  if (!touchActivation.accept(input))
    return true;
  const auto event = Kotonoha::MapControllerEvent(input, false, true);
  ensureLayout();
  if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
       event.button.button == SDL_BUTTON_RIGHT) ||
      (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_B))) {
    close();
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL && !entries.empty()) {
    const float dy =
        event.wheel.y *
        (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
    if (dy > 0 && focused_ > 0)
      stepFocus(focused_ - 1);
    else if (dy < 0 && focused_ + 1 < entries.size())
      stepFocus(focused_ + 1);
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.x >= 44 &&
      event.motion.x < 596 && event.motion.y >= 16 && event.motion.y < 432 &&
      !rows_.empty()) {
    const float y = event.motion.y - 16 + scroll;
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (y >= rows_[i].top && y < rows_[i].top + rows_[i].height) {
        if (focused_ != i)
          focus(i);
        break;
      }
    }
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !entries.empty()) {
    switch (event.key.key) {
    case SDLK_UP:
      stepFocus(focused_ ? focused_ - 1 : 0);
      return true;
    case SDLK_DOWN:
      stepFocus(std::min(focused_ + 1, entries.size() - 1));
      return true;
    case SDLK_HOME:
      stepFocus(0);
      return true;
    case SDLK_END:
      stepFocus(entries.size() - 1);
      return true;
    case SDLK_PAGEUP:
      stepFocus(focused_ > 2 ? focused_ - 3 : 0);
      return true;
    case SDLK_PAGEDOWN:
      stepFocus(std::min(focused_ + 3, entries.size() - 1));
      return true;
    case SDLK_RETURN:
    case SDLK_SPACE:
      if (!event.key.repeat)
        replayEntry(focused_);
      return true;
    default:
      break;
    }
  }
  const bool isLeftClick = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                            event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                           event.button.button == SDL_BUTTON_LEFT;
  const bool isTouchAct = event.button.which == SDL_TOUCH_MOUSEID
                              ? event.type == SDL_EVENT_MOUSE_BUTTON_UP
                              : event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  if (isLeftClick && isTouchAct && event.button.x >= 44 &&
      event.button.x < 596 && event.button.y >= 16 && event.button.y < 432) {
    const float y = event.button.y - 16 + scroll;
    for (size_t i = 0; i < rows_.size(); ++i)
      if (y >= rows_[i].top && y < rows_[i].top + rows_[i].height) {
        focus(i);
        replayEntry(i);
        return true;
      }
  }
  return VNBacklog::onEvent(event);
}
void AmagamiBacklog::renderFrame(Kotonoha::RendererRef r) {
  // BackLog.scriptMain: fullscreen black Sprite, opacity .5; LogList uses
  // 416 visible pixels. LogLine uses 552-wide lines, 44 pitch, 132/page.
  engine.GetUI().panel(r, {0, 0, 640, 448}, {0, 0, 0, 128});
}
void AmagamiBacklog::renderEntries(Kotonoha::RendererRef r) {
  ensureLayout();
  if (scrollToBottom) {
    scroll = targetScroll = maxScroll;
    scrollToBottom = false;
  }
  SDL_Rect previous{};
  const bool clipped = SDL_RenderClipEnabled(&r);
  SDL_GetRenderClipRect(&r, &previous);
  SDL_Rect clip{44, 16, 552, 416};
  SDL_SetRenderClipRect(&r, &clip);
  auto &ui = engine.GetVNUI();
  for (size_t i = 0; i < rows_.size(); ++i) {
    const auto &row = rows_[i];
    const float y = 16 + row.top - scroll;
    if (y + row.height < 16 || y >= 432)
      continue;
    if (i == focused_)
      engine.GetUI().panel(r, {44, y, 552, row.height}, {204, 153, 102, 42});
    for (size_t line = 0; line < row.lines.size(); ++line)
      ui.textSized(r, row.lines[line], 44, y + float(line) * 44, 552, 24,
                   {240, 240, 245, 255});
  }
  SDL_SetRenderClipRect(&r, clipped ? &previous : nullptr);
  if (maxScroll > 0) {
    const float total = maxScroll + 416;
    const float height = std::max(12.0f, 416 * 416 / total);
    engine.GetUI().panel(r, {612, 16, 3, 416}, {255, 255, 255, 50});
    engine.GetUI().panel(
        r, {611, 16 + (416 - height) * scroll / maxScroll, 5, height},
        {240, 210, 180, 210});
  }
}
void AmagamiEventMap::setEvents(
    std::vector<Campaign::Event> available, std::vector<Campaign::Event> map,
    int day, int period,
    std::function<void(int, const std::string &)> callback) {
  events_ = std::move(available);
  map_ = map.empty() ? events_ : std::move(map);
  day_ = day;
  period_ = period;
  std::vector<Kotonoha::ChoiceOption> choices;
  for (size_t i = 0; i < events_.size(); ++i) {
    const auto &event = events_[i];
    const auto &original = event.title.empty() ? event.name : event.title;
    choices.push_back({engine.GetTranslation().text(-1, original), int(i)});
  }
  choices.push_back({engine.GetTranslation().text(-1, "次の時間へ"), -1});
  setQuestion(engine.GetTranslation().text(-1, "行動を選択してください"),
              choices, std::move(callback));
  layout();
}
void AmagamiEventMap::layout() {
  if (selected < events_.size()) {
    originX_ = events_[selected].x - 4;
    originY_ = events_[selected].y - 2;
  }
  for (size_t i = 0; i < events_.size(); ++i) {
    const auto &event = events_[i];
    const float x = 68 + (event.x - originX_) * 42;
    const float y = 190 + (event.y - originY_) * 44 + ((event.x + 1) % 2) * 22;
    options[i].hitBox = {x - 20, y - 20, 40, 40};
  }
  options.back().hitBox = {448, 393, 176, 32};
}
void AmagamiEventMap::move(int dx, int dy) {
  if (events_.empty()) {
    selected = events_.size();
    return;
  }
  if (selected >= events_.size()) {
    selected = 0;
    return;
  }
  const auto &from = events_[selected];
  size_t best = selected;
  int distance = std::numeric_limits<int>::max();
  for (size_t i = 0; i < events_.size(); ++i) {
    const int x = events_[i].x - from.x, y = events_[i].y - from.y;
    if ((dx && x * dx <= 0) || (dy && y * dy <= 0))
      continue;
    const int candidate = x * x + y * y;
    if (candidate < distance) {
      distance = candidate;
      best = i;
    }
  }
  selected = best;
  layout();
}
bool AmagamiEventMap::onEvent(Kotonoha::EventRef input) {
  if (!active)
    return false;
  if (!touchActivation.accept(input))
    return true;
  const auto event = Kotonoha::MapControllerEvent(input, false);
  layout();
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    switch (event.key.key) {
    case SDLK_LEFT:
      move(-1, 0);
      return true;
    case SDLK_RIGHT:
      move(1, 0);
      return true;
    case SDLK_UP:
      move(0, -1);
      return true;
    case SDLK_DOWN:
      move(0, 1);
      return true;
    case SDLK_TAB:
      selected = (selected + 1) % options.size();
      layout();
      return true;
    case SDLK_END:
      selected = events_.size();
      return true;
    case SDLK_RETURN:
    case SDLK_SPACE:
      select(selected);
      return true;
    default:
      return false;
    }
  }
  const bool isLeftClick = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                            event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                           event.button.button == SDL_BUTTON_LEFT;
  const bool isTouchAct = event.button.which == SDL_TOUCH_MOUSEID
                              ? event.type == SDL_EVENT_MOUSE_BUTTON_UP
                              : event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  if (isLeftClick && isTouchAct) {
    for (size_t i = 0; i < options.size(); ++i)
      if (Kotonoha::UI::clicked(event, options[i].hitBox)) {
        if (selected == i)
          select(i);
        else {
          selected = i;
          layout();
        }
        return true;
      }
  }
  return ChoiceQuestionScene::onEvent(event);
}
void AmagamiEventMap::render(Kotonoha::RendererRef r, float) {
  if (!active)
    return;
  layout();
  auto &ui = engine.GetVNUI();
  engine.GetUI().panel(r, {0, 0, 640, 448}, {24, 25, 32, 255});
  auto picture = [&](const std::string &file, SDL_FRect rect) {
    if (!assets_.contains(file))
      return;
    auto &texture = assets_.texture(r, file);
    Kotonoha::RenderUpscaledTexture(&r, &texture, nullptr, &rect);
  };
  // NodeMapFrame.create constructs BG_Picture(200,1,0), not an invented menu.
  picture("bg/200_10.tm2", {0, 0, 640, 448});
  ui.textSized(r, questionText, 48, 18, 550, 22);
  ui.textSized(r,
               engine.GetTranslation().text(-1, "日付") + " " +
                   std::to_string(day_) + "   " +
                   engine.GetTranslation().text(-1, "時間") + " " +
                   std::to_string(period_),
               48, 51, 550, 18);
  SDL_Rect clip{40, 84, 390, 340};
  SDL_SetRenderClipRect(&r, &clip);
  // Native NodeMapView uses a staggered 42x44 grid. The category hex layer is
  // recoverable from EventNode.setIcon; complex overlay/type flags stay
  // pending.
  for (const auto &event : map_) {
    const float x = 68 + (event.x - originX_) * 42;
    const float y = 190 + (event.y - originY_) * 44 + ((event.x + 1) % 2) * 22;
    if (x < 20 || x > 450 || y < 64 || y > 444)
      continue;
    const int category = (event.icon >> 12) & 7;
    const bool executable =
        std::any_of(events_.begin(), events_.end(), [&](const auto &candidate) {
          return candidate.name == event.name;
        });
    std::string file = "idou/node_hex" + std::to_string(category) +
                       (executable || category == 0 ? "1.tm2" : "0.tm2");
    picture(file, {x - 32, y - 32, 64, 64});
  }
  for (size_t i = 0; i < events_.size(); ++i) {
    const auto &box = options[i].hitBox;
    if (selected == i)
      picture("idou/cs_op.tm2", {box.x - 12, box.y - 12, 64, 64});
  }
  SDL_SetRenderClipRect(&r, nullptr);
  if (selected < events_.size()) {
    const auto &event = events_[selected];
    ui.textSized(r, options[selected].label, 448, 288, 176, 20);
    auto hint = engine.GetTranslation().text(-1, event.message);
    if (hint == event.message)
      hint = DisplayText(event.message);
    ui.textSized(r, hint, 448, 324, 176, 16);
  }
  engine.GetUI().panel(r, options.back().hitBox,
                       selected == events_.size() ? SDL_Color{133, 58, 110, 255}
                                                  : SDL_Color{50, 40, 45, 255});
  ui.textSized(r, options.back().label, 454, 399, 164, 18);
}
std::vector<std::string> AmagamiEventMap::sourceKeys() const {
  if (selected >= events_.size())
    return {"次の時間へ"};
  return {events_[selected].title, events_[selected].message};
}
void AmagamiRuntime::syncCampaignMap() {
  if (!eventMap_ || !session_ || !session_->campaignActive() ||
      !session_->ended()) {
    if (eventMap_)
      eventMap_->hide();
    campaignMapKey_.clear();
    return;
  }
  if (!error_.empty()) {
    eventMap_->hide();
    return;
  }
  try {
    const auto key = session_->name() + ":" +
                     std::to_string(session_->campaignDate()) + ":" +
                     std::to_string(session_->campaignPeriod()) + ":" +
                     std::to_string(session_->history().size());
    if (campaignMapKey_ == key)
      return;
    campaignMapKey_ = key;
    const auto available = session_->availableEvents();
    std::vector<std::string> names;
    for (const auto &event : available)
      names.push_back(event.name);
    eventMap_->setEvents(available, session_->campaignMapEvents(),
                         session_->campaignDate(), session_->campaignPeriod(),
                         [this, names](int option, const std::string &) {
                           campaignMapKey_.clear();
                           voice_.Stop();
                           autoElapsed_ = 0;
                           session_->setSkipHeld(false);
                           try {
                             if (option < 0)
                               session_->nextCampaignPeriod();
                             else
                               session_->selectCampaignEvent(
                                   names.at(size_t(option)));
                           } catch (const std::exception &failure) {
                             error_ = failure.what();
                             throw;
                           }
                           displayed_ = 0;
                           voiced_ = 0;
                           frameClock_.reset();
                           textBox_->hide();
                           syncDialogue();
                           syncChoices();
                         });
  } catch (const std::exception &failure) {
    error_ = failure.what();
    eventMap_->hide();
  }
}

void AmagamiRuntime::syncChoices() {
  if (!choices_ || !session_)
    return;
  if (session_->choices().empty()) {
    choices_->hide();
    return;
  }
  if (choices_->isActive())
    return;
  std::vector<Kotonoha::ChoiceOption> options;
  for (size_t i = 0; i < session_->choices().size(); ++i)
    options.push_back({engine_->GetTranslation().text(
                           translationRoute(), session_->choices()[i].text),
                       int(i)});
  choices_->setQuestion("", options, [this](int i, const std::string &) {
    voice_.Stop();
    session_->choose(size_t(i));
    syncDialogue();
  });
}
void AmagamiRuntime::syncDialogue() {
  if (!session_ || !textBox_ || displayed_ == session_->history().size())
    return;
  displayed_ = session_->history().size();
  autoElapsed_ = 0;
  const auto &line = session_->dialogue();
  auto route = int(assets_->source()->scenarioIndex(session_->name()));
  const auto text = engine_->GetTranslation().text(route, line.original);
  textBox_->setOriginalKey(line.original);
  textBox_->setDialogue(engine_->GetTranslation().text(route, line.speaker),
                        text.empty() || text == line.original
                            ? DisplayText(line.original)
                            : text);
}

AmagamiRuntime::AmagamiRuntime(std::shared_ptr<Assets> assets,
                               std::string start)
    : Runtime("Amagami", "0.1.0", "me.hirameki.kotonoha.amagami"),
      assets_(std::move(assets)), start_(std::move(start)) {}
AmagamiRuntime::~AmagamiRuntime() { shutdown(); }
void AmagamiRuntime::shutdown() {
  voice_.Stop();
  music_.Stop();
  if (eventMap_)
    eventMap_->hide();
  campaignMapKey_.clear();
  backlogVoice_.Stop();
  ambience_.Stop();
  soundEffects_.clear();
  nextSoundEffect_ = 0;
  if (backlogView_)
    backlogView_->close();
  if (assets_)
    assets_->clearCache();
  session_.reset();
}
void AmagamiRuntime::initGlobal(Kotonoha::Engine &e) {
  engine_ = &e;
  if (!choices_)
    choices_ = std::make_unique<AmagamiChoices>(e, *assets_);
  if (!textBox_)
    textBox_ = std::make_unique<AmagamiTextBox>(e);
  AddScene(textBox_.get(), Kotonoha::GameState::Gameplay);
  AddScene(choices_.get(), Kotonoha::GameState::Gameplay);
  if (!backlogView_) {
    backlogView_ = std::make_unique<AmagamiBacklog>(e);
    backlogView_->setVoiceReplay([this](const std::string &id) {
      backlogVoice_.Stop();
      audio(id, "voice", false, true);
    });
    backlogView_->soundEffect = [this](int note) {
      audio("se/" + std::to_string(note), "se");
    };
    backlogView_->onClose = [this] {
      backlog_ = false;
      backlogVoice_.Stop();
      frameClock_.reset();
    };
    AddScene(backlogView_.get(), Kotonoha::GameState::Gameplay);
  }
  if (!eventMap_) {
    eventMap_ = std::make_unique<AmagamiEventMap>(e, *assets_);
    AddScene(eventMap_.get(), Kotonoha::GameState::Gameplay);
  }
  savePath_ = (Kotonoha::saveDirectory() / "quick-save.txt").string();
}
void AmagamiRuntime::initMenu(Kotonoha::Engine &) {
  if (!start_.empty()) {
    auto name = start_;
    start_.clear();
    start(name);
  }
}
void AmagamiRuntime::initGameplay(Kotonoha::Engine &) {}
void AmagamiRuntime::start(const std::string &name, bool campaign) {
  error_.clear();
  notice_.clear();
  music_.Stop();
  voice_.Stop();
  ambience_.Stop();
  soundEffects_.clear();
  nextSoundEffect_ = 0;
  session_ = std::make_unique<Session>(assets_->source());
  bindSessionAudio();
  session_->setRealtime(true);
  frameClock_.reset();
  choices_->hide();
  eventMap_->hide();
  campaignMapKey_.clear();
  if (campaign)
    session_->startCampaign();
  else
    session_->start(name);
  paused_ = false;
  backlog_ = false;
  backlogView_->close();
  focus_ = 0;
  voiced_ = 0;
  displayed_ = 0;
  presentationSeconds_ = 0;
  autoElapsed_ = speedGaugeSeconds_ = 0;
  autoIndicator_ = skipIndicator_ = 0;
  lastMessageSpeed_ = -1;
  textBox_->setDialogue("", "");
  textBox_->hide();
  syncDialogue();
  SetState(Kotonoha::GameState::Gameplay);
}
void AmagamiRuntime::audio(const std::string &id, const std::string &channel,
                           bool loop, bool historyReplay) {
  try {
    // Sound.playSEPrim's MIDI notes 12..60 map to the 49 decoded SPU waves.
    // Out-of-range notes have no voice in the original program bank.
    if (channel == "se") {
      const auto slash = id.find('/');
      const int note =
          slash == std::string::npos ? -1 : std::stoi(id.substr(slash + 1));
      if (note < 12 || note > 60)
        return;
    }
    auto clip = assets_->loadAudio(id);
    if (clip.data.empty())
      return;
    if (channel == "voice" && !historyReplay &&
        clip.spec.format == SDL_AUDIO_F32) {
      voiceEnvelope_.clear();
      const size_t chunk =
          std::max(1, clip.spec.freq / 60) * clip.spec.channels;
      for (size_t start = 0; start < clip.data.size() / sizeof(float);
           start += chunk) {
        const size_t end =
            std::min(start + chunk, clip.data.size() / sizeof(float));
        double power = 0;
        for (size_t i = start; i < end; ++i) {
          float sample;
          std::memcpy(&sample, clip.data.data() + i * sizeof(float),
                      sizeof(float));
          power += double(sample) * sample;
        }
        voiceEnvelope_.push_back(float(std::sqrt(power / (end - start))));
      }
    }
    SDL_AudioSpec dst{SDL_AUDIO_S16, clip.spec.channels, clip.spec.freq};
    Uint8 *converted = nullptr;
    int bytes = 0;
    if (!SDL_ConvertAudioSamples(&clip.spec, clip.data.data(),
                                 int(clip.data.size()), &dst, &converted,
                                 &bytes))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::PcmPlayer::Clip pcm;
    pcm.channels = dst.channels;
    pcm.rate = dst.freq;
    pcm.loop = loop;
    pcm.gain = channel == "bgm" ? 0.48f : channel == "ambient" ? 1.0f : 0.8f;
    pcm.samples.resize(bytes / 2);
    std::memcpy(pcm.samples.data(), converted, bytes);
    SDL_free(converted);
    // ADX v4 stores loop samples after its per-channel predictor histories.
    // Trim at loop end so PcmPlayer repeats only the original loop region.
    if (loop) {
      const auto bytes = assets_->read(id);
      auto be32 = [&](size_t p) -> uint32_t {
        return uint32_t(bytes[p]) << 24 | uint32_t(bytes[p + 1]) << 16 |
               uint32_t(bytes[p + 2]) << 8 | bytes[p + 3];
      };
      const size_t loops = 0x18 + std::max(8, pcm.channels * 4);
      if (bytes.size() >= loops + 24 && bytes[4] == 3 && bytes[0x12] == 4) {
        const size_t dataStart = (size_t(bytes[2]) << 8 | bytes[3]) + 4;
        if (dataStart >= loops + 30 && be32(loops + 4)) {
          const size_t begin = be32(loops + 8), end = be32(loops + 16);
          if (begin >= end || end > pcm.samples.size() / pcm.channels)
            throw std::runtime_error("Invalid ADX loop samples");
          pcm.loopStart = begin;
          pcm.samples.resize(end * pcm.channels);
        }
      }
    }
    if (channel == "se") {
      Kotonoha::PcmPlayer *player = nullptr;
      for (auto &candidate : soundEffects_)
        if (!candidate->Playing()) {
          player = candidate.get();
          break;
        }
      if (!player && soundEffects_.size() < 16) {
        soundEffects_.push_back(std::make_unique<Kotonoha::PcmPlayer>());
        player = soundEffects_.back().get();
      }
      if (!player) {
        player = soundEffects_[nextSoundEffect_].get();
        nextSoundEffect_ = (nextSoundEffect_ + 1) % soundEffects_.size();
      }
      if (!player->Play(engine_->GetAudio().channel("se"), std::move(pcm)))
        throw std::runtime_error(SDL_GetError());
      return;
    }
    auto &player = historyReplay          ? backlogVoice_
                   : channel == "bgm"     ? music_
                   : channel == "ambient" ? ambience_
                                          : voice_;
    if (!player.Play(engine_->GetAudio().channel(channel), std::move(pcm)))
      throw std::runtime_error(SDL_GetError());
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Amagami audio %s: %s", id.c_str(),
                e.what());
  }
}
void AmagamiRuntime::bindSessionAudio() {
  session_->sound = [this](const std::string &kind, int n) {
    if (kind == "music") {
      if (n < 0)
        music_.Stop();
      else
        audio("music/" + std::to_string(n), "bgm", true);
    } else if (kind == "music-gain")
      music_.SetGain(0.48f * std::clamp(n, 0, 100) / 100.0f);
    else if (kind == "ambience") {
      if (n < 0)
        ambience_.Stop();
      else
        audio("sound/" + std::to_string(n), "ambient", true);
    } else if (kind == "ambience-gain")
      ambience_.SetGain(std::clamp(n, 0, 100) / 100.0f);
    else if (kind == "se")
      audio("se/" + std::to_string(n), "se");
  };
}
void AmagamiRuntime::updateModes(double seconds) {
  if (!session_)
    return;
  if (paused_ || backlog_ || engine_->isAgentPaused() ||
      engine_->isStoryPaused()) {
    session_->setSkipHeld(false);
    return;
  }
  const int speed = std::clamp(session_->messageSpeed(), 0, 3);
  if (lastMessageSpeed_ != speed) {
    if (lastMessageSpeed_ >= 0)
      speedGaugeSeconds_ = 2; // TextSpeedGauge.run: 120 frames.
    lastMessageSpeed_ = speed;
  }
  speedGaugeSeconds_ = std::max(0.0, speedGaugeSeconds_ - seconds);
  // TextWindow.main caps character waits at 2, 1 and 0 frames for levels1..3.
  textBox_->setTextSpeed(speed == 0 ? 10 : speed == 1 ? 30 : 60);
  const float indicatorStep = float(seconds * 10); // six original frames
  auto approach = [&](float &value, bool shown) {
    value = shown ? std::min(1.0f, value + indicatorStep)
                  : std::max(0.0f, value - indicatorStep);
  };
  approach(autoIndicator_, session_->autoMode());
  approach(skipIndicator_, session_->skipMode());
  if (!session_->choices().empty()) {
    // Never run through an answer; choosing remains a distinct player action.
    session_->setSkipHeld(false);
    autoElapsed_ = 0;
    return;
  }
  if (session_->ended() || session_->waiting() || !session_->messageVisible() ||
      session_->messageCleared()) {
    autoElapsed_ = 0;
    return;
  }
  if (speed == 3 || session_->skipping()) {
    textBox_->completeDialogue();
    session_->revealDialogue(SIZE_MAX);
  }
  if (session_->skipping()) {
    advance();
    return;
  }
  if (!session_->autoMode() || !textBox_->isDialogueComplete()) {
    autoElapsed_ = 0;
    return;
  }
  if (session_->autoSync() && voice_.Playing())
    return;
  autoElapsed_ += seconds;
  // waitOX uses original action-queue length, not translated string length.
  // The native host approximates the queue's text portion here; inline control
  // tokens still need to join that queue before exact timing can be claimed.
  const auto characters = Kotonoha::VNTextBoxUtility::CountUtf8Chars(
      DisplayText(session_->dialogue().original));
  const double wait = (40 + characters * 3) *
                      (4 - std::clamp(session_->autoWait(), 0, 3)) / 120.0;
  if (autoElapsed_ >= wait) {
    autoElapsed_ = 0;
    advance();
  }
}
void AmagamiRuntime::renderModeIndicators(Kotonoha::RendererRef r) {
  auto draw = [&](const std::string &id, float x, float y, float reveal,
                  SDL_Color light = {255, 255, 255, 255},
                  const std::string &original = {}) {
    if (reveal <= 0 || !assets_->contains(id))
      return;
    auto &texture = assets_->texture(r, id);
    float width = 0, height = 0;
    SDL_GetTextureSize(&texture, &width, &height);
    if (!original.empty()) {
      const auto translated = engine_->GetTranslation().text(-1, original);
      if (translated != original && !translated.empty()) {
        float size = 14;
        while (size > 9 &&
               engine_->GetVNUI().measureSized(translated, 2000, size).x >
                   width)
          --size;
        light.a = Uint8(reveal * 255);
        engine_->GetVNUI().textSized(r, translated, x - width / 2, y - size / 2,
                                     width, size, light);
        return;
      }
    }
    SDL_FRect destination{x - width / 2, y - height * reveal / 2, width,
                          height * reveal};
    SDL_SetTextureAlphaModFloat(&texture, reveal);
    SDL_SetTextureColorMod(&texture, light.r, light.g, light.b);
    Kotonoha::RenderUpscaledTexture(&r, &texture, nullptr, &destination);
    SDL_SetTextureColorMod(&texture, 255, 255, 255);
    SDL_SetTextureAlphaModFloat(&texture, 1);
  };
  // TextWindow.initialize uses native centers (144,50) and (224,50).
  draw("sysgraph/win_auto.tm2", 464, 274, autoIndicator_, {255, 255, 255, 255},
       "auto");
  draw("sysgraph/win_skip.tm2", 544, 274, skipIndicator_, {255, 255, 255, 255},
       "skip");
  if (speedGaugeSeconds_ > 0) {
    const float opacity = float(std::min(1.0, speedGaugeSeconds_ * 10));
    draw("sysgraph/win_speed.tm2", 500, 256, opacity, {255, 255, 255, 255},
         "text speed");
    for (int i = 0; i < 4; ++i) {
      const Uint8 level = i <= session_->messageSpeed() ? 255 : 128;
      draw("sysgraph/textspeed1.tm2", 552 + i * 10, 256, opacity,
           {level, level, level, 255});
    }
  }
}
bool AmagamiRuntime::handleBacklogAction() {
  if (!session_ || !backlogView_)
    return false;
  session_->setSkipHeld(false);
  if (backlogView_->isVisible())
    backlogView_->close();
  else {
    paused_ = false;
    backlogView_->setHistory(session_->history(), translationRoute());
    backlogView_->open();
    backlog_ = true;
    frameClock_.reset();
  }
  return true;
}
bool AmagamiRuntime::handleNewGameAction() {
  start("n0dea_ev01A", true);
  return true;
}
bool AmagamiRuntime::handleAdvanceAction() {
  if (!session_ || session_->ended() || session_->waiting() || paused_ ||
      backlog_ || !session_->choices().empty())
    throw std::logic_error("Amagami story advance is blocked");
  advance();
  return true;
}
bool AmagamiRuntime::advanceStoryTime(double seconds) {
  if (!session_ || paused_ || backlog_)
    throw std::logic_error("No active Amagami story to step");
  updatePresentation(seconds);
  frameClock_.reset();
  return true;
}
void AmagamiRuntime::advance() {
  if (!session_ || session_->ended() || session_->waiting())
    return;
  syncDialogue();
  if (session_->choices().empty() && textBox_ &&
      !textBox_->isDialogueComplete()) {
    textBox_->completeDialogue();
    session_->revealDialogue(SIZE_MAX);
    return;
  }
  voice_.Stop();
  if (session_->choices().empty())
    session_->advance();
  else
    session_->choose(focus_);
  focus_ = 0;
  syncDialogue();
}
void AmagamiRuntime::picture(SDL_Renderer *r, const std::string &id,
                             SDL_FRect rect) {
  if (!assets_->contains(id))
    return;
  auto &t = assets_->texture(*r, id);
  Kotonoha::RenderUpscaledTexture(r, &t, nullptr, &rect);
}
void AmagamiRuntime::actor(SDL_Renderer *r, const Visual &v) {
  const float zoom = 0.5f * v.scaleX * (1 + v.stretchX);
  const float zoomY = 0.5f * v.scaleY * (1 + v.stretchY);
  int eyeFrame = std::clamp(v.eye, 0, 2),
      mouthFrame = std::clamp(v.mouth, 0, 2);
  if (v.blink && v.eye == 0 && v.blinkFrame >= v.nextBlink) {
    const double frame = v.blinkFrame - v.nextBlink;
    eyeFrame = frame < 2 || frame >= 8 ? 1 : 2;
  }
  static const std::vector<std::string> actorCodes{
      "PLY", "TUK", "RIH", "CAO", "SAE", "AII", "HAR", "MIY",
      "RIS", "UME", "MAY", "KAN", "MAN", "KAO", "AYU", "YUK"};
  // Actor.fadeCloth formats cnum as hexadecimal, not decimal. Characters a..f
  // share the same origin/face tables and must not index past those tables.
  const char digit = v.file.size() > 7 ? v.file[7] : '0';
  const size_t actorId =
      digit >= '0' && digit <= '9'   ? size_t(digit - '0')
      : digit >= 'a' && digit <= 'f' ? size_t(digit - 'a' + 10)
      : digit >= 'A' && digit <= 'F' ? size_t(digit - 'A' + 10)
                                     : actorCodes.size();
  if (actorId >= actorCodes.size())
    throw std::runtime_error("Invalid actor ID in texture path: " + v.file);
  if (session_ && actorId < actorCodes.size() &&
      session_->dialogue().voiceCode == actorCodes[actorId] && !v.manualMouth &&
      voice_.Playing()) {
    const size_t frame = size_t(voice_.Position() * 60);
    if (frame < voiceEnvelope_.size())
      mouthFrame = voiceEnvelope_[frame] > 0.04f    ? 0
                   : voiceEnvelope_[frame] > 0.015f ? 1
                                                    : 2;
  }
  const auto origin = assets_->source()->actorOrigin(unsigned(actorId), v.pose);
  const float left =
      320 + v.x + v.motionX + v.nodX + origin.x * v.scaleX * (1 + v.stretchX);
  const float top =
      224 + v.y + v.motionY + v.nodY + origin.y * v.scaleY * (1 + v.stretchY);
  auto rectangle = [&](SDL_FRect part) {
    return SDL_FRect{left + part.x * zoom, top + part.y * zoomY, part.w * zoom,
                     part.h * zoomY};
  };
  const auto bounds = assets_->bodyBounds(v.file);
  auto &body = assets_->texture(*r, "actor-body/" + v.file);
  const auto destination = rectangle(
      {float(bounds.x), float(bounds.y), float(bounds.w), float(bounds.h)});
  SDL_SetTextureAlphaModFloat(&body, std::clamp(v.alpha, 0.0f, 1.0f));
  Kotonoha::RenderUpscaledTexture(r, &body, nullptr, &destination);
  SDL_SetTextureAlphaModFloat(&body, 1);
  SDL_FPoint mouthPoint{400, 425};
  if (!v.face.empty() && assets_->contains(v.face)) {
    const auto &parts = assets_->fragments(v.face);
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto &part = parts[i];
      const auto group = part.group;
      if (group < 1 || group > 3)
        continue;
      const auto expression = size_t(
          std::clamp(group == 3 ? v.mouthExpression : v.expression, 0, 8));
      const auto selected = (group - 1) * (parts.size() / 3) + expression * 3 +
                            size_t(group == 3 ? mouthFrame : eyeFrame);
      if (i != selected)
        continue;
      if (group == 3)
        mouthPoint = {part.destination.x + part.destination.w / 2,
                      part.destination.y + part.destination.h / 2};
      const auto dst = rectangle(part.destination);
      auto &tile = assets_->texture(*r, "actor-face/" + v.face + "/" +
                                            std::to_string(i));
      SDL_SetTextureBlendMode(&tile, SDL_BLENDMODE_BLEND);
      SDL_SetTextureAlphaModFloat(&tile, std::clamp(v.alpha, 0.0f, 1.0f));
      Kotonoha::RenderUpscaledTexture(r, &tile, nullptr, &dst);
      SDL_SetTextureAlphaModFloat(&tile, 1);
    }
  }
  if (v.breathAge >= 0 && v.breathTime > 0) {
    const int dir = std::clamp(v.breathDirection, 0, 8);
    constexpr int dx[]{0, 1, -1}, dy[]{1, -1, 0};
    constexpr float drift[]{0.144f, 0.192f, 0.256f};
    constexpr float startSize[]{0.08f, 0.06f, 0.045f},
        endSize[]{0.256f, 0.192f, 0.144f};
    constexpr float fadeIn[]{2, 3, 5}, fadeOut[]{2, 3, 4}, fadeAt[]{2, 3, 5};
    const float age = float(v.breathAge / v.breathTime);
    const float progress = std::sin(std::min(1.0f, age / 9) * 1.57079632679f);
    for (size_t i = 0; i < 3; ++i) {
      const float opacity =
          v.alpha * v.breathDensity * std::clamp(age / fadeIn[i], 0.0f, 1.0f) *
          std::clamp(1 - (age - fadeAt[i]) / fadeOut[i], 0.0f, 1.0f);
      if (opacity <= 0)
        continue;
      const std::string id = "sysgraph/breath" + std::to_string(i + 1) + ".tm2";
      if (!assets_->contains(id))
        continue;
      auto &tex = assets_->texture(*r, id);
      const float size =
          128 * v.breathSize *
          (startSize[i] + (endSize[i] - startSize[i]) * progress);
      const float travel = drift[i] * v.breathSize * 32 * progress;
      const float x = left + (mouthPoint.x + dx[dir % 3] * travel) * zoom;
      const float y = top + (mouthPoint.y + dy[dir / 3] * travel) * zoomY;
      SDL_FRect puff{x - size * zoom / 2, y - size * zoomY / 2, size * zoom,
                     size * zoomY};
      SDL_SetTextureAlphaModFloat(&tex, opacity);
      Kotonoha::RenderUpscaledTexture(r, &tex, nullptr, &puff);
      SDL_SetTextureAlphaModFloat(&tex, 1);
    }
  }
}
void AmagamiRuntime::render(Kotonoha::RendererRef r, float scale) {
  if (GetState() == Kotonoha::GameState::Splash) {
    Runtime::render(r, scale);
    return;
  }
  Kotonoha::UI::FrameScope frame(r, Kotonoha::UI::fit({640, 448}, viewport()));
  auto &ui = engine_->GetUI();
  ui.panel(r, {0, 0, 640, 448}, {0, 0, 0, 255});
  if (!session_) {
    picture(&r, "bg/164_10.tm2", {0, 0, 640, 448});
    picture(&r, "bg/140_20.tm2", {0, 0, 640, 448});
    picture(&r, "sysgraph/rogo1snow.tm2", {468, 128, 64, 64});
    picture(&r, "sysgraph/copy.tm2", {16, 404, 320, 24});
    if (!titleMenu_) {
      const float alpha = 0.65f + 0.35f * std::sin(float(SDL_GetTicks()) / 500);
      const auto prompt = engine_->GetTranslation().text(-1, "PRESS START");
      if (prompt != "PRESS START") {
        ui.textSized(r, prompt, 196, 322, 248, 22,
                     {20, 25, 35, Uint8(alpha * 255)});
      } else {
        auto &tex = assets_->texture(r, "sysgraph/txt_start.tm2");
        SDL_SetTextureAlphaModFloat(&tex, alpha);
        SDL_FRect dst{196, 320, 248, 32};
        SDL_RenderTexture(&r, &tex, nullptr, &dst);
        SDL_SetTextureAlphaModFloat(&tex, 1);
      }
    } else {
      const auto heading = engine_->GetTranslation().text(-1, "メインメニュー");
      if (heading == "メインメニュー")
        picture(&r, "sysgraph/menu_main.tm2", {232, 245, 176, 56});
      else {
        ui.textSized(r, heading, 232, 260, 176, 22, {20, 25, 35, 255});
      }
      for (size_t i = 0; i < 3; ++i) {
        SDL_FRect b{264, float(305 + i * 36), 112, 32};
        const std::string original = i == 0   ? "はじめから"
                                     : i == 1 ? "続きから"
                                              : "終了";
        const auto label = engine_->GetTranslation().text(-1, original);
        const bool isText = (label != original || i == 2);
        if (focus_ == i) {
          auto &tex = assets_->texture(r, "sysgraph/cursor_base.tm2");
          Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &b);
        }
        if (isText)
          ui.textSized(r, label, b.x + 6, b.y + 5, 100, 20, {15, 25, 35, 255});
        else
          picture(&r, "sysgraph/menu_main" + std::to_string(i) + ".tm2", b);
      }
    }
  } else {
    for (const auto &[name, v] : session_->visuals()) {
      if (name == "ITEM_BASE" || name == "ITEM_ICON")
        continue;
      if (!v.visible || v.file.empty())
        continue;
      if (v.file.rfind("stand/b", 0) == 0) {
        auto live = v;
        if (auto transition = session_->actorTransitions().find(name);
            transition != session_->actorTransitions().end()) {
          const float remaining =
              float(transition->second.remaining / transition->second.frames);
          auto old = transition->second.previous;
          old.alpha *= remaining;
          actor(&r, old);
          live.alpha *= 1 - remaining;
        }
        actor(&r, live);
      } else {
        auto &tex = assets_->texture(r, v.file);
        // A quarter-turn exchanges the image axes before camera scaling.
        const bool quarterTurn =
            std::abs(std::sin(v.angle * 3.14159265358979323846)) > 0.999;
        const float width = 640 * (quarterTurn ? v.scaleY : v.scaleX);
        const float height = 448 * (quarterTurn ? v.scaleX : v.scaleY);
        SDL_FRect dst{320 + v.x - width / 2, 224 + v.y - height / 2, width,
                      height};
        SDL_SetTextureAlphaModFloat(&tex, std::clamp(v.alpha, 0.0f, 1.0f));
        if (!Kotonoha::RenderUpscaledTextureRotated(&r, &tex, nullptr, &dst,
                                                    -v.angle * 180.0, nullptr,
                                                    SDL_FLIP_NONE))
          throw std::runtime_error(SDL_GetError());
        SDL_SetTextureAlphaModFloat(&tex, 1);
      }
    }
    if (session_->stageFade() > 0) {
      Uint8 shade = session_->whiteFade() ? 255 : 0;
      ui.panel(r, {0, 0, 640, 448},
               {shade, shade, shade, Uint8(session_->stageFade() * 255)});
    }
    // ItemView is a foreground overlay, independent of the background camera.
    for (const auto &key : {"ITEM_BASE", "ITEM_ICON"}) {
      const auto it = session_->visuals().find(key);
      if (it == session_->visuals().end() || !it->second.visible)
        continue;
      const auto &v = it->second;
      auto &tex = assets_->texture(r, v.file);
      float width = 0, height = 0;
      SDL_GetTextureSize(&tex, &width, &height);
      SDL_FRect rect{320 - width * v.scaleX / 2, 224 - height * v.scaleY / 2,
                     width * v.scaleX, height * v.scaleY};
      SDL_SetTextureAlphaModFloat(&tex, std::clamp(v.alpha, 0.0f, 1.0f));
      Kotonoha::RenderUpscaledTexture(&r, &tex, nullptr, &rect);
      SDL_SetTextureAlphaModFloat(&tex, 1);
    }
    syncChoices();
    if (choices_->isActive())
      choices_->render(r, scale);
    else if (session_->messageVisible()) {
      picture(&r, "sysgraph/win0.tm2", {24, 266, 592, 160});
      syncDialogue();
      renderModeIndicators(r);
      if (textBox_ && !session_->messageCleared()) {
        textBox_->show();
        textBox_->render(r, scale);
        if (textBox_->isDialogueComplete())
          picture(&r, "sysgraph/flower.tm2", {572, 394, 24, 24});
      }
    }
    if (session_->ended() && !session_->campaignActive()) {
      ui.panel(r, {60, 130, 520, 120});
      ui.textSized(
          r, engine_->GetTranslation().text(-1, "このイベントは終了しました"),
          85, 150, 470, 25);
      ui.textSized(r, engine_->GetTranslation().text(-1, "Enter: タイトルへ"),
                   85, 195, 470, 20);
    }
    syncCampaignMap();
    if (eventMap_->isActive())
      eventMap_->render(r, scale);
  }
}

void AmagamiRuntime::renderOverlay(Kotonoha::RendererRef r, float scale) {
  {
    Kotonoha::UI::FrameScope frame(r, Kotonoha::UI::fit({640, 448}, viewport()));
    auto &ui = engine_->GetUI();
    if (session_ && paused_) {
      ui.backdrop(r);
      const std::vector<std::string> menu = {"戻る", "セーブ", "ロード",
                                             "タイトルへ", "設定"};
      for (size_t i = 0; i < menu.size(); ++i) {
        SDL_FRect b{180, float(100 + i * 54), 280, 44};
        ui.panel(r, b,
                 i == focus_ ? SDL_Color{133, 58, 110, 255}
                             : SDL_Color{25, 30, 50, 255});
        ui.textSized(r, engine_->GetTranslation().text(-1, menu[i]), b.x + 15,
                     b.y + 8, b.w - 30, 24);
      }
    }
    if (!notice_.empty())
      ui.textSized(r, engine_->GetTranslation().text(-1, notice_), 10, 5, 620, 14,
                   {255, 235, 150, 255});
    if (!error_.empty()) {
      ui.panel(r, {20, 30, 600, 180}, {60, 15, 25, 250});
      ui.textSized(
          r, engine_->GetTranslation().text(-1, "Execution stopped: ") + error_,
          35, 45, 570, 16);
    }
  }
  Runtime::renderOverlay(r, scale);
}
void AmagamiRuntime::activate(size_t i) {
  if (!session_) {
    if (!titleMenu_) {
      titleMenu_ = true;
      focus_ = 0;
      return;
    }
    if (i == 0)
      start("n0dea_ev01A", true);
    else if (i == 1) {
      if (!session_)
        session_ = std::make_unique<Session>(assets_->source());
      loadGame(savePath_);
    } else {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
    return;
  }
  if (paused_) {
    if (i == 1)
      saveGame(savePath_);
    else if (i == 2)
      loadGame(savePath_);
    else if (i == 4)
      engine_->openSettings();
    else if (i == 3) {
      shutdown();
      SetState(Kotonoha::GameState::Menu);
    }
    paused_ = false;
    focus_ = 0;
    return;
  }
  if (session_->ended()) {
    shutdown();
    SetState(Kotonoha::GameState::Menu);
    focus_ = 0;
    return;
  }
  advance();
}
bool AmagamiRuntime::onEvent(Kotonoha::EventRef event) {
  if (GetState() == Kotonoha::GameState::Splash) {
    return Runtime::onEvent(event);
  }
  // Shared dialogs keep their original input, including controller shortcuts.
  for (auto *scene : GetActiveScenes())
    if (scene && scene->capturesInput() && scene != backlogView_.get() &&
        scene != choices_.get() && scene != eventMap_.get()) {
      if (session_)
        session_->setSkipHeld(false);
      return Runtime::onEvent(event);
    }
  try {
    auto e = Kotonoha::UI::toFrame(
        Kotonoha::MapControllerEvent(
            event,
            session_ && !session_->ended() && !paused_ && !backlog_ &&
                !engine_->isStoryPaused() && session_->choices().empty(),
            backlog_),
        Kotonoha::UI::fit({640, 448}, viewport()));
    syncChoices();
    syncCampaignMap();
    if (session_ && (e.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                     e.type == SDL_EVENT_GAMEPAD_REMOVED ||
                     (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
                      event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) ||
                     (e.type == SDL_EVENT_KEY_UP &&
                      (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL))))
      session_->setSkipHeld(false);
    if (backlogView_ && backlogView_->isVisible()) {
      backlogView_->onEvent(e);
      return true;
    }
    if (session_ && !paused_ && !backlog_ && eventMap_->isActive()) {
      const bool menuKey = e.type == SDL_EVENT_KEY_DOWN &&
                           (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_F5 ||
                            e.key.key == SDLK_F9 || e.key.key == SDLK_PAGEUP ||
                            e.key.key == SDLK_B);
      if (!menuKey) {
        eventMap_->onEvent(e);
        return true;
      }
    }
    if (session_ && !paused_ && !backlog_ && choices_->isActive()) {
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key >= SDLK_1 &&
          e.key.key <= SDLK_9 && !e.key.repeat) {
        size_t i = e.key.key - SDLK_1;
        if (i < choices_->getOptions().size())
          choices_->chooseOption(i);
        return true;
      }
      const bool globalKey =
          e.type == SDL_EVENT_KEY_DOWN &&
          (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_PAGEUP ||
           e.key.key == SDLK_F5 || e.key.key == SDLK_F9);
      if (!globalKey) {
        choices_->onEvent(e);
        return true;
      }
    }
    if (session_ && !paused_ && !backlog_ && session_->choices().empty()) {
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_A) {
          session_->setAutoMode(!session_->autoMode());
          audio("se/50", "se");
          autoElapsed_ = 0;
          return true;
        }
        if (e.key.key == SDLK_S) {
          session_->setSkipMode(!session_->skipMode());
          audio("se/50", "se");
          return true;
        }
        if (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL) {
          session_->setSkipHeld(true);
          return true;
        }
        if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS ||
            e.key.key == SDLK_MINUS) {
          const int previous = session_->messageSpeed();
          session_->setMessageSpeed(previous +
                                    (e.key.key == SDLK_MINUS ? -1 : 1));
          if (session_->messageSpeed() != previous)
            audio("se/21", "se");
          return true;
        }
      }
      if (e.type == SDL_EVENT_KEY_UP &&
          (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL)) {
        session_->setSkipHeld(false);
        return true;
      }
    }
    if (session_ && e.type == SDL_EVENT_WINDOW_FOCUS_LOST)
      session_->setSkipHeld(false);
    size_t count = !session_ ? 3 : paused_ ? 5 : session_->choices().size();
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_ESCAPE) {
        if (session_)
          session_->setSkipHeld(false);
        if (!error_.empty()) {
          error_.clear();
          shutdown();
        } else {
          paused_ = !paused_;
          focus_ = 0;
        }
        return true;
      }
      if (e.key.key == SDLK_PAGEUP || e.key.key == SDLK_B) {
        handleBacklogAction();
        return true;
      }
      if (e.key.key == SDLK_F5 && session_) {
        saveGame(savePath_);
        notice_ = "Saved";
        return true;
      }
      if (e.key.key == SDLK_F9 && session_) {
        loadGame(savePath_);
        notice_ = "Loaded";
        return true;
      }
      if (backlog_) {
        backlog_ = false;
        return true;
      }
      if (count && (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN)) {
        focus_ = (focus_ + (e.key.key == SDLK_UP ? count - 1 : 1)) % count;
        return true;
      }
      if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9 && session_ &&
          !session_->choices().empty()) {
        size_t i = e.key.key - SDLK_1;
        if (i < count) {
          focus_ = i;
          activate(i);
        }
        return true;
      }
      if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
        if (error_.empty())
          activate(focus_);
        return true;
      }
    }
    const bool isLeftClick = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                              e.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                             e.button.button == SDL_BUTTON_LEFT;
    const bool isTouchAct = e.button.which == SDL_TOUCH_MOUSEID
                                ? e.type == SDL_EVENT_MOUSE_BUTTON_UP
                                : e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    if (isLeftClick && isTouchAct && error_.empty() && e.button.x >= 0 &&
        e.button.x < 640 && e.button.y >= 0 && e.button.y < 448) {
      if (backlog_) {
        backlog_ = false;
        return true;
      }
      if (!session_ && !titleMenu_) {
        activate(0);
        return true;
      }
      for (size_t i = 0; i < count; ++i) {
        SDL_FRect b = !session_ ? SDL_FRect{220, float(300 + i * 36), 200, 38}
                      : paused_ ? SDL_FRect{180, float(100 + i * 54), 280, 44}
                                : SDL_FRect{55, float(70 + i * 49), 530, 44};
        if (Kotonoha::UI::clicked(e, b)) {
          focus_ = i;
          activate(i);
          return true;
        }
      }
      if (count == 0) {
        activate(0);
        return true;
      }
    }
  } catch (const std::exception &e) {
    error_ = e.what();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Amagami: %s", e.what());
    return true;
  }
  return false;
}
void AmagamiRuntime::updatePresentation(double seconds) {
  if (session_ && !paused_ && !backlog_) {
    session_->tick(seconds);
    presentationSeconds_ += seconds;
  }
  syncDialogue();
  syncChoices();
  syncCampaignMap();
}
void AmagamiRuntime::update() {
  if (GetState() == Kotonoha::GameState::Splash) {
    Runtime::update();
    return;
  }
  const double delta = frameClock_.seconds();
  frameClock_.reset();
  if (session_ && !paused_ && !backlog_ && !engine_->isStoryPaused()) {
    try {
      updatePresentation(delta);
    } catch (const std::exception &e) {
      error_ = e.what();
    }
  }
  syncDialogue();
  syncChoices();
  syncCampaignMap();
  if (textBox_) {
    if (paused_ || backlog_ || !session_ || eventMap_->isActive() ||
        !session_->messageVisible() || session_->messageCleared())
      textBox_->hide();
    else
      textBox_->show();
    Runtime::update();
    if (session_ && !paused_ && !backlog_ && session_->messageVisible())
      session_->revealDialogue(Kotonoha::VNTextBoxUtility::CountUtf8Chars(
          textBox_->getVisibleText()));
  }
  updateModes(delta);
  const bool pausedAudio = paused_ || backlog_ || engine_->isStoryPaused();
  music_.SetPaused(pausedAudio);
  ambience_.SetPaused(pausedAudio);
  voice_.SetPaused(pausedAudio);
  const bool externalPause = engine_->isAgentPaused() ||
                             engine_->GetSettingsMenu().isOpen() ||
                             engine_->GetDebug().pausesStory();
  for (auto &effect : soundEffects_)
    effect->SetPaused(paused_ || externalPause);
  backlogVoice_.SetPaused(externalPause);
  if (session_ && !pausedAudio && voiced_ != session_->history().size()) {
    voiced_ = session_->history().size();
    const auto &line = session_->dialogue();
    if (line.voice >= 0 && !line.voiceCode.empty())
      audio("voice/" + line.voiceCode + "/" + std::to_string(line.voice),
            "voice");
  }
}
SDL_FPoint AmagamiRuntime::sceneSize(const Kotonoha::Scene &scene) const {
  if (dynamic_cast<const AmagamiTextBox *>(&scene) ||
      dynamic_cast<const Kotonoha::SubtitleScene *>(&scene) ||
      dynamic_cast<const AmagamiChoices *>(&scene) ||
      dynamic_cast<const AmagamiBacklog *>(&scene) ||
      dynamic_cast<const AmagamiEventMap *>(&scene))
    return {640, 448};
  return Runtime::sceneSize(scene);
}
std::string AmagamiRuntime::debugNode() const {
  return session_ ? session_->location() : "title";
}
std::vector<Kotonoha::DebugNode> AmagamiRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> out;
  for (const auto &name : assets_->source()->scenarios())
    out.push_back({name, name, {}});
  return out;
}
void AmagamiRuntime::debugJumpToNode(const std::string &id) { start(id); }
std::vector<std::string> AmagamiRuntime::activeTranslationKeys() const {
  if (!session_)
    return {};
  if (eventMap_ && eventMap_->isActive())
    return eventMap_->sourceKeys();
  std::vector<std::string> keys;
  if (!session_->choices().empty())
    for (const auto &choice : session_->choices())
      keys.push_back(choice.text);
  else if (session_->messageVisible() && !session_->messageCleared())
    keys.push_back(session_->dialogue().original);
  return keys;
}
int AmagamiRuntime::translationRoute() const {
  if (eventMap_ && eventMap_->isActive())
    return -1;
  return session_ ? int(assets_->source()->scenarioIndex(session_->name()))
                  : -1;
}
Kotonoha::Translation::SourceTexts AmagamiRuntime::translationSources() const {
  Kotonoha::Translation::SourceTexts sources;
  sources[-1] = {"行動を選択してください",
                 "次の時間へ",
                 "日付",
                 "時間",
                 "auto",
                 "skip",
                 "text speed",
                 "はじめから",
                 "続きから",
                 "終了",
                 "戻る",
                 "セーブ",
                 "ロード",
                 "タイトルへ",
                 "このイベントは終了しました",
                 "Enter: タイトルへ",
                 "Saved",
                 "Loaded",
                 "Execution stopped: ",
                 "PRESS START",
                 "メインメニュー",
                 "選択肢",
                 "設定",
                 "バックログ"};
  for (size_t i = 0; i < assets_->source()->classes().size(); ++i) {
    auto texts = assets_->source()->systemClass(i).texts();
    sources[-1].insert(sources[-1].end(), texts.begin(), texts.end());
  }
  for (size_t i = 0; i < assets_->source()->scenarios().size(); ++i)
    sources.emplace(int(i), assets_->source()->script(i).texts());
  return sources;
}
std::vector<int> AmagamiRuntime::translationRoutes() const {
  std::vector<int> out{-1};
  for (size_t i = 0; i < assets_->source()->scenarios().size(); ++i)
    out.push_back(int(i));
  return out;
}
std::vector<std::string> AmagamiRuntime::translationTexts(int route) const {
  return route < 0 ? translationSources().at(-1)
                   : assets_->source()->script(route).texts();
}
void AmagamiRuntime::saveGame(const std::string &path) {
  if (!session_)
    throw std::runtime_error("No active Amagami game");
  session_->save(path);
}
void AmagamiRuntime::loadGame(const std::string &path) {
  if (!session_)
    session_ = std::make_unique<Session>(assets_->source());
  session_->load(path);
  session_->setRealtime(true);
  frameClock_.reset();
  choices_->hide();
  eventMap_->hide();
  campaignMapKey_.clear();
  bindSessionAudio();
  voice_.Stop();
  music_.Stop();
  ambience_.Stop();
  soundEffects_.clear();
  nextSoundEffect_ = 0;
  if (session_->musicTrack() >= 0)
    audio("music/" + std::to_string(session_->musicTrack()), "bgm", true);
  music_.SetGain(0.48f * session_->musicGain() / 100.0f);
  if (session_->ambienceTrack() >= 0)
    audio("sound/" + std::to_string(session_->ambienceTrack()), "ambient",
          true);
  ambience_.SetGain(session_->ambienceGain() / 100.0f);
  SetState(Kotonoha::GameState::Gameplay);
  paused_ = false;
  backlog_ = false;
  backlogView_->close();
  error_.clear();
  focus_ = 0;
  voiced_ = 0;
  displayed_ = 0;
  presentationSeconds_ = 0;
  autoElapsed_ = speedGaugeSeconds_ = 0;
  autoIndicator_ = skipIndicator_ = 0;
  lastMessageSpeed_ = -1;
  textBox_->setDialogue("", "");
  textBox_->hide();
  syncDialogue();
  syncChoices();
  syncCampaignMap();
  if (!eventMap_->isActive() && session_->messageVisible() &&
      !session_->messageCleared())
    textBox_->show();
}
} // namespace Amagami
