#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/utils/OverlayRender.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdint>
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

Prompt::~Prompt() { ClearLayoutTextures(); }

void Prompt::ClearLayoutTextures() {
  for (auto &item : lastLayout) {
    if (item.textTexture != nullptr) {
      SDL_DestroyTexture(item.textTexture);
      item.textTexture = nullptr;
    }
    if (item.shadowTexture != nullptr) {
      SDL_DestroyTexture(item.shadowTexture);
      item.shadowTexture = nullptr;
    }
  }
  lastLayout.clear();
}

void Prompt::HandleEvent(SDL_Event *event, struct Kotonoha_Game *gameContext,
                         Audio *audioManager) {
  if (event == nullptr || promptResult == nullptr || *promptResult != -1 ||
      lastLayout.empty()) {
    return;
  }

  float rawX = -1.0f;
  float rawY = -1.0f;
  bool isClick = false;

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event->button.button == SDL_BUTTON_LEFT) {
    rawX = event->button.x;
    rawY = event->button.y;
    isClick = true;
  } else if (event->type == SDL_EVENT_FINGER_DOWN && gameContext != nullptr &&
             gameContext->window != nullptr) {
    int w = 0, h = 0;
    // No SDL3, SDL_GetWindowSizeInPixels lida melhor com telas de alta
    // densidade (Retina/DPI)
    SDL_GetWindowSizeInPixels(gameContext->window, &w, &h);
    rawX = event->tfinger.x * static_cast<float>(w);
    rawY = event->tfinger.y * static_cast<float>(h);
    isClick = true;
  }

  if (!isClick) {
    return;
  }

  // Se usamos SDL_GetWindowSizeInPixels, as coordenadas já estão em pixels
  // reais de renderização. Caso contrário, aplicamos a escala se necessário.
  float windowScale = 1.0f;
  if (gameContext != nullptr && gameContext->window != nullptr &&
      event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    windowScale = SDL_GetWindowDisplayScale(gameContext->window);
  }

  const float clickX =
      (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && windowScale > 0.0f)
          ? (rawX * windowScale)
          : rawX;
  const float clickY =
      (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && windowScale > 0.0f)
          ? (rawY * windowScale)
          : rawY;

  for (const auto &item : lastLayout) {
    const bool hit =
        clickX >= item.rect.x && clickX <= (item.rect.x + item.rect.w) &&
        clickY >= item.rect.y && clickY <= (item.rect.y + item.rect.h);

    if (hit) {
      *promptResult = item.originalIndex;

      if (audioManager != nullptr && !audioPath.empty() && time != nullptr) {
        const Uint64 nowMs = KotonohaTimeGet(time);
        audioManager->AddMedia(audioPath.c_str(), nowMs, UINT64_MAX, false,
                               "Se");
      }
      break;
    }
  }
}

enum Kotonoha_Scene_Status Prompt::Render(KOTONOHA_SCENE_CALL) {
  if (userData == nullptr || render == nullptr) {
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
    promptObj->ClearLayoutTextures();
    return KOTONOHA_SCENE_NULL;
  }

  const std::vector<int> visibleIndices =
      BuildVisiblePromptIndices(promptObj->prompt);

  if (visibleIndices.empty()) {
    promptObj->ClearLayoutTextures();
    return KOTONOHA_SCENE_DRAW_OVERLAYED;
  }

  TTF_SetFontSize(promptObj->font, 32);

  int outputWidth = 0;
  int outputHeight = 0;
  float windowScale = 1.0f;

  if (!KotonohaOverlayGetMetrics(window, render, &outputWidth, &outputHeight,
                                 &windowScale)) {
    return KOTONOHA_SCENE_NULL;
  }

  float rawMouseX = 0.0f;
  float rawMouseY = 0.0f;
  SDL_GetMouseState(&rawMouseX, &rawMouseY);

  const float mouseX =
      (windowScale > 0.0f) ? (rawMouseX * windowScale) : rawMouseX;
  const float mouseY =
      (windowScale > 0.0f) ? (rawMouseY * windowScale) : rawMouseY;

