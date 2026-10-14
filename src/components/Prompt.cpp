#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <vector>

namespace Kotonoha {

namespace {

bool PromptTextValid(const std::string &text) { return !text.empty(); }

std::vector<int>
BuildVisiblePromptIndices(const std::vector<std::string> &items) {
  std::vector<int> indices;
  indices.reserve(items.size());

  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    if (PromptTextValid(items[i])) {
      indices.push_back(i);
    }
  }

  return indices;
}

} // namespace

Prompt::Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start,
               Uint64 end, Kotonoha_Time *time, TTF_Font *font,
               const char *clickAudioPath)
    : prompt(std::move(prompt)), promptResult(promptResult), time(time),
      startTime(start), endTime(end), font(font),
      audioPath(clickAudioPath ? clickAudioPath : "assets/Se/click.ogg") {}

void Prompt::HandleEvent(SDL_Event *event, struct Kotonoha_Game *gameContext,
                         Audio *audioManager) {
  if (event == nullptr || promptResult == nullptr || lastLayout.empty()) {
    return;
  }

  float clickX = -1.0f;
  float clickY = -1.0f;
  bool isClick = false;

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event->button.button == SDL_BUTTON_LEFT) {
    clickX = event->button.x;
    clickY = event->button.y;
    isClick = true;
  } else if (event->type == SDL_EVENT_FINGER_DOWN && gameContext != nullptr &&
             gameContext->window != nullptr) {
    int w = 0, h = 0;
    SDL_GetWindowSize(gameContext->window, &w, &h);
    clickX = event->tfinger.x * static_cast<float>(w);
    clickY = event->tfinger.y * static_cast<float>(h);
    isClick = true;
  }

  if (!isClick) {
    return;
  }

  for (const auto &item : lastLayout) {
    const bool hit =
        clickX >= item.rect.x && clickX <= (item.rect.x + item.rect.w) &&
        clickY >= item.rect.y && clickY <= (item.rect.y + item.rect.h);

    if (hit) {
      if (*promptResult == item.originalIndex) {
        *promptResult = -1;
      } else {
        *promptResult = item.originalIndex;
      }

      // Toca o áudio de seleção imediatamente
      if (audioManager != nullptr && !audioPath.empty() && time != nullptr) {
        const Uint64 nowMs = KotonohaTimeGet(time);
        audioManager->AddMedia(audioPath.c_str(), nowMs, nowMs + 3000, false,
                               "Se");
      }
      break;
    }
  }
}

enum Kotonoha_Scene_Status Prompt::Render(KOTONOHA_SCENE_CALL) {
  if (userData == nullptr || render == nullptr || target == nullptr) {
    return KOTONOHA_SCENE_NULL;
  }

  auto *promptObj = static_cast<Prompt *>(userData);

  if (promptObj->font == nullptr || promptObj->time == nullptr ||
      promptObj->promptResult == nullptr) {
    return KOTONOHA_SCENE_NULL;
  }

  bool inRange = false;
  Sint64 diff = 0;
  KotonohaTimeGetFromEvent(promptObj->time, promptObj->startTime,
                           promptObj->endTime, &inRange, &diff);

  if (!inRange) {
    promptObj->lastLayout.clear();
    return KOTONOHA_SCENE_NULL;
  }

  const std::vector<int> visibleIndices =
      BuildVisiblePromptIndices(promptObj->prompt);

  if (visibleIndices.empty()) {
    promptObj->lastLayout.clear();
    return KOTONOHA_SCENE_DRAW_OVERLAYED;
  }

  float mouseX = 0.0f;
  float mouseY = 0.0f;
  SDL_GetMouseState(&mouseX, &mouseY);

  std::vector<PromptLayoutItem> layout;
  layout.reserve(visibleIndices.size());

  const int targetW = static_cast<int>(target->w);
  const int targetH = static_cast<int>(target->h);
  const int centerY = targetH / 2;
  const int halfW = targetW / 2;
  const int paddingX = 24;
  const int paddingY = 16;

  if (visibleIndices.size() == 1) {
    const int idx = visibleIndices[0];

    int textW = 0;
    int textH = 0;
    if (!TTF_GetStringSize(promptObj->font, promptObj->prompt[idx].c_str(),
                           promptObj->prompt[idx].size(), &textW, &textH)) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to measure prompt text: %s", SDL_GetError());
      return KOTONOHA_SCENE_DRAW_OVERLAYED;
    }

