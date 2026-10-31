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
      preset < UpscalePreset::Anime4K || preset > UpscalePreset::ArtCNNLarge ||
      !std::isfinite(factor) || factor < 1 || factor > 4 ||
      shaderFiles.find('\0') != std::string::npos)
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

Upscaling::Upscaling(RendererRef value) : renderer(value) {
  if (activeUpscaling)
    throw std::logic_error(
        "The migrated native backends support one active Upscaler");
  activeUpscaling = this;
}
Upscaling::~Upscaling() {
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
  settings = value;
}

SDL_Texture &Upscaling::process(TextureRef texture, Uint64 serial) {
  return implementation ? implementation->process(texture, serial) : texture;
}

void Upscaling::clear() {
  if (implementation)
    implementation->clear();
}

TexturePtr Upscaling::scaled(TextureRef texture, float scale) {
  if (!std::isfinite(scale) || scale <= 0 || scale > 16)
    throw std::invalid_argument("Scale must be in (0, 16]");
  const int w = std::max(1, static_cast<int>(std::lround(texture.w * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(texture.h * scale)));
  const auto maximum = SDL_GetNumberProperty(
      SDL_GetRendererProperties(&renderer),
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
    } catch (...) {
      implementation->configure(active);
      implementation->clear();
      throw;
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
  SurfacePtr converted(SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGBA32));
  if (!converted)
    throw std::runtime_error(SDL_GetError());
  return converted;
}

} // namespace Kotonoha
