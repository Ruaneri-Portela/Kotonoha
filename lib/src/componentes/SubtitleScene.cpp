#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>
#include <ass/ass.h>
#include <cmath>
#include <cstdarg>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

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
} // namespace

struct SubtitleScene::State {
  std::unique_ptr<ASS_Library, decltype(&ass_library_done)> library{
      ass_library_init(), ass_library_done};
  std::unique_ptr<ASS_Renderer, decltype(&ass_renderer_done)> renderer{
      nullptr, ass_renderer_done};
  std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track{nullptr,
                                                              ass_free_track};
  TexturePtr glyph;
  std::optional<AssStyle> style;
  int glyphWidth = 0, glyphHeight = 0;
  std::string appearanceKey;

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
        << ',' << style << ",,0,0,0,," << literal(displayCueText(cue.text))
        << '\n';
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
  sourceCues.clear();
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
  const auto appearanceKey = appearance.vnFontPath + "|" +
                             std::to_string(appearance.vnTextColorOverride) +
                             "|" + std::to_string(c.r) + "," +
                             std::to_string(c.g) + "," + std::to_string(c.b) +
                             "," + std::to_string(c.a);
  if (state->appearanceKey != appearanceKey) {
    ASS_Style overrideStyle =
        state->track->n_styles ? state->track->styles[0] : ASS_Style{};
    int overrideBits = 0;
    std::string family;
    const auto font = FileResolver::font(appearance.vnFontPath.empty()
                                             ? appearance.fontPath
                                             : appearance.vnFontPath);
    ass_set_fonts(state->renderer.get(), font.string().c_str(), nullptr,
                  ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
    if (!appearance.vnFontPath.empty()) {
      auto bytes = FileResolver::loadFontMemory(appearance.vnFontPath);
      if (!bytes.empty())
        ass_add_font(state->library.get(), const_cast<char *>("kotonoha-vn"),
                     reinterpret_cast<char *>(bytes.data()), int(bytes.size()));
      family =
          engine.GetVNUI().fontFamily() ? engine.GetVNUI().fontFamily() : "";
      overrideStyle.FontName = family.data();
      overrideBits |= ASS_OVERRIDE_BIT_FONT_NAME;
    }
    if (appearance.vnTextColorOverride) {
      overrideStyle.PrimaryColour = (uint32_t(c.r) << 24) |
                                    (uint32_t(c.g) << 16) |
                                    (uint32_t(c.b) << 8) | (255 - c.a);
      overrideBits |= ASS_OVERRIDE_BIT_COLORS;
    }
    ass_set_selective_style_override(state->renderer.get(), &overrideStyle);
    ass_set_selective_style_override_enabled(state->renderer.get(),
                                             overrideBits);
    state->appearanceKey = appearanceKey;
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
    // The presentation rectangle is in output pixels, including Retina.
    sx *= area.w / logicalWidth;
    sy *= area.h / logicalHeight;
  }
  const int w = std::max(1, static_cast<int>(std::lround(width * sx)));
  const int h = std::max(1, static_cast<int>(std::lround(height * sy)));
  ass_set_frame_size(state->renderer.get(), w, h);
  int changed = 0;
  auto *image = ass_render_frame(state->renderer.get(), state->track.get(),
                                 std::llround(position * 1000), &changed);
  for (; image; image = image->next) {
    if (image->w <= 0 || image->h <= 0)
      continue;
    if (state->glyphWidth < image->w || state->glyphHeight < image->h) {
      state->glyphWidth = std::max(state->glyphWidth, image->w);
      state->glyphHeight = std::max(state->glyphHeight, image->h);
      state->glyph.reset(SDL_CreateTexture(
          &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
          state->glyphWidth, state->glyphHeight));
      if (!state->glyph ||
          !SDL_SetTextureBlendMode(state->glyph.get(), SDL_BLENDMODE_BLEND) ||
          !SDL_SetTextureScaleMode(state->glyph.get(), SDL_SCALEMODE_NEAREST))
        throw std::runtime_error(SDL_GetError());
    }
    const SDL_Rect region{0, 0, image->w, image->h};
    void *pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(state->glyph.get(), &region, &pixels, &pitch))
      throw std::runtime_error(SDL_GetError());
    const Uint8 alpha = 255 - (image->color & 0xff);
    for (int y = 0; y < image->h; ++y) {
      auto *row = static_cast<Uint8 *>(pixels) + y * pitch;
      for (int x = 0; x < image->w; ++x) {
        row[x * 4] = image->color >> 24;
        row[x * 4 + 1] = image->color >> 16;
        row[x * 4 + 2] = image->color >> 8;
        row[x * 4 + 3] = image->bitmap[y * image->stride + x] * alpha / 255;
      }
    }
    SDL_UnlockTexture(state->glyph.get());
    const SDL_FRect source{0, 0, float(image->w), float(image->h)};
    const SDL_FRect destination{
        image->dst_x * float(width) / w, image->dst_y * float(height) / h,
        image->w * float(width) / w, image->h * float(height) / h};
    // libass already rasterized at output density; bypass the image upscaler.
    if (!SDL_RenderTexture(&renderer, state->glyph.get(), &source,
                           &destination))
      throw std::runtime_error(SDL_GetError());
  }
}
} // namespace Kotonoha
