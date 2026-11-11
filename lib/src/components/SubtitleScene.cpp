#include <Kotonoha/components/SubtitleScene.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>
#include <array>
#include <ass/ass.h>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Kotonoha {
namespace {
void message(int level, const char *format, va_list args, void *) {
  if (level <= 4)
    SDL_LogMessageV(LogMedia,
                    level <= 1   ? SDL_LOG_PRIORITY_ERROR
                    : level <= 2 ? SDL_LOG_PRIORITY_WARN
                                 : SDL_LOG_PRIORITY_DEBUG,
                    format, args);
}

std::string timestamp(double seconds) {
  const auto cs = static_cast<long long>(std::llround(seconds * 100));
  std::ostringstream result;
  result << cs / 360000 << ':' << std::setfill('0') << std::setw(2)
         << cs / 6000 % 60 << ':' << std::setw(2) << cs / 100 % 60 << '.'
         << std::setw(2) << cs % 100;
  return result.str();
}

std::string literal(const std::string &text) {
  std::string result;
  for (char ch : text) {
    if (ch == '\r')
      continue;
    if (ch == '\n')
      result += "\\N";
    else {
      if (ch == '{' || ch == '}' || ch == '\\')
        result += '\\';
      result += ch;
    }
  }
  return result;
}

std::string assEventText(const std::string &text) {
  std::string result;
  for (char ch : text) {
    if (ch == '\r')
      continue;
    if (ch == '\n')
      result += "\\N";
    else
      result += ch;
  }
  return result;
}
} // namespace

struct SubtitleScene::State {
  std::unique_ptr<ASS_Library, decltype(&ass_library_done)> library{
      ass_library_init(), ass_library_done};
  std::unique_ptr<ASS_Renderer, decltype(&ass_renderer_done)> renderer{
      nullptr, ass_renderer_done};
  std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track{nullptr,
                                                              ass_free_track};
  struct CachedGlyph {
    TexturePtr texture;
    SDL_FRect destination{};
    int bitmapWidth = 0, bitmapHeight = 0;
    uint32_t bitmapColor = 0;
    // Packed coverage bytes: enables exact comparison without trusting libass
    // pointers.
    std::vector<Uint8> coverage;
  };
  struct PooledTexture {
    TexturePtr texture;
    int width = 0, height = 0;
    size_t bytes = 0;
  };
  std::vector<CachedGlyph> glyphs;
  std::vector<PooledTexture> texturePool;
  size_t poolBytes = 0;
  static constexpr size_t maxPoolBytes = 16ull * 1024 * 1024;
  bool cacheValid = false;
  int cachedWidth = 0, cachedHeight = 0;
  SDL_Renderer *cachedRenderer = nullptr;
  int assWidth = 0, assHeight = 0;
  std::optional<AssStyle> style;
  std::string appearanceKey;
  std::unordered_map<uint32_t, std::array<Uint32, 256>> colorLuts;
  std::string registeredVNFont;

  void recycle(TexturePtr texture, int w, int h) {
    if (!texture)
      return;
    const size_t bytes = size_t(w) * size_t(h) * 4;
    if (bytes > maxPoolBytes || poolBytes > maxPoolBytes - bytes)
      return;
    poolBytes += bytes;
    texturePool.push_back({std::move(texture), w, h, bytes});
  }
  TexturePtr acquire(SDL_Renderer &renderer, int w, int h) {
    for (size_t i = texturePool.size(); i > 0; --i) {
      auto &entry = texturePool[i - 1];
      if (entry.width == w && entry.height == h) {
        TexturePtr result = std::move(entry.texture);
        poolBytes -= entry.bytes;
        texturePool.erase(texturePool.begin() + (i - 1));
        return result;
      }
    }
    TexturePtr result(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, w, h));
    if (!result ||
        !SDL_SetTextureBlendMode(result.get(), SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(result.get(), SDL_SCALEMODE_NEAREST))
      throw std::runtime_error(SDL_GetError());
    return result;
  }
  void invalidate(bool discardPool = false) {
    for (auto &glyph : glyphs)
      recycle(std::move(glyph.texture), glyph.bitmapWidth, glyph.bitmapHeight);
    glyphs.clear();
    cacheValid = false;
    if (discardPool) {
      texturePool.clear();
      poolBytes = 0;
    }
  }
  const std::array<Uint32, 256> &lut(uint32_t color) {
    auto it = colorLuts.find(color);
    if (it != colorLuts.end())
      return it->second;
    if (colorLuts.size() >= 64)
      colorLuts.clear();
    const auto *format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
    if (!format)
      throw std::runtime_error(SDL_GetError());
    std::array<Uint32, 256> values{};
    const Uint8 r = color >> 24, g = color >> 16, b = color >> 8;
    const unsigned alpha = 255 - (color & 0xff);
    for (unsigned i = 0; i < 256; ++i)
      values[i] = SDL_MapRGBA(format, nullptr, r, g, b, Uint8(i * alpha / 255));
    return colorLuts.emplace(color, std::move(values)).first->second;
  }

