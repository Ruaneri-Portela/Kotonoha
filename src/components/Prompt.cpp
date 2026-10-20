#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/utils/OverlayRender.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
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
               const char *clickAudioPath, const char *appearAudioPath)
    : prompt(std::move(prompt)), promptResult(promptResult), time(time),
      startTime(start), endTime(end), font(font),
      audioPath(clickAudioPath ? clickAudioPath : "assets/Se/click.ogg"),
      appearAudioPath(appearAudioPath ? appearAudioPath : "") {}

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

void Prompt::SetViewport(SDL_FRect renderViewport) {
  viewport = renderViewport;
}

bool Prompt::IsActive() const {
  if (time == nullptr || prompt.empty()) {
    return false;
  }
  bool inRange = false;
  Sint64 diff = 0;
  KotonohaTimeGetFromEvent(time, startTime, endTime, &inRange, &diff);
  return inRange;
}

void Prompt::HandleEvent(SDL_Event *event, struct Kotonoha_Game *gameContext,
                         Audio *audioManager) {
  if (event == nullptr || promptResult == nullptr || *promptResult != -1 ||
      lastLayout.empty() || !IsActive()) {
    return;
  }

  auto selectOption = [&](int originalIndex) {
    const auto option =
        std::find_if(lastLayout.begin(), lastLayout.end(),
                     [originalIndex](const PromptLayoutItem &item) {
                       return item.originalIndex == originalIndex;
                     });
    if (option == lastLayout.end()) {
      return false;
    }
    *promptResult = originalIndex;
    if (audioManager != nullptr && !audioPath.empty() && time != nullptr) {
      audioManager->AddMedia(audioPath.c_str(), 0, 0, false, "Se");
    }
    return true;
  };

  auto moveFocus = [&](int direction) {
    if (lastLayout.empty()) {
      return;
    }
    auto focused = std::find_if(lastLayout.begin(), lastLayout.end(),
                                [this](const PromptLayoutItem &item) {
                                  return item.originalIndex == focusedIndex;
                                });
    int index = focused == lastLayout.end()
                    ? 0
                    : static_cast<int>(focused - lastLayout.begin());
    const int count = static_cast<int>(lastLayout.size());
    index = (index + direction + count) % count;
    focusedIndex = lastLayout[static_cast<size_t>(index)].originalIndex;
    keyboardFocusActive = true;
  };

  if (event->type == SDL_EVENT_KEY_DOWN) {
    switch (event->key.key) {
    case SDLK_LEFT:
    case SDLK_UP:
      moveFocus(-1);
      return;
    case SDLK_RIGHT:
    case SDLK_DOWN:
      moveFocus(1);
      return;
    case SDLK_1:
      selectOption(lastLayout.front().originalIndex);
      return;
    case SDLK_2:
      if (lastLayout.size() > 1) {
        selectOption(lastLayout[1].originalIndex);
      }
      return;
    case SDLK_SPACE:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      selectOption(focusedIndex >= 0 ? focusedIndex
                                     : lastLayout.front().originalIndex);
      return;
    default:
      break;
    }
  } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (event->gbutton.button) {
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
      moveFocus(-1);
      return;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
      moveFocus(1);
      return;
    case SDL_GAMEPAD_BUTTON_SOUTH:
      selectOption(focusedIndex >= 0 ? focusedIndex
                                     : lastLayout.front().originalIndex);
      return;
    default:
      break;
    }
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
    SDL_GetWindowSizeInPixels(gameContext->window, &w, &h);
    rawX = event->tfinger.x * static_cast<float>(w);
    rawY = event->tfinger.y * static_cast<float>(h);
    isClick = true;
  }

  if (!isClick) {
    return;
  }

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && gameContext != nullptr &&
      gameContext->window != nullptr) {
    const float windowScale = SDL_GetWindowDisplayScale(gameContext->window);
    if (windowScale > 0.0f) {
      rawX *= windowScale;
      rawY *= windowScale;
    }
  }
  const float clickX = rawX - viewport.x;
  const float clickY = rawY - viewport.y;

  for (const auto &item : lastLayout) {
    const bool hit =
        clickX >= item.rect.x && clickX <= (item.rect.x + item.rect.w) &&
        clickY >= item.rect.y && clickY <= (item.rect.y + item.rect.h);

    if (hit) {
      selectOption(item.originalIndex);
      keyboardFocusActive = false;
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
    promptObj->appearSoundPlayed = false;
    return KOTONOHA_SCENE_NULL;
  }

  const std::vector<int> visibleIndices =
      BuildVisiblePromptIndices(promptObj->prompt);

  if (!promptObj->appearSoundPlayed && !visibleIndices.empty()) {
    promptObj->appearSoundPlayed = true;
    if (promptObj->audio != nullptr && !promptObj->appearAudioPath.empty()) {
      promptObj->audio->AddMedia(promptObj->appearAudioPath.c_str(), 0, 0,
                                 false, "Se");
    }
  }

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
      (windowScale > 0.0f ? rawMouseX * windowScale : rawMouseX) -
      promptObj->viewport.x;
  const float mouseY =
      (windowScale > 0.0f ? rawMouseY * windowScale : rawMouseY) -
      promptObj->viewport.y;

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
    const bool focused = promptObj->keyboardFocusActive &&
                         promptObj->focusedIndex == item.originalIndex;

    SDL_Color fg = clicking               ? SDL_Color{255, 140, 0, 255}
                   : selected             ? SDL_Color{255, 230, 120, 255}
                   : (hovered || focused) ? SDL_Color{180, 220, 255, 255}
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
  focusedIndex = -1;
  keyboardFocusActive = false;
  appearSoundPlayed = false;
  ClearLayoutTextures();
}

} // namespace Kotonoha