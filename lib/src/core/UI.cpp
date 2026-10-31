#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace Kotonoha {

std::size_t utf8Prefix(const std::string &text, std::size_t characters) {
  std::size_t i = 0;
  while (i < text.size() && characters > 0) {
    ++i;
    while (i < text.size() &&
           (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
      ++i;
    --characters;
  }
  return i;
}

void eraseLastCharacter(std::string &text) {
  if (text.empty())
    return;
  std::size_t i = text.size() - 1;
  while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
    --i;
  text.erase(i);
}

UI::UI(const std::string &fontPath) {
  if (!TTF_Init())
    throw std::runtime_error(SDL_GetError());
  std::string path;
  try {
    if (!fontPath.empty()) {
      try {
        path = FileResolver::font(fontPath).string();
        font.reset(TTF_OpenFont(path.c_str(), 24));
      } catch (const std::exception &error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "UI custom font '%s': %s", fontPath.c_str(), error.what());
      }
    }
    if (!font) {
      path = FileResolver::font().string();
      font.reset(TTF_OpenFont(path.c_str(), 24));
    }
  } catch (...) {
    TTF_Quit();
    throw;
  }
  if (!font) {
    const std::string error = "Cannot open font " + path + ": " + SDL_GetError();
    TTF_Quit();
    throw std::runtime_error(error);
  }
}

UI::~UI() {
  cache.clear();
  font.reset();
  TTF_Quit();
}

void UI::setPixelDensity(float value) {
  if (!std::isfinite(value) || value <= 0)
    throw std::invalid_argument("UI pixel density must be positive and finite");
  if (density == value)
    return;
  if (!TTF_SetFontSize(font.get(), 24 * value))
    throw std::runtime_error(SDL_GetError());
  density = value;
}

SDL_FPoint UI::viewSize(RendererRef renderer) {
  int w = 0, h = 0;
  SDL_RendererLogicalPresentation presentation;
  if (!SDL_GetRenderLogicalPresentation(&renderer, &w, &h, &presentation))
    throw std::runtime_error(SDL_GetError());
  if (presentation == SDL_LOGICAL_PRESENTATION_DISABLED &&
      !SDL_GetRenderOutputSize(&renderer, &w, &h))
    throw std::runtime_error(SDL_GetError());
  return {static_cast<float>(w), static_cast<float>(h)};
}

float UI::text(RendererRef renderer, const std::string &value, float x, float y,
               float wrap, SDL_Color color) {
  if (value.empty())
    return 0;
  const int pixels = std::max(1, static_cast<int>(std::ceil(wrap * density)));
  const std::string key =
      std::to_string(density) + ":" + std::to_string(pixels) + ":" + value;
  auto found = cache.find(key);
  if (found == cache.end()) {
    if (cache.size() >= 512)
      cache.clear();
    SurfacePtr surface{TTF_RenderText_Blended_Wrapped(
        font.get(), value.c_str(), value.size(), {255, 255, 255, 255}, pixels)};
    if (!surface)
      throw std::runtime_error(SDL_GetError());
    TexturePtr texture{SDL_CreateTextureFromSurface(&renderer, surface.get())};
    if (!texture)
      throw std::runtime_error(SDL_GetError());
    found =
        cache
            .emplace(key, CachedText{std::move(texture),
                                     static_cast<float>(surface->w) / density,
                                     static_cast<float>(surface->h) / density})
            .first;
  }
  auto &entry = found->second;
  SDL_SetTextureColorMod(entry.texture.get(), color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(entry.texture.get(), color.a);
  const SDL_FRect destination{x, y, entry.width, entry.height};
  if (!SDL_RenderTexture(&renderer, entry.texture.get(), nullptr, &destination))
    throw std::runtime_error(SDL_GetError());
  return entry.height;
}

float UI::textHeight(RendererRef renderer, const std::string &value,
                     float wrap) {
  // Use the same cached layout as rendering, outside the visible viewport.
  return text(renderer, value, -10000, -10000, wrap);
}

SDL_FPoint UI::textSize(const std::string &value, float width) const {
  if (value.empty())
    return {0, 0};
  int w = 0, h = 0;
  if (!TTF_GetStringSizeWrapped(
          font.get(), value.c_str(), value.size(),
          std::max(1, static_cast<int>(std::ceil(width * density))), &w, &h))
    throw std::runtime_error(SDL_GetError());
  return {w / density, h / density};
}

void UI::panel(RendererRef renderer, SDL_FRect rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(&renderer, &rect);
}

void UI::button(RendererRef renderer, SDL_FRect rect, const std::string &label,
                bool selected) {
  panel(renderer, rect,
        selected ? SDL_Color{65, 75, 110, 255} : SDL_Color{36, 41, 60, 250});
  text(renderer, label, rect.x + 12, rect.y + 9, rect.w - 24);
}

SDL_FRect UI::centered(float w, float h) {
  return {(width - w) / 2, (height - h) / 2, w, h};
}

UI::Frame UI::fit(SDL_FPoint content, SDL_Point viewport) {
  const float scale = std::min(viewport.x / content.x, viewport.y / content.y);
  return {scale, (viewport.x - content.x * scale) / 2,
          (viewport.y - content.y * scale) / 2, content.x, content.y};
}

SDL_Event UI::toFrame(const SDL_Event &event, Frame frame) {
  if (frame.identity())
    return event;
  SDL_Event out = event;
  const auto map = [&](float &x, float &y) {
    x = (x - frame.x) / frame.scale;
    y = (y - frame.y) / frame.scale;
  };
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
    map(out.motion.x, out.motion.y);
    out.motion.xrel /= frame.scale;
    out.motion.yrel /= frame.scale;
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    map(out.button.x, out.button.y);
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    map(out.wheel.mouse_x, out.wheel.mouse_y);
    break;
  default:
    break;
  }
  return out;
}

// The viewport is expressed in scaled units: pixel = viewport * scale.
UI::FrameScope::FrameScope(RendererRef target, Frame frame) {
  if (frame.identity())
    return;
  renderer = &target;
  SDL_SetRenderScale(renderer, frame.scale, frame.scale);
  const SDL_Rect view{static_cast<int>(frame.x / frame.scale + 0.5f),
                      static_cast<int>(frame.y / frame.scale + 0.5f),
                      static_cast<int>(frame.width + 0.5f),
                      static_cast<int>(frame.height + 0.5f)};
  SDL_SetRenderViewport(renderer, &view);
}

UI::FrameScope::~FrameScope() {
  if (!renderer)
    return;
  SDL_SetRenderClipRect(renderer, nullptr);
  SDL_SetRenderViewport(renderer, nullptr);
  SDL_SetRenderScale(renderer, 1, 1);
}

bool UI::clicked(EventRef event, SDL_FRect rect) {
  if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event.button.button != SDL_BUTTON_LEFT)
    return false;
  const SDL_FPoint point{event.button.x, event.button.y};
  return SDL_PointInRectFloat(&point, &rect);
}

} // namespace Kotonoha
