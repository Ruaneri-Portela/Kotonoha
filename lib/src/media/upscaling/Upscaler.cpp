#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>
#include <Kotonoha/media/upscaling/OpenGLUpscaler.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {
namespace {
Upscaling *activeUpscaling = nullptr;
void requireSDL(bool result) {
  if (!result)
    throw std::runtime_error(SDL_GetError());
}

void copy(RendererRef renderer, TextureRef input, TextureRef output) {
  SDL_Texture *target = SDL_GetRenderTarget(&renderer);
  SDL_BlendMode blend;
  Uint8 r, g, b, a;
  requireSDL(SDL_GetTextureBlendMode(&input, &blend));
  requireSDL(SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a));
  requireSDL(SDL_SetRenderTarget(&renderer, &output));
  const bool copied = SDL_SetTextureBlendMode(&input, SDL_BLENDMODE_NONE) &&
                      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 0) &&
                      SDL_RenderClear(&renderer) &&
                      SDL_RenderTexture(&renderer, &input, nullptr, nullptr);
  const std::string error = copied ? "" : SDL_GetError();
  const bool restoredBlend = SDL_SetTextureBlendMode(&input, blend);
  const bool restoredTarget = SDL_SetRenderTarget(&renderer, target);
  const bool restoredColor = SDL_SetRenderDrawColor(&renderer, r, g, b, a);
  requireSDL(restoredBlend && restoredTarget && restoredColor);
  if (!copied)
    throw std::runtime_error("Upscaler copy: " + error);
}

class LinearUpscaler final : public Upscaler {
  void configureBackend(const UpscaleSettings &) override {}
  bool renderInto(TextureRef input, TextureRef output) override {
    copy(renderer, input, output);
    return true;
  }

public:
  explicit LinearUpscaler(RendererRef value) : Upscaler(value) {}
  UpscaleBackend backend() const override { return UpscaleBackend::Linear; }
  bool isAvaliable() const override { return true; }
};
} // namespace