  const bool isMouseDown = (SDL_GetMouseState(nullptr, nullptr) &
                            SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;

  const int centerY = outputHeight / 2;
  const int halfW = outputWidth / 2;
  const int paddingX = static_cast<int>(24.0f * windowScale);
  const int paddingY = static_cast<int>(16.0f * windowScale);

  static int lastOutputW = 0;
  static int lastOutputH = 0;
  const bool targetResized =
      (lastOutputW != outputWidth || lastOutputH != outputHeight);

  if (targetResized) {
    promptObj->ClearLayoutTextures();
    lastOutputW = outputWidth;
    lastOutputH = outputHeight;
  }

  if (promptObj->lastLayout.size() != visibleIndices.size()) {
    promptObj->ClearLayoutTextures();
    promptObj->lastLayout.reserve(visibleIndices.size());

    if (visibleIndices.size() == 1) {
      const int idx = visibleIndices[0];
      int textW = 0, textH = 0; // Usando int* para TTF_GetStringSize
      TTF_GetStringSize(promptObj->font, promptObj->prompt[idx].c_str(),
                        promptObj->prompt[idx].size(), &textW, &textH);

      SDL_FRect rect{};
      rect.w = (static_cast<float>(textW) + static_cast<float>(paddingX * 2)) *
               windowScale;
      rect.h = (static_cast<float>(textH) + static_cast<float>(paddingY * 2)) *
               windowScale;
      rect.x = (static_cast<float>(outputWidth) - rect.w) / 2.0f;
      rect.y = (static_cast<float>(outputHeight) - rect.h) / 2.0f;

      PromptLayoutItem item{};
      item.originalIndex = idx;
      item.rect = rect;
      promptObj->lastLayout.push_back(item);
    } else {
      const size_t count =
          visibleIndices.size() > 2 ? 2 : visibleIndices.size();
      for (size_t slot = 0; slot < count; ++slot) {
        const int idx = visibleIndices[slot];
        int textW = 0, textH = 0; // Usando int* para TTF_GetStringSize
        TTF_GetStringSize(promptObj->font, promptObj->prompt[idx].c_str(),
                          promptObj->prompt[idx].size(), &textW, &textH);

        SDL_FRect rect{};
        rect.w =
            (static_cast<float>(textW) + static_cast<float>(paddingX * 2)) *
            windowScale;
        rect.h =
            (static_cast<float>(textH) + static_cast<float>(paddingY * 2)) *
            windowScale;

        const float regionX = (slot == 0) ? 0.0f : static_cast<float>(halfW);
        const float regionW = static_cast<float>(halfW);

        rect.x = regionX + (regionW - rect.w) / 2.0f;
        rect.y = static_cast<float>(centerY) - (rect.h / 2.0f);

        PromptLayoutItem item{};
        item.originalIndex = idx;
        item.rect = rect;
        promptObj->lastLayout.push_back(item);
      }
    }
  }

  for (auto &item : promptObj->lastLayout) {
    const bool hovered =
        mouseX >= item.rect.x && mouseX <= (item.rect.x + item.rect.w) &&
        mouseY >= item.rect.y && mouseY <= (item.rect.y + item.rect.h);

    const bool selected = (*(promptObj->promptResult) == item.originalIndex);
    const bool clicking =
        hovered && isMouseDown && (*(promptObj->promptResult) == -1);

    SDL_Color fg = clicking   ? SDL_Color{255, 140, 0, 255}
                   : selected ? SDL_Color{255, 230, 120, 255}
                   : hovered  ? SDL_Color{180, 220, 255, 255}
                              : SDL_Color{255, 255, 255, 255};

    SDL_Color shadowColor = {0, 0, 0, static_cast<Uint8>(hovered ? 200 : 160)};

    if (item.textTexture != nullptr) {
      SDL_DestroyTexture(item.textTexture);
      item.textTexture = nullptr;
    }
    if (item.shadowTexture != nullptr) {
      SDL_DestroyTexture(item.shadowTexture);
      item.shadowTexture = nullptr;
    }

    float textW = 0.0f;
    float textH = 0.0f;

    const std::string &txt = promptObj->prompt[item.originalIndex];

    if (KotonohaOverlayCreateTextTexture(render, promptObj->font, txt.c_str(),
                                         shadowColor, &item.shadowTexture,
                                         &textW, &textH)) {
      item.shadowW = static_cast<int>(textW * windowScale);
      item.shadowH = static_cast<int>(textH * windowScale);
    }

    if (KotonohaOverlayCreateTextTexture(render, promptObj->font, txt.c_str(),
                                         fg, &item.textTexture, &textW,
                                         &textH)) {
      item.textW = static_cast<int>(textW * windowScale);
      item.textH = static_cast<int>(textH * windowScale);
    }

    if (item.shadowTexture != nullptr) {
      SDL_SetTextureBlendMode(item.shadowTexture, SDL_BLENDMODE_BLEND);
      SDL_FRect shadowRect = {
          item.rect.x + static_cast<float>(paddingX) + (3.0f * windowScale),
          item.rect.y + static_cast<float>(paddingY) + (3.0f * windowScale),
          static_cast<float>(item.shadowW), static_cast<float>(item.shadowH)};
      SDL_RenderTexture(render, item.shadowTexture, nullptr, &shadowRect);
    }

    if (item.textTexture != nullptr) {
      SDL_FRect textRect = {item.rect.x + static_cast<float>(paddingX),
                            item.rect.y + static_cast<float>(paddingY),
                            static_cast<float>(item.textW),
                            static_cast<float>(item.textH)};
      SDL_RenderTexture(render, item.textTexture, nullptr, &textRect);
    }
  }

  return KOTONOHA_SCENE_DRAW_OVERLAYED;
}

void Prompt::Reset() {
  if (promptResult != nullptr) {
    *promptResult = -1;
  }
  ClearLayoutTextures();
}

} // namespace Kotonoha