  State(Engine &engine) {
    if (!library)
      throw std::runtime_error("Cannot initialize libass");
    ass_set_message_cb(library.get(), message, nullptr);
    renderer.reset(ass_renderer_init(library.get()));
    if (!renderer)
      throw std::runtime_error("Cannot initialize libass renderer");
    // libass scans every file in ass_set_fonts_dir(), including OFL.txt.
    // Register only the font files accepted by the engine's font catalog.
    for (const auto &[name, bytes] : FileResolver::getFontMap())
      ass_add_font(library.get(), name.c_str(),
                   reinterpret_cast<const char *>(bytes.data()),
                   static_cast<int>(bytes.size()));

    try {
      auto data = FileResolver::loadFontMemory(engine.GetSettings().fontPath);
      if (!data.empty()) {
        ass_add_font(library.get(), const_cast<char *>("default"),
                     reinterpret_cast<char *>(data.data()),
                     static_cast<int>(data.size()));
        ass_add_font(library.get(), const_cast<char *>("Noto Sans JP"),
                     reinterpret_cast<char *>(data.data()),
                     static_cast<int>(data.size()));
      }
    } catch (...) {
    }

    std::filesystem::path font;
    try {
      font = FileResolver::font(engine.GetSettings().fontPath);
    } catch (const std::exception &) {
      // Uma fonte configurada que não é encontrada não deve impedir a
      // inicialização da cena de legendas; usamos a fonte padrão do motor.
    }
    ass_set_fonts(renderer.get(),
                  font.empty() ? nullptr : font.string().c_str(),
                  "Noto Sans JP", ASS_FONTPROVIDER_NONE, nullptr, 1);
  }
};

SubtitleScene::SubtitleScene(Engine &engine, int width, int height)
    : Scene(engine), width(width), height(height) {
  setLogicalSize(width, height);
  state = std::make_unique<State>(engine);
  state->style = engine.GetSubtitleStyle();
}

void SubtitleScene::setLogicalSize(int width, int height) {
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
    throw std::invalid_argument("Invalid subtitle scene dimensions");
  this->width = width;
  this->height = height;
}
SubtitleScene::~SubtitleScene() = default;

void SubtitleScene::loadAss(const std::string &script) {
  if (script.empty() || script.size() > 16 * 1024 * 1024)
    throw std::invalid_argument("ASS script must contain 1 byte to 16 MiB");
  std::string bytes = script;
  std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track(
      ass_read_memory(state->library.get(), bytes.data(), bytes.size(),
                      nullptr),
      ass_free_track);
  if (!track || track->n_styles == 0)
    throw std::runtime_error("Invalid ASS subtitle script");
  state->track = std::move(track);
  state->appearanceKey.clear();
  state->invalidate();
}