    SDL_FRect rect{};
    rect.w = static_cast<float>(textW + paddingX * 2);
    rect.h = static_cast<float>(textH + paddingY * 2);
    rect.x = static_cast<float>(targetW - rect.w) / 2.0f;
    rect.y = static_cast<float>(targetH - rect.h) / 2.0f;

    layout.push_back({idx, rect});
  } else {
    const size_t count = visibleIndices.size() > 2 ? 2 : visibleIndices.size();

    for (size_t slot = 0; slot < count; ++slot) {
      const int idx = visibleIndices[slot];

      int textW = 0;
      int textH = 0;
      if (!TTF_GetStringSize(promptObj->font, promptObj->prompt[idx].c_str(),
                             promptObj->prompt[idx].size(), &textW, &textH)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to measure prompt text: %s", SDL_GetError());
        continue;
      }

      SDL_FRect rect{};
      rect.w = static_cast<float>(textW + paddingX * 2);
      rect.h = static_cast<float>(textH + paddingY * 2);

      const float regionX = (slot == 0) ? 0.0f : static_cast<float>(halfW);
      const float regionW = static_cast<float>(halfW);

      rect.x = regionX + (regionW - rect.w) / 2.0f;
      rect.y = static_cast<float>(centerY) - (rect.h / 2.0f);

      layout.push_back({idx, rect});
    }
  }

  promptObj->lastLayout = layout;

  for (const auto &item : layout) {
    const bool hovered =
        mouseX >= item.rect.x && mouseX <= (item.rect.x + item.rect.w) &&
        mouseY >= item.rect.y && mouseY <= (item.rect.y + item.rect.h);

    const bool selected = (*(promptObj->promptResult) == item.originalIndex);

    SDL_Color fg = selected  ? SDL_Color{255, 230, 120, 255}
                   : hovered ? SDL_Color{180, 220, 255, 255}
                             : SDL_Color{255, 255, 255, 255};

    SDL_Color shadow = {0, 0, 0, static_cast<Uint8>(hovered ? 200 : 160)};

    SDL_Surface *shadowSurface = TTF_RenderText_Blended(
        promptObj->font, promptObj->prompt[item.originalIndex].c_str(),
        promptObj->prompt[item.originalIndex].size(), shadow);
    if (shadowSurface == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to render prompt shadow surface: %s",
                   SDL_GetError());
      continue;
    }

    SDL_Texture *shadowTexture =
        SDL_CreateTextureFromSurface(render, shadowSurface);
    if (shadowTexture == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to create prompt shadow texture: %s",
                   SDL_GetError());
      SDL_DestroySurface(shadowSurface);
      continue;
    }

    SDL_Surface *textSurface = TTF_RenderText_Blended(
        promptObj->font, promptObj->prompt[item.originalIndex].c_str(),
        promptObj->prompt[item.originalIndex].size(), fg);
    if (textSurface == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to render prompt text surface: %s", SDL_GetError());
      SDL_DestroyTexture(shadowTexture);
      SDL_DestroySurface(shadowSurface);
      continue;
    }

    SDL_Texture *textTexture =
        SDL_CreateTextureFromSurface(render, textSurface);
    if (textTexture == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Failed to create prompt text texture: %s", SDL_GetError());
      SDL_DestroySurface(textSurface);
      SDL_DestroyTexture(shadowTexture);
      SDL_DestroySurface(shadowSurface);
      continue;
    }

    SDL_SetTextureBlendMode(shadowTexture, SDL_BLENDMODE_BLEND);

    SDL_FRect shadowRect = {item.rect.x + static_cast<float>(paddingX) + 3.0f,
                            item.rect.y + static_cast<float>(paddingY) + 3.0f,
                            static_cast<float>(shadowSurface->w),
                            static_cast<float>(shadowSurface->h)};

    SDL_FRect textRect = {item.rect.x + static_cast<float>(paddingX),
                          item.rect.y + static_cast<float>(paddingY),
                          static_cast<float>(textSurface->w),
                          static_cast<float>(textSurface->h)};

    SDL_RenderTexture(render, shadowTexture, nullptr, &shadowRect);
    SDL_RenderTexture(render, textTexture, nullptr, &textRect);

    SDL_DestroyTexture(textTexture);
    SDL_DestroySurface(textSurface);
    SDL_DestroyTexture(shadowTexture);
    SDL_DestroySurface(shadowSurface);
  }

  return KOTONOHA_SCENE_DRAW_OVERLAYED;
}

} // namespace Kotonoha
