#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <Kotonoha/localization/Translation.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace Kotonoha {

namespace {
std::atomic<std::uint64_t> nextUiInstance{1};
}

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

UI::UI(const std::string &fontPath)
    : instanceId_(nextUiInstance.fetch_add(1, std::memory_order_relaxed)) {
  try {
    fontData = FileResolver::loadFontMemory(fontPath);
    if (fontData.empty()) {
      fontData = FileResolver::loadFontMemory("");
    }
    if (!fontData.empty()) {
      Kotonoha::IOStreamPtr io{
          SDL_IOFromConstMem(fontData.data(), fontData.size())};
      if (io) {
        font.reset(TTF_OpenFontIO(io.release(), true, 24));
      }
    }
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "UI font memory load error: %s",
                error.what());
  }
  if (!font) {
    throw std::runtime_error(std::string("Cannot open font via memory: ") +
                             SDL_GetError());
  }
}

UI::~UI() {
  // Destroy glyph text before the corresponding fonts and atlas.
  cache.clear();
  rasterFonts.clear();
  textEngine.reset();
  font.reset();
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

float UI::rasterDensity(RendererRef renderer) {
  float x = 1, y = 1;
  SDL_GetRenderScale(&renderer, &x, &y);
  int w = 0, h = 0;
  SDL_RendererLogicalPresentation mode;
  SDL_GetRenderLogicalPresentation(&renderer, &w, &h, &mode);
  if (mode != SDL_LOGICAL_PRESENTATION_DISABLED && w > 0 && h > 0) {
    SDL_FRect area{};
    if (SDL_GetRenderLogicalPresentationRect(&renderer, &area)) {
      x *= area.w / w;
      y *= area.h / h;
    }
  }
  return std::max(1.0f, std::max(std::abs(x), std::abs(y)));
}

float UI::text(RendererRef renderer, const std::string &value, float x, float y,
               float wrap, SDL_Color color) {
  return textSized(renderer, value, x, y, wrap, 24, color);
}
float UI::textSized(RendererRef renderer, const std::string &original, float x,
                    float y, float wrap, float fontSize, SDL_Color color) {
  const auto value = translation ? translation->text(-1, original) : original;
  if (value.empty())
    return 0;
  const float raster = rasterDensity(renderer);
  const int pixels = std::max(1, static_cast<int>(std::ceil(wrap * raster)));
  const std::string key = std::to_string(fontSize) + ":" +
                          std::to_string(raster) + ":" +
                          std::to_string(pixels) + ":" + value;

  // SDL_ttf draws atlas glyphs through the SDL renderer. OpenGL/OpenGL ES
  // renderers therefore compose text on the GPU; other backends remain native.
  if (textRenderer != &renderer) {
    cache.clear(); // Drop SDL textures while the previous renderer still lives.
    rasterFonts.clear();
    textEngine.reset();
    textRenderer = &renderer;
    textEngine.reset(TTF_CreateRendererTextEngine(textRenderer));
    if (!textEngine)
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "SDL_ttf glyph atlas unavailable (%s); using surfaces",
                  SDL_GetError());
  }

  auto found = cache.find(key);
  if (found == cache.end()) {
    if (cache.size() >= 512)
      cache.clear();
    CachedText entry;
    if (textEngine) {
      // Dedicated fonts per size avoid invalidating every atlas glyph on
      // TTF_SetFontSize; cache the font as well as the shaped TTF_Text.
      const std::string sizeKey = std::to_string(fontSize * raster);
      auto sized = rasterFonts.find(sizeKey);
      if (sized == rasterFonts.end()) {
        if (rasterFonts.size() >= 64) {
          cache.clear(); // Text objects must die before their fonts.
          rasterFonts.clear();
          sized = rasterFonts.end();
        }
        IOStreamPtr io(SDL_IOFromConstMem(fontData.data(), fontData.size()));
        if (io) {
          FontPtr created(TTF_OpenFontIO(io.release(), true, fontSize * raster));
          if (created)
            sized = rasterFonts.emplace(sizeKey, std::move(created)).first;
        }
      }
      if (sized != rasterFonts.end()) {
        entry.glyphText.reset(TTF_CreateText(textEngine.get(), sized->second.get(),
                                            value.c_str(), value.size()));
        int width = 0, height = 0;
        if (entry.glyphText &&
            TTF_SetTextWrapWidth(entry.glyphText.get(), pixels) &&
            TTF_GetTextSize(entry.glyphText.get(), &width, &height)) {
          entry.width = static_cast<float>(width) / raster;
          entry.height = static_cast<float>(height) / raster;
        } else {
          entry.glyphText.reset();
        }
      }
    }
    if (!entry.glyphText) {
      // Compatibility fallback: rasterize the whole phrase once.
      const auto oldSize = TTF_GetFontSize(font.get());
      const float targetSize = fontSize * raster;
      const bool sizeChanged = std::abs(targetSize - oldSize) > 0.01f;
      struct RestoreSize {
        TTF_Font *font;
        float size;
        bool restore;
        ~RestoreSize() {
          if (restore)
            TTF_SetFontSize(font, size);
        }
      } restore{font.get(), oldSize, sizeChanged};
      if (sizeChanged && !TTF_SetFontSize(font.get(), targetSize))
        throw std::runtime_error(SDL_GetError());
      SurfacePtr surface{TTF_RenderText_Blended_Wrapped(
          font.get(), value.c_str(), value.size(), {255, 255, 255, 255},
          pixels)};
      if (!surface || !MarkTTFTextRaster(*surface))
        throw std::runtime_error(SDL_GetError());
      entry.texture.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
      if (!entry.texture || !MarkTTFTextRaster(*entry.texture))
        throw std::runtime_error(SDL_GetError());
      entry.width = static_cast<float>(surface->w) / raster;
      entry.height = static_cast<float>(surface->h) / raster;
    }
    found = cache.emplace(key, std::move(entry)).first;
  }

  auto &entry = found->second;
  if (entry.glyphText) {
    if (entry.tint.r != color.r || entry.tint.g != color.g ||
        entry.tint.b != color.b || entry.tint.a != color.a) {
      if (!TTF_SetTextColor(entry.glyphText.get(), color.r, color.g, color.b,
                            color.a))
        throw std::runtime_error(SDL_GetError());
      entry.tint = color;
    }
    if (std::abs(raster - 1.0f) < 1e-4f) {
      if (!TTF_DrawRendererText(entry.glyphText.get(), x, y))
        throw std::runtime_error(SDL_GetError());
    } else {
      // SDL3 scales both the active viewport and clip rect by the render scale.
      // Scale them by `raster` alongside dividing render scale by `raster` so
      // HiDPI atlas glyphs render at 1:1 physical pixels without shifting or
      // clipping text inside FrameScope or clipped components (e.g. VNTextBox).
      float scaleX = 1.0f, scaleY = 1.0f;
      if (!SDL_GetRenderScale(&renderer, &scaleX, &scaleY))
        throw std::runtime_error(SDL_GetError());
      const bool viewportSet = SDL_RenderViewportSet(&renderer);
      const bool clipEnabled = SDL_RenderClipEnabled(&renderer);
      SDL_Rect viewport{}, clip{};
      if (viewportSet && !SDL_GetRenderViewport(&renderer, &viewport))
        throw std::runtime_error(SDL_GetError());
      if (clipEnabled && !SDL_GetRenderClipRect(&renderer, &clip))
        throw std::runtime_error(SDL_GetError());

      struct RestoreRenderState {
        SDL_Renderer *r;
        float sx, sy;
        bool hadViewport, hadClip;
        SDL_Rect vp, cl;
        bool &restored;
        ~RestoreRenderState() {
          if (!SDL_SetRenderScale(r, sx, sy))
            restored = false;
          if (hadViewport && !SDL_SetRenderViewport(r, &vp))
            restored = false;
          if (hadClip && !SDL_SetRenderClipRect(r, &cl))
            restored = false;
        }
      };

      bool drawn = false;
      bool restored = true;
      {
        RestoreRenderState guard{&renderer, scaleX,   scaleY, viewportSet,
                                 clipEnabled, viewport, clip,   restored};
        float drawX = x * raster;
        float drawY = y * raster;
        if (!SDL_SetRenderScale(&renderer, scaleX / raster, scaleY / raster))
          throw std::runtime_error(SDL_GetError());
        if (viewportSet) {
          const SDL_Rect scaledViewport{
              static_cast<int>(std::lround(viewport.x * raster)),
              static_cast<int>(std::lround(viewport.y * raster)),
              static_cast<int>(std::ceil(viewport.w * raster)),
              static_cast<int>(std::ceil(viewport.h * raster))};
          drawX += viewport.x * raster - static_cast<float>(scaledViewport.x);
          drawY += viewport.y * raster - static_cast<float>(scaledViewport.y);
          if (!SDL_SetRenderViewport(&renderer, &scaledViewport))
            throw std::runtime_error(SDL_GetError());
        }
        if (clipEnabled) {
          const int clipX = static_cast<int>(std::floor(clip.x * raster));
          const int clipY = static_cast<int>(std::floor(clip.y * raster));
          const int clipRight =
              static_cast<int>(std::ceil((clip.x + clip.w) * raster));
          const int clipBottom =
              static_cast<int>(std::ceil((clip.y + clip.h) * raster));
          const SDL_Rect scaledClip{clipX, clipY,
                                    std::max(0, clipRight - clipX),
                                    std::max(0, clipBottom - clipY)};
          if (!SDL_SetRenderClipRect(&renderer, &scaledClip))
            throw std::runtime_error(SDL_GetError());
        }
        drawn = TTF_DrawRendererText(entry.glyphText.get(), drawX, drawY);
      }
      if (!drawn || !restored)
        throw std::runtime_error(SDL_GetError());
    }
  } else {
    SDL_SetTextureColorMod(entry.texture.get(), color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(entry.texture.get(), color.a);
    const SDL_FRect destination{x, y, entry.width, entry.height};
    if (!SDL_RenderTexture(&renderer, entry.texture.get(), nullptr,
                           &destination))
      throw std::runtime_error(SDL_GetError());
  }
  return entry.height;
}

float UI::textHeight(RendererRef renderer, const std::string &original,
                     float wrap) {
  // Measure text on the CPU. Rendering it off-screen created GPU textures for
  // every historical dialogue line and caused cache churn while scrolling.
  const auto value = translation ? translation->text(-1, original) : original;
  if (value.empty())
    return 0.0f;
  const float raster = rasterDensity(renderer);
  const int pixels = std::max(1, static_cast<int>(std::ceil(wrap * raster)));
  const float oldSize = TTF_GetFontSize(font.get());
  const float targetSize = 24.0f * raster;
  const bool sizeChanged = std::abs(targetSize - oldSize) > 0.01f;
  struct RestoreSize {
    TTF_Font *font;
    float size;
    bool restore;
    ~RestoreSize() {
      if (restore)
        TTF_SetFontSize(font, size);
    }
  } restore{font.get(), oldSize, sizeChanged};
  if (sizeChanged && !TTF_SetFontSize(font.get(), targetSize))
    throw std::runtime_error(SDL_GetError());
  int width = 0, height = 0;
  if (!TTF_GetStringSizeWrapped(font.get(), value.c_str(), value.size(), pixels,
                                &width, &height))
    throw std::runtime_error(SDL_GetError());
  return static_cast<float>(height) / raster;
}

SDL_FPoint UI::textSize(const std::string &value, float width) const {
  return measureSized(value, width, 24);
}
float UI::textWidth(const std::string &original, float fontSize) const {
  const auto value = translation ? translation->text(-1, original) : original;
  if (value.empty())
    return 0.0f;
  const float oldSize = TTF_GetFontSize(font.get());
  const float targetSize = fontSize * density;
  const bool sizeChanged = std::abs(targetSize - oldSize) > 0.01f;
  struct RestoreSize {
    TTF_Font *font;
    float size;
    bool restore;
    ~RestoreSize() {
      if (restore)
        TTF_SetFontSize(font, size);
    }
  } restore{font.get(), oldSize, sizeChanged};
  if (sizeChanged && !TTF_SetFontSize(font.get(), targetSize))
    throw std::runtime_error(SDL_GetError());
  int width = 0, height = 0;
  if (!TTF_GetStringSize(font.get(), value.c_str(), value.size(), &width,
                         &height))
    throw std::runtime_error(SDL_GetError());
  return static_cast<float>(width) / density;
}

SDL_FPoint UI::measureSized(const std::string &original, float width,
                            float fontSize) const {
  const auto value = translation ? translation->text(-1, original) : original;
  const float oldSize = TTF_GetFontSize(font.get());
  const float targetSize = fontSize * density;
  const bool sizeChanged = std::abs(targetSize - oldSize) > 0.01f;
  struct RestoreSize {
    TTF_Font *font;
    float size;
    bool restore;
    ~RestoreSize() {
      if (restore)
        TTF_SetFontSize(font, size);
    }
  } restore{font.get(), oldSize, sizeChanged};
  if (sizeChanged && !TTF_SetFontSize(font.get(), targetSize))
    throw std::runtime_error(SDL_GetError());
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

void UI::backdrop(RendererRef renderer, SDL_Color color) {
  float scaleX = 1, scaleY = 1;
  SDL_Rect viewport{}, clip{};
  const bool viewportSet = SDL_RenderViewportSet(&renderer);
  const bool clipEnabled = SDL_RenderClipEnabled(&renderer);
  SDL_GetRenderScale(&renderer, &scaleX, &scaleY);
  SDL_GetRenderViewport(&renderer, &viewport);
  SDL_GetRenderClipRect(&renderer, &clip);

  SDL_SetRenderClipRect(&renderer, nullptr);
  SDL_SetRenderViewport(&renderer, nullptr);
  SDL_SetRenderScale(&renderer, 1, 1);
  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(&renderer, nullptr);

  SDL_SetRenderScale(&renderer, scaleX, scaleY);
  SDL_SetRenderViewport(&renderer, viewportSet ? &viewport : nullptr);
  SDL_SetRenderClipRect(&renderer, clipEnabled ? &clip : nullptr);
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
  if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.type != SDL_EVENT_MOUSE_BUTTON_UP)
    return false;
  const auto activation = event.button.which == SDL_TOUCH_MOUSEID
                              ? SDL_EVENT_MOUSE_BUTTON_UP
                              : SDL_EVENT_MOUSE_BUTTON_DOWN;
  if (event.type != activation || event.button.button != SDL_BUTTON_LEFT)
    return false;
  const SDL_FPoint point{event.button.x, event.button.y};
  return SDL_PointInRectFloat(&point, &rect);
}

} // namespace Kotonoha