void SubtitleScene::setCues(const std::vector<SubtitleCue> &cues,
                            float fontSize) {
  if (!std::isfinite(fontSize) || fontSize < 8 || fontSize > 144)
    throw std::invalid_argument("Subtitle font size must be within 8..144");
  std::ostringstream ass;
  if (state->style)
    ass << state->style->header;
  else
    ass << "[Script Info]\nScriptType: v4.00+\nPlayResX: " << width
        << "\nPlayResY: " << height << "\nWrapStyle: 0\n"
        << "[V4+ Styles]\n"
        << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
           "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
           "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
           "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        << "Style: Default,Noto Sans JP," << fontSize
        << ",&H00FFF5F8,&H00FFFFFF,&H00201A2E,"
           "&H96000000,0,0,0,0,100,100,0,0,1,2.6,1.2,2,48,48,36,1\n";
  ass << "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
         "MarginV, Effect, Text\n";
  for (const auto &cue : cues)
    if (!std::isfinite(cue.start) || !std::isfinite(cue.end) || cue.start < 0 ||
        cue.end <= cue.start)
      throw std::invalid_argument("Invalid subtitle cue");
  auto timedCues = cues;
  if (replacesPreviousCue()) {
    std::stable_sort(
        timedCues.begin(), timedCues.end(),
        [](const auto &a, const auto &b) { return a.start < b.start; });
    for (size_t i = 1; i < timedCues.size(); ++i)
      timedCues[i - 1].end = std::min(timedCues[i - 1].end, timedCues[i].start);
  }
  for (const auto &cue : timedCues) {
    // Equal-time replacements leave only the last original line visible.
    if (replacesPreviousCue() && cue.end == cue.start)
      continue;
    if (!std::isfinite(cue.start) || !std::isfinite(cue.end) || cue.start < 0 ||
        cue.end <= cue.start || cue.end > 24 * 3600 ||
        cue.text.find('\0') != std::string::npos ||
        cue.speaker.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid subtitle cue");
    std::string style = "Default";
    if (state->style) {
      const auto &names = state->style->names;
      if (std::find(names.begin(), names.end(), cue.speaker) != names.end())
        style = cue.speaker;
      else if (std::find(names.begin(), names.end(), style) == names.end())
        style = names.front();
    }
    ass << "Dialogue: 0," << timestamp(cue.start) << ',' << timestamp(cue.end)
        << ',' << style << ",,0,0,0,,"
        << (cue.assText ? assEventText(cue.text) : literal(cue.text)) << '\n';
  }
  loadAss(ass.str());
  sourceCues = cues;
  cueFontSize = fontSize;
}

void SubtitleScene::translationChanged(const std::string &original,
                                       const std::string &previous,
                                       const std::string &translated) {
  auto cues = sourceCues;
  bool changed = false;
  for (auto &cue : cues) {
    if (cue.text == original || cue.text == previous) {
      cue.text = translated;
      changed = true;
    }
    if (cue.speaker == original || cue.speaker == previous) {
      cue.speaker = translated;
      changed = true;
    }
  }
  if (changed)
    setCues(cues, cueFontSize);
}
void SubtitleScene::clear() {
  state->track.reset();
  state->invalidate();
  sourceCues.clear();
}

std::vector<std::string> SubtitleScene::currentCueTexts() const {
  std::vector<std::string> texts;
  for (const auto &cue : sourceCues)
    if (cue.start <= position && position < cue.end)
      texts.push_back(cue.text);
  return texts;
}

std::vector<std::string> SubtitleScene::sourceCueTexts() const {
  std::vector<std::string> texts;
  texts.reserve(sourceCues.size());
  for (const auto &cue : sourceCues)
    texts.push_back(cue.text);
  return texts;
}

void SubtitleScene::setStyle(AssStyle style) {
  state->style = AssStyle::parse(style.header);
  clear();
}

void SubtitleScene::setPosition(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 24 * 3600)
    throw std::invalid_argument(
        "Subtitle position must be within 0..86400 seconds");
  position = seconds;
}