const std::array<UpscalePreset, 15> &UpscalePresets() {
  static constexpr std::array<UpscalePreset, 15> values{
      UpscalePreset::Anime4KModeAHQ,
      UpscalePreset::Anime4KModeBHQ,
      UpscalePreset::Anime4KModeCHQ,
      UpscalePreset::Anime4KModeAAHQ,
      UpscalePreset::Anime4KModeBBHQ,
      UpscalePreset::Anime4KModeCAHQ,
      UpscalePreset::Anime4KModeAFast,
      UpscalePreset::Anime4KModeBFast,
      UpscalePreset::Anime4KModeCFast,
      UpscalePreset::Anime4KModeAAFast,
      UpscalePreset::Anime4KModeBBFast,
      UpscalePreset::Anime4KModeCAFast,
      UpscalePreset::ArtCNN,
      UpscalePreset::ArtCNNDenoise,
      UpscalePreset::ArtCNNLarge};
  return values;
}
const char *UpscalePresetName(UpscalePreset preset) {
  switch (preset) {
  case UpscalePreset::Anime4KModeAHQ:
    return "Anime4K: Mode A (HQ)";
  case UpscalePreset::Anime4KModeBHQ:
    return "Anime4K: Mode B (HQ)";
  case UpscalePreset::Anime4KModeCHQ:
    return "Anime4K: Mode C (HQ)";
  case UpscalePreset::Anime4KModeAAHQ:
    return "Anime4K: Mode A+A (HQ)";
  case UpscalePreset::Anime4KModeBBHQ:
    return "Anime4K: Mode B+B (HQ)";
  case UpscalePreset::Anime4KModeCAHQ:
    return "Anime4K: Mode C+A (HQ)";
  case UpscalePreset::Anime4KModeAFast:
    return "Anime4K: Mode A (Fast)";
  case UpscalePreset::Anime4KModeBFast:
    return "Anime4K: Mode B (Fast)";
  case UpscalePreset::Anime4KModeCFast:
    return "Anime4K: Mode C (Fast)";
  case UpscalePreset::Anime4KModeAAFast:
    return "Anime4K: Mode A+A (Fast)";
  case UpscalePreset::Anime4KModeBBFast:
    return "Anime4K: Mode B+B (Fast)";
  case UpscalePreset::Anime4KModeCAFast:
    return "Anime4K: Mode C+A (Fast)";
  case UpscalePreset::ArtCNN:
    return "ArtCNN C4F16";
  case UpscalePreset::ArtCNNDenoise:
    return "ArtCNN C4F16 DS";
  case UpscalePreset::ArtCNNLarge:
    return "ArtCNN C4F32";
  }
  return "Unknown";
}
bool UpscalePresetIsAnime4K(UpscalePreset preset) {
  return preset == UpscalePreset::Anime4K ||
         preset >= UpscalePreset::Anime4KModeBHQ;
}
std::vector<std::string> UpscalePresetShaderFiles(UpscalePreset preset) {
  if (preset == UpscalePreset::ArtCNN)
    return {"ArtCNN_C4F16.glsl"};
  if (preset == UpscalePreset::ArtCNNDenoise)
    return {"ArtCNN_C4F16_DS.glsl"};
  if (preset == UpscalePreset::ArtCNNLarge)
    return {"ArtCNN_C4F32.glsl"};
  const auto &values = UpscalePresets();
  const auto found = std::find(values.begin(), values.end(), preset);
  if (found == values.end())
    throw std::invalid_argument("Unknown upscaler preset");
  const auto index = std::distance(values.begin(), found);
  const bool fast = index >= 6;
  const auto mode = index % 6;
  const std::string primary = fast ? "M" : "VL", secondary = fast ? "S" : "M";
  std::vector<std::string> files{"Anime4K_Clamp_Highlights.glsl"};
  if (mode == 2 || mode == 5)
    files.push_back("Anime4K_Upscale_Denoise_CNN_x2_" + primary + ".glsl");
  else {
    files.push_back(std::string("Anime4K_Restore_CNN_") +
                    ((mode == 1 || mode == 4) ? "Soft_" : "") + primary +
                    ".glsl");
    files.push_back("Anime4K_Upscale_CNN_x2_" + primary + ".glsl");
  }
  if (mode == 3)
    files.push_back("Anime4K_Restore_CNN_" + secondary + ".glsl");
  files.push_back("Anime4K_AutoDownscalePre_x2.glsl");
  files.push_back("Anime4K_AutoDownscalePre_x4.glsl");
  if (mode == 4)
    files.push_back("Anime4K_Restore_CNN_Soft_" + secondary + ".glsl");
  if (mode == 5)
    files.push_back("Anime4K_Restore_CNN_" + secondary + ".glsl");
  files.push_back("Anime4K_Upscale_CNN_x2_" + secondary + ".glsl");
  return files;
}

bool UpscaleSettings::enabled() const {
  return backend != UpscaleBackend::None && factor > 1;
}

const char *UpscaleBackendName(UpscaleBackend backend) {
  switch (backend) {
  case UpscaleBackend::None:
    return "None";
  case UpscaleBackend::Linear:
    return "Linear";
  case UpscaleBackend::MPV:
    return "MPV (OpenGL)";
  case UpscaleBackend::MetalFX:
    return "MetalFX";
  case UpscaleBackend::Auto:
    return "Auto";
  }
  return "Unknown";
}

std::vector<std::string> UpscaleShaderFiles(const std::string &files) {
  std::vector<std::string> result;
  const bool lines = files.find('\n') != std::string::npos;
  size_t start = 0;
  for (size_t i = 0; i <= files.size(); ++i) {
    const bool drive = i == start + 1 && files[i] == ':' &&
                       ((files[start] >= 'A' && files[start] <= 'Z') ||
                        (files[start] >= 'a' && files[start] <= 'z')) &&
                       i + 1 < files.size() &&
                       (files[i + 1] == '\\' || files[i + 1] == '/');
    if (i != files.size() && files[i] != '\n' &&
        (lines || files[i] != ':' || drive))
      continue;
    auto name = files.substr(start, i - start);
    const auto first = name.find_first_not_of(" \t\r");
    if (first != std::string::npos) {
      name = name.substr(first, name.find_last_not_of(" \t\r") - first + 1);
      result.push_back(std::move(name));
    }
    start = i + 1;
  }
  return result;
}

