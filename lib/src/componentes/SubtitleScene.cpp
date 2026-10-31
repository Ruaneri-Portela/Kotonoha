#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Log.hpp>

#include <ass/ass.h>
#include <algorithm>
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
    SDL_LogMessageV(LogMedia, level <= 1 ? SDL_LOG_PRIORITY_ERROR :
                   level <= 2 ? SDL_LOG_PRIORITY_WARN : SDL_LOG_PRIORITY_DEBUG,
                   format, args);
}

std::string timestamp(double seconds) {
  const auto cs = static_cast<long long>(std::llround(seconds * 100));
  std::ostringstream result;
  result << cs / 360000 << ':' << std::setfill('0') << std::setw(2)
         << cs / 6000 % 60 << ':' << std::setw(2) << cs / 100 % 60
         << '.' << std::setw(2) << cs % 100;
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
  std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track{nullptr, ass_free_track};
  TexturePtr glyph;
  std::optional<AssStyle> style;
  int glyphWidth = 0, glyphHeight = 0;

  State() {
    if (!library)
      throw std::runtime_error("Cannot initialize libass");
    ass_set_message_cb(library.get(), message, nullptr);
    renderer.reset(ass_renderer_init(library.get()));
    if (!renderer)
      throw std::runtime_error("Cannot initialize libass renderer");
    // Only engine fonts (assets/fonts): no system font provider.
    const auto font = FileResolver::font();
    ass_set_fonts_dir(library.get(), FileResolver::fontDirectory().string().c_str());
    ass_set_fonts(renderer.get(), font.string().c_str(), "Noto Sans JP",
                  ASS_FONTPROVIDER_NONE, nullptr, 1);
  }
};

SubtitleScene::SubtitleScene(Engine &engine, int width, int height)
    : Scene(engine), width(width), height(height) {
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
    throw std::invalid_argument("Invalid subtitle scene dimensions");
  state = std::make_unique<State>();
  state->style = engine.GetSubtitleStyle();
}
SubtitleScene::~SubtitleScene() = default;

void SubtitleScene::loadAss(const std::string &script) {
  if (script.empty() || script.size() > 16 * 1024 * 1024)
    throw std::invalid_argument("ASS script must contain 1 byte to 16 MiB");
  std::string bytes = script;
  std::unique_ptr<ASS_Track, decltype(&ass_free_track)> track(
      ass_read_memory(state->library.get(), bytes.data(), bytes.size(), nullptr),
      ass_free_track);
  if (!track || track->n_styles == 0)
    throw std::runtime_error("Invalid ASS subtitle script");
  state->track = std::move(track);
}

void SubtitleScene::setCues(const std::vector<SubtitleCue> &cues, float fontSize) {
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
  for (const auto &cue : cues) {
    if (!std::isfinite(cue.start) || !std::isfinite(cue.end) ||
        cue.start < 0 || cue.end <= cue.start || cue.end > 24 * 3600 ||
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
        << ',' << style << ",,0,0,0,," << literal(cue.text)
        << '\n';
  }
  loadAss(ass.str());
}

void SubtitleScene::clear() { state->track.reset(); }

void SubtitleScene::setStyle(AssStyle style) {
  state->style = AssStyle::parse(style.header);
  clear();
}

void SubtitleScene::setPosition(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 24 * 3600)
    throw std::invalid_argument("Subtitle position must be within 0..86400 seconds");
  position = seconds;
}

void SubtitleScene::render(RendererRef renderer, float) {
  if (!state->track)
    return;
  float sx = 1, sy = 1;
  if (!SDL_GetRenderScale(&renderer, &sx, &sy))
    throw std::runtime_error(SDL_GetError());
  const int w = std::clamp(static_cast<int>(std::lround(width * sx)), 1, 8192);
  const int h = std::clamp(static_cast<int>(std::lround(height * sy)), 1, 8192);
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
      state->glyph.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
          SDL_TEXTUREACCESS_STREAMING, state->glyphWidth, state->glyphHeight));
      if (!state->glyph ||
          !SDL_SetTextureBlendMode(state->glyph.get(), SDL_BLENDMODE_BLEND))
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
    const SDL_FRect destination{image->dst_x * float(width) / w,
        image->dst_y * float(height) / h, image->w * float(width) / w,
        image->h * float(height) / h};
    if (!SDL_RenderTexture(&renderer, state->glyph.get(), &source, &destination))
      throw std::runtime_error(SDL_GetError());
  }
}
} // namespace Kotonoha