void SubtitleScene::render(RendererRef renderer, float) {
  if (!state->track)
    return;

  const auto &appearance = engine.GetSettings();
  const auto c = appearance.vnTextColor;
  const std::string fontSetting = appearance.vnFontPath.empty()
                                      ? appearance.fontPath
                                      : appearance.vnFontPath;
  const char *familyName = engine.GetVNUI().fontFamily();
  const std::string family = familyName ? familyName : "";
  const std::string key = fontSetting + "|" + family + "|" +
                          std::to_string(appearance.vnTextColorOverride) + "|" +
                          std::to_string(c.r) + "," + std::to_string(c.g) +
                          "," + std::to_string(c.b) + "," + std::to_string(c.a);

  if (state->appearanceKey != key) {
    ASS_Style overrideStyle =
        state->track->n_styles ? state->track->styles[0] : ASS_Style{};
    int bits = 0;
    std::string fontPath;
    try {
      fontPath = FileResolver::font(fontSetting).string();
    } catch (const std::exception &e) {
      SDL_LogWarn(LogMedia, "Subtitle font: %s", e.what());
    }
    ass_set_fonts(state->renderer.get(),
                  fontPath.empty() ? nullptr : fontPath.c_str(), nullptr,
                  ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
    if (!appearance.vnFontPath.empty() &&
        state->registeredVNFont != appearance.vnFontPath) {
      auto bytes = FileResolver::loadFontMemory(appearance.vnFontPath);
      if (!bytes.empty() &&
          bytes.size() <= size_t(std::numeric_limits<int>::max())) {
        ass_add_font(state->library.get(), const_cast<char *>("kotonoha-vn"),
                     reinterpret_cast<char *>(bytes.data()), int(bytes.size()));
        state->registeredVNFont = appearance.vnFontPath;
      }
    }
    if (!appearance.vnFontPath.empty()) {
      overrideStyle.FontName = const_cast<char *>(family.c_str());
      bits |= ASS_OVERRIDE_BIT_FONT_NAME;
    }
    if (appearance.vnTextColorOverride) {
      overrideStyle.PrimaryColour = (uint32_t(c.r) << 24) |
                                    (uint32_t(c.g) << 16) |
                                    (uint32_t(c.b) << 8) | (255 - c.a);
      bits |= ASS_OVERRIDE_BIT_COLORS;
    }
    ass_set_selective_style_override(state->renderer.get(), &overrideStyle);
    ass_set_selective_style_override_enabled(state->renderer.get(), bits);
    state->appearanceKey = key;
    state->invalidate();
  }

  float sx = 1, sy = 1;
  if (!SDL_GetRenderScale(&renderer, &sx, &sy))
    throw std::runtime_error(SDL_GetError());
  int logicalWidth = 0, logicalHeight = 0;
  SDL_RendererLogicalPresentation presentation;
  if (!SDL_GetRenderLogicalPresentation(&renderer, &logicalWidth,
                                        &logicalHeight, &presentation))
    throw std::runtime_error(SDL_GetError());
  if (presentation != SDL_LOGICAL_PRESENTATION_DISABLED) {
    SDL_FRect area{};
    if (!SDL_GetRenderLogicalPresentationRect(&renderer, &area))
      throw std::runtime_error(SDL_GetError());
    if (logicalWidth <= 0 || logicalHeight <= 0)
      throw std::runtime_error("Invalid logical presentation size");
    sx *= area.w / logicalWidth;
    sy *= area.h / logicalHeight;
  }
  if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0 || sy <= 0)
    throw std::runtime_error("Invalid subtitle scale");
  const int w = std::clamp(
      int(std::lround(std::clamp(double(width) * sx, 1.0, 8192.0))), 1, 8192);
  const int h = std::clamp(
      int(std::lround(std::clamp(double(height) * sy, 1.0, 8192.0))), 1, 8192);

  if (state->cachedRenderer != &renderer || state->cachedWidth != w ||
      state->cachedHeight != h) {
    state->invalidate(true);
    state->cachedRenderer = &renderer;
    state->cachedWidth = w;
    state->cachedHeight = h;
  }

  if (state->assWidth != w || state->assHeight != h) {
    ass_set_frame_size(state->renderer.get(), w, h);
    state->assWidth = w;
    state->assHeight = h;
    state->invalidate();
  }
  int changed = 0;
  ASS_Image *images =
      ass_render_frame(state->renderer.get(), state->track.get(),
                       std::llround(position * 1000), &changed);

  const auto destinationFor = [&](const ASS_Image *image) -> SDL_FRect {
    return {image->dst_x * float(width) / w, image->dst_y * float(height) / h,
            image->w * float(width) / w, image->h * float(height) / h};
  };

  // Exact coverage comparison: no reliance on ASS_Image bitmap pointer
  // identity.
  const auto matches = [](const State::CachedGlyph &glyph,
                          const ASS_Image *image) -> bool {
    if (!image->bitmap || glyph.bitmapWidth != image->w ||
        glyph.bitmapHeight != image->h || glyph.bitmapColor != image->color)
      return false;
    for (int y = 0; y < image->h; ++y) {
      if (std::memcmp(glyph.coverage.data() + size_t(y) * image->w,
                      image->bitmap + size_t(y) * image->stride,
                      size_t(image->w)) != 0)
        return false;
    }
    return true;
  };

  if (!state->cacheValid || changed != 0) {
    // Stage new entries while preserving the old textures for reuse.
    std::vector<State::CachedGlyph> previous = std::move(state->glyphs);
    std::vector<State::CachedGlyph> next;
    size_t totalPixels = 0;
    size_t index = 0;
    try {
      for (ASS_Image *image = images; image; image = image->next) {
        if (!image->bitmap || image->w <= 0 || image->h <= 0 ||
            image->w > 8192 || image->h > 8192 || image->stride < image->w)
          continue;
        const size_t pixels = size_t(image->w) * size_t(image->h);
        if (pixels > 32ull * 1024 * 1024 ||
            totalPixels > 64ull * 1024 * 1024 - pixels || next.size() >= 4096)
          break;
        totalPixels += pixels;
        State::CachedGlyph glyph;
        const bool same = index < previous.size() && previous[index].texture &&
                          matches(previous[index], image);
        if (same) {
          glyph = std::move(previous[index]);
        } else {
          // Reuse same-size texture first, then the bounded pool.
          if (index < previous.size() && previous[index].texture &&
              previous[index].bitmapWidth == image->w &&
              previous[index].bitmapHeight == image->h)
            glyph.texture = std::move(previous[index].texture);
          else
            glyph.texture = state->acquire(renderer, image->w, image->h);

          glyph.coverage.resize(pixels);
          const auto &lut = state->lut(image->color);
          void *pixelsOut = nullptr;
          int pitch = 0;
          if (!SDL_LockTexture(glyph.texture.get(), nullptr, &pixelsOut,
                               &pitch))
            throw std::runtime_error(SDL_GetError());
          for (int y = 0; y < image->h; ++y) {
            auto *row = static_cast<Uint8 *>(pixelsOut) + size_t(y) * pitch;
            const auto *src = image->bitmap + size_t(y) * image->stride;
            std::memcpy(glyph.coverage.data() + size_t(y) * image->w, src,
                        size_t(image->w));
            for (int x = 0; x < image->w; ++x) {
              const Uint32 pixel = lut[src[x]];
              std::memcpy(row + size_t(x) * 4, &pixel, sizeof(pixel));
            }
          }
          SDL_UnlockTexture(glyph.texture.get());
        }
        glyph.destination = destinationFor(image);
        glyph.bitmapWidth = image->w;
        glyph.bitmapHeight = image->h;
        glyph.bitmapColor = image->color;
        next.push_back(std::move(glyph));
        ++index;
      }
    } catch (...) {
      state->glyphs.clear();
      state->cacheValid = false;
      throw;
    }
    for (auto &glyph : previous)
      state->recycle(std::move(glyph.texture), glyph.bitmapWidth,
                     glyph.bitmapHeight);
    state->glyphs = std::move(next);
    state->cacheValid = true; // Empty frames are cached too.
  }

  for (const auto &glyph : state->glyphs) {
    if (!SDL_RenderTexture(&renderer, glyph.texture.get(), nullptr,
                           &glyph.destination))
      throw std::runtime_error(SDL_GetError());
  }
}
} // namespace Kotonoha