std::string UpscaleShaderFileList(const std::vector<std::string> &files) {
  std::string result;
  for (const auto &file : files) {
    if (file.empty() || file.find_first_of("\r\n") != std::string::npos ||
        file.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid shader file path");
    result += file + "\n";
  }
  return result;
}

void UpscaleSettings::validate() const {
  if (backend < UpscaleBackend::None || backend > UpscaleBackend::Auto ||
      preset < UpscalePreset::Anime4K ||
      preset > UpscalePreset::Anime4KModeCAFast || !std::isfinite(factor) ||
      factor < 1 || factor > 4 || shaderFiles.find('\0') != std::string::npos)
    throw std::invalid_argument("Invalid upscaler settings (factor: 1..4)");
}

Upscaler::Upscaler(RendererRef value) : renderer(value) {}
Upscaler::~Upscaler() = default;

Uint64 Upscaler::retryDeadline() const { return 0; }
void Upscaler::clearBackendCache() {}
SDL_PixelFormat Upscaler::workingFormat() const {
  return SDL_PIXELFORMAT_RGBA32;
}
const UpscaleSettings &Upscaler::getSettings() const { return settings; }
const UpscaleSettings &Upscaling::getSettings() const { return settings; }
const Upscaler *Upscaling::getImplementation() const {
  return implementation.get();
}

struct Upscaling::TextureCache {
  TexturePtr texture;
  Uint64 serial = 0, retryAt = 0;
  bool failed = false;
};

void Upscaling::clearTextureCaches() {
  for (auto &pair : textureCaches)
    if (auto entry = pair.second.lock()) {
      entry->texture.reset();
      entry->failed = false;
    }
  textureCaches.clear();
}

Upscaling::Upscaling(RendererRef value) : renderer(value) {
  if (activeUpscaling)
    throw std::logic_error(
        "The migrated native backends support one active Upscaler");
  activeUpscaling = this;
  SDL_SetPointerProperty(SDL_GetRendererProperties(&renderer),
                         "kotonoha.upscaling", this);
}
Upscaling *Upscaling::forRenderer(RendererRef renderer) {
  return static_cast<Upscaling *>(SDL_GetPointerProperty(
      SDL_GetRendererProperties(&renderer), "kotonoha.upscaling", nullptr));
}

Upscaling::~Upscaling() {
  clearTextureCaches();
  SDL_ClearProperty(SDL_GetRendererProperties(&renderer), "kotonoha.upscaling");
  implementation.reset();
  activeUpscaling = nullptr;
}

bool Upscaling::supports(UpscaleBackend backend) const {
  switch (backend) {
  case UpscaleBackend::None:
  case UpscaleBackend::Linear:
  case UpscaleBackend::Auto:
    return true;
  case UpscaleBackend::MPV:
    return OpenGLUpscaler::isAvaliable(renderer);
  case UpscaleBackend::MetalFX:
    return MetalFXUpscaler::isAvaliable(renderer);
  }
  return false;
}

UpscaleBackend Upscaling::resolve(UpscaleBackend backend) const {
  if (backend != UpscaleBackend::Auto)
    return backend;
  const char *name = SDL_GetRendererName(&renderer);
  const std::string driver = name ? name : "";
  if (driver == "metal" && supports(UpscaleBackend::MetalFX))
    return UpscaleBackend::MetalFX;
  if (driver.rfind("opengl", 0) == 0 && supports(UpscaleBackend::MPV))
    return UpscaleBackend::MPV;
  return UpscaleBackend::Linear;
}

UpscaleBackend Upscaling::activeBackend() const {
  return implementation ? implementation->backend() : UpscaleBackend::None;
}

void Upscaler::configure(const UpscaleSettings &value) {
  value.validate();
  if (!isAvaliable())
    throw std::runtime_error(
        "Upscaler is unavailable for the current renderer/device");
  if (value.backend != backend())
    throw std::invalid_argument("Settings do not match the concrete upscaler");
  if (settings.backend == value.backend && settings.preset == value.preset &&
      settings.factor == value.factor &&
      settings.shaderFiles == value.shaderFiles)
    return;
  configureBackend(value);
  clear();
  settings = value;
  SDL_LogInfo(LogUpscaler, "Upscaler backend=%d preset=%d factor=%.2f",
              static_cast<int>(value.backend), static_cast<int>(value.preset),
              value.factor);
}

void Upscaler::clear() {
  clearBackendCache();
  output.reset();
  intermediate.reset();
  source = nullptr;
  sourceIdentity = 0;
  cached = false;
  retryAt = 0;
  width = height = 0;
}

SDL_Texture &Upscaler::process(TextureRef texture, Uint64 contentSerial) {
  if (!settings.enabled())
    return texture;
  const auto identity = SDL_GetTextureProperties(&texture);
  if (!identity)
    throw std::runtime_error(SDL_GetError());
  const auto properties = SDL_GetRendererProperties(&renderer);
  const auto maximum = SDL_GetNumberProperty(
      properties, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
  const double w = std::round(texture.w * settings.factor);
  const double h = std::round(texture.h * settings.factor);
  if (w < 1 || h < 1 || w > 32768 || h > 32768 ||
      (maximum > 0 && (w > maximum || h > maximum)) ||
      (w * h + static_cast<double>(texture.w) * texture.h) * 4 >
          256 * 1024 * 1024)
    throw std::runtime_error(
        "Upscaler dimensions exceed texture/memory limits");
  const int nextWidth = static_cast<int>(w), nextHeight = static_cast<int>(h);
  if (!output || width != nextWidth || height != nextHeight || !intermediate ||
      intermediate->w != texture.w || intermediate->h != texture.h) {
    clear();
    const auto format = workingFormat();
    intermediate.reset(SDL_CreateTexture(
        &renderer, format, SDL_TEXTUREACCESS_TARGET, texture.w, texture.h));
    output.reset(SDL_CreateTexture(&renderer, format, SDL_TEXTUREACCESS_TARGET,
                                   nextWidth, nextHeight));
    if (!intermediate || !output)
      throw std::runtime_error(SDL_GetError());
    requireSDL(
        SDL_SetTextureScaleMode(intermediate.get(), SDL_SCALEMODE_LINEAR));
    requireSDL(SDL_SetTextureScaleMode(output.get(), SDL_SCALEMODE_LINEAR));
    width = nextWidth;
    height = nextHeight;
  }
  if (cached && source == &texture && sourceIdentity == identity &&
      serial == contentSerial && (!retryAt || SDL_GetTicks() < retryAt))
    return *output;
  cached = false;
  copy(renderer, texture, *intermediate);
  if (!renderInto(*intermediate, *output))
    throw std::runtime_error(std::string("Upscaler failed: ") + SDL_GetError());
  retryAt = retryDeadline();
  source = &texture;
  sourceIdentity = identity;
  serial = contentSerial;
  cached = true;
  return *output;
}

void Upscaling::configure(const UpscaleSettings &value) {
  value.validate();
  const bool changed = settings.backend != value.backend ||
                       settings.preset != value.preset ||
                       settings.factor != value.factor ||
                       settings.shaderFiles != value.shaderFiles;
  if (!supports(value.backend))
    throw std::invalid_argument(
        "Upscaler is unavailable for the current renderer");
  UpscaleSettings concrete = value;
  concrete.backend =
      value.enabled() ? resolve(value.backend) : UpscaleBackend::None;
  if (implementation && implementation->backend() == concrete.backend) {
    implementation->configure(concrete);
  } else {
    std::unique_ptr<Upscaler> next;
    switch (concrete.backend) {
    case UpscaleBackend::None:
    case UpscaleBackend::Auto:
      break;
    case UpscaleBackend::Linear:
      next = std::make_unique<LinearUpscaler>(renderer);
      break;
    case UpscaleBackend::MPV:
      next = std::make_unique<OpenGLUpscaler>(renderer);
      break;
    case UpscaleBackend::MetalFX:
      next = std::make_unique<MetalFXUpscaler>(renderer);
      break;
    }
    if (next)
      next->configure(concrete);
    implementation = std::move(next);
    SDL_LogInfo(LogUpscaler, "Upscaler %s -> %s (factor %.2f)",
                UpscaleBackendName(value.backend),
                UpscaleBackendName(concrete.backend), value.factor);
  }
  if (changed)
    clearTextureCaches();
  settings = value;
}

SDL_Texture &Upscaling::process(TextureRef texture, Uint64 serial) {
  if (!implementation)
    return texture;
  try {
    constexpr const char *key = "kotonoha.upscale-cache";
    using Owner = std::shared_ptr<TextureCache>;
    const auto properties = SDL_GetTextureProperties(&texture);
    auto *owner =
        static_cast<Owner *>(SDL_GetPointerProperty(properties, key, nullptr));
    if (!owner) {
      auto created = std::make_unique<Owner>(std::make_shared<TextureCache>());
      if (!SDL_SetPointerPropertyWithCleanup(
              properties, key, created.get(),
              [](void *, void *value) { delete static_cast<Owner *>(value); },
              nullptr))
        throw std::runtime_error(SDL_GetError());
      owner = created.release();
    }
    auto &entry = **owner;
    if (entry.failed && entry.serial == serial)
      return texture;
    if (entry.texture && entry.serial == serial &&
        (!entry.retryAt || SDL_GetTicks() < entry.retryAt))
      return *entry.texture;
    // Modulation belongs to drawing, rather than to the cached source pixels.
    Uint8 r, g, b, a;
    SDL_GetTextureColorMod(&texture, &r, &g, &b);
    SDL_GetTextureAlphaMod(&texture, &a);
    struct RestoreMod {
      SDL_Texture &texture;
      Uint8 r, g, b, a;
      ~RestoreMod() {
        SDL_SetTextureColorMod(&texture, r, g, b);
        SDL_SetTextureAlphaMod(&texture, a);
      }
    } restore{texture, r, g, b, a};
    SDL_SetTextureColorMod(&texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(&texture, 255);
    if (!entry.texture)
      textureCaches.emplace(&texture, *owner);
    auto &processed = implementation->process(texture, serial);
    if (!entry.texture || entry.texture->w != processed.w ||
        entry.texture->h != processed.h) {
      entry.texture.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET,
                                            processed.w, processed.h));
      if (!entry.texture)
        throw std::runtime_error(SDL_GetError());
      for (auto it = textureCaches.begin(); it != textureCaches.end();) {
        if (it->second.expired())
          it = textureCaches.erase(it);
        else
          ++it;
      }
    }
    copy(renderer, processed, *entry.texture);
    SDL_SetBooleanProperty(SDL_GetTextureProperties(entry.texture.get()),
                           "kotonoha.upscaled", true);
    entry.failed = false;
    entry.serial = serial;
    entry.retryAt = implementation->retryDeadline();
    return *entry.texture;
  } catch (const std::exception &error) {
    auto *owner = static_cast<std::shared_ptr<TextureCache> *>(
        SDL_GetPointerProperty(SDL_GetTextureProperties(&texture),
                               "kotonoha.upscale-cache", nullptr));
    if (owner) {
      (*owner)->texture.reset();
      (*owner)->serial = serial;
      (*owner)->failed = true;
    }
    SDL_LogWarn(LogUpscaler, "Cannot upscale texture; using original: %s",
                error.what());
    return texture;
  }
}
void Upscaling::clear() {
  clearTextureCaches();
  if (implementation)
    implementation->clear();
}

TexturePtr Upscaling::scaled(TextureRef texture, float scale) {
  if (!std::isfinite(scale) || scale <= 0 || scale > 16)
    throw std::invalid_argument("Scale must be in (0, 16]");
  const int w = std::max(1, static_cast<int>(std::lround(texture.w * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(texture.h * scale)));
  const auto maximum =
      SDL_GetNumberProperty(SDL_GetRendererProperties(&renderer),
                            SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
  if (maximum > 0 && (w > maximum || h > maximum))
    throw std::runtime_error("Scaled picture exceeds the texture size limit");
  TexturePtr result(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, w, h));
  if (!result)
    throw std::runtime_error(SDL_GetError());
  SDL_Texture *source = &texture;
  const bool upscale = implementation && scale > 1;
  const UpscaleSettings active =
      implementation ? implementation->getSettings() : UpscaleSettings{};
  if (upscale) {
    UpscaleSettings still = active;
    still.factor = std::min(scale, 4.0f);
    try {
      implementation->configure(still);
      source = &implementation->process(texture, ++stillSerial);
    } catch (const std::exception &error) {
      implementation->configure(active);
      implementation->clear();
      source = &texture;
      SDL_LogWarn(LogUpscaler,
                  "Cannot upscale picture; using original pixels: %s",
                  error.what());
    }
  }
  SDL_ScaleMode mode = SDL_SCALEMODE_LINEAR;
  requireSDL(SDL_GetTextureScaleMode(source, &mode));
  requireSDL(SDL_SetTextureScaleMode(source, SDL_SCALEMODE_LINEAR));
  try {
    copy(renderer, *source, *result);
  } catch (...) {
    SDL_SetTextureScaleMode(source, mode);
    if (upscale) {
      implementation->configure(active);
      implementation->clear();
    }
    throw;
  }
  requireSDL(SDL_SetTextureScaleMode(source, mode));
  if (upscale) {
    implementation->configure(active);
    implementation->clear();
  }
  requireSDL(SDL_SetTextureBlendMode(result.get(), SDL_BLENDMODE_BLEND));
  requireSDL(SDL_SetTextureScaleMode(result.get(), SDL_SCALEMODE_LINEAR));
  SDL_LogDebug(LogUpscaler, "Scaled still %dx%d -> %dx%d (%s)", texture.w,
               texture.h, w, h, upscale ? "upscaler" : "linear");
  if (upscale)
    SDL_SetBooleanProperty(SDL_GetTextureProperties(result.get()),
                           "kotonoha.upscaled", true);
  return result;
}

SurfacePtr Upscaling::scaled(const SDL_Surface &surface, float scale) {
  TexturePtr input(SDL_CreateTextureFromSurface(
      &renderer, const_cast<SDL_Surface *>(&surface)));
  if (!input)
    throw std::runtime_error(SDL_GetError());
  const TexturePtr output = scaled(*input, scale);
  SDL_Texture *target = SDL_GetRenderTarget(&renderer);
  requireSDL(SDL_SetRenderTarget(&renderer, output.get()));
  SurfacePtr pixels(SDL_RenderReadPixels(&renderer, nullptr));
  const std::string error = pixels ? "" : SDL_GetError();
  requireSDL(SDL_SetRenderTarget(&renderer, target));
  if (!pixels)
    throw std::runtime_error("Cannot read scaled pixels: " + error);
  if (pixels->format == SDL_PIXELFORMAT_RGBA32)
    return pixels;
  SurfacePtr converted(
      SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGBA32));
  if (!converted)
    throw std::runtime_error(SDL_GetError());
  return converted;
}

bool RenderUpscaledTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_FRect *source,
                           const SDL_FRect *destination, Uint64 contentSerial) {
  return RenderUpscaledTextureRotated(renderer, texture, source, destination, 0,
                                      nullptr, SDL_FLIP_NONE, contentSerial);
}
bool RenderUpscaledTextureRotated(SDL_Renderer *renderer, SDL_Texture *texture,
                                  const SDL_FRect *source,
                                  const SDL_FRect *destination, double angle,
                                  const SDL_FPoint *center, SDL_FlipMode flip,
                                  Uint64 contentSerial) {
  if (!renderer || !texture)
    return SDL_SetError("Missing renderer or texture");
  auto draw = [&](SDL_Texture *input, const SDL_FRect *crop) {
    // Keep the ordinary SDL copy path for atlas crops. Some renderers round
    // fractional source rectangles differently in the rotated-copy path.
    return angle == 0 && flip == SDL_FLIP_NONE && !center
               ? SDL_RenderTexture(renderer, input, crop, destination)
               : SDL_RenderTextureRotated(renderer, input, crop, destination,
                                          angle, center, flip);
  };
  auto *pipeline = Upscaling::forRenderer(*renderer);
  if (!pipeline || !pipeline->getSettings().enabled() ||
      SDL_GetBooleanProperty(SDL_GetTextureProperties(texture),
                             "kotonoha.upscaled", false))
    return draw(texture, source);
  auto &processed = pipeline->process(*texture, contentSerial);
  Uint8 r, g, b, a;
  SDL_GetTextureColorMod(texture, &r, &g, &b);
  SDL_GetTextureAlphaMod(texture, &a);
  SDL_SetTextureColorMod(&processed, r, g, b);
  SDL_SetTextureAlphaMod(&processed, a);
  SDL_BlendMode blend;
  if (!SDL_GetTextureBlendMode(texture, &blend) ||
      !SDL_SetTextureBlendMode(&processed, blend))
    return false;
  SDL_FRect crop{};
  if (source) {
    const float x = static_cast<float>(processed.w) / texture->w;
    const float y = static_cast<float>(processed.h) / texture->h;
    crop = {source->x * x, source->y * y, source->w * x, source->h * y};
  }
  return draw(&processed, source ? &crop : nullptr);
}

} // namespace Kotonoha
