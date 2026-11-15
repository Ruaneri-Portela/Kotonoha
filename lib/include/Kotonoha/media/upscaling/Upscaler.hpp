#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <array>
#include <string>
#include <vector>

namespace Kotonoha {

// Auto picks MetalFX on Metal, MPV on OpenGL and Linear otherwise.
enum class UpscaleBackend { None, Linear, MPV, MetalFX, Auto };
enum class UpscalePreset {
  Anime4K = 0,
  Anime4KModeAHQ = Anime4K,
  ArtCNN = 1,
  ArtCNNDenoise = 2,
  ArtCNNLarge = 3,
  Anime4KModeBHQ = 4,
  Anime4KModeCHQ,
  Anime4KModeAAHQ,
  Anime4KModeBBHQ,
  Anime4KModeCAHQ,
  Anime4KModeAFast,
  Anime4KModeBFast,
  Anime4KModeCFast,
  Anime4KModeAAFast,
  Anime4KModeBBFast,
  Anime4KModeCAFast
};
const std::array<UpscalePreset, 15> &UpscalePresets();
const char *UpscalePresetName(UpscalePreset preset);
bool UpscalePresetIsAnime4K(UpscalePreset preset);
std::vector<std::string> UpscalePresetShaderFiles(UpscalePreset preset);

// factor is the on/off switch, as in Kotonoha: 1.0 disables upscaling (no
// backend resources, linear SDL scaling), anything above 1.0 (up to 4.0)
// enables the selected backend.
struct UpscaleSettings {
  UpscaleBackend backend = UpscaleBackend::Auto;
  UpscalePreset preset = UpscalePreset::Anime4K;
  float factor = 1;
  std::string shaderFiles; // Ordered newline-separated paths; legacy ':' lists
                           // accepted.
  bool enabled() const;
  void validate() const;
};

const char *UpscaleBackendName(UpscaleBackend backend);
std::vector<std::string> UpscaleShaderFiles(const std::string &files);
std::string UpscaleShaderFileList(const std::vector<std::string> &files);

// Shared texture allocation, conversion and frame caching for every backend.
class Upscaler {
  friend class Upscaling;
  UpscaleSettings settings;
  TexturePtr intermediate, output;
  SDL_Texture *source = nullptr;
  SDL_PropertiesID sourceIdentity = 0;
  Uint64 serial = 0, retryAt = 0;
  int width = 0, height = 0;
  bool cached = false;

protected:
  RendererRef renderer;
  virtual void configureBackend(const UpscaleSettings &value) = 0;
  virtual bool renderInto(TextureRef input, TextureRef output) = 0;
  virtual Uint64 retryDeadline() const;
  virtual void clearBackendCache();
  virtual SDL_PixelFormat workingFormat() const;

public:
  explicit Upscaler(RendererRef renderer);
  virtual ~Upscaler();
  Upscaler(const Upscaler &) = delete;
  Upscaler &operator=(const Upscaler &) = delete;
  virtual UpscaleBackend backend() const = 0;
  virtual bool isAvaliable() const = 0;
  void configure(const UpscaleSettings &value);
  const UpscaleSettings &getSettings() const;
  // Change contentSerial whenever pixels or texture modulation change.
  SDL_Texture &process(TextureRef texture, Uint64 contentSerial);
  void clear();
};

// Runtime selection of a concrete Upscaler. One active native pipeline/process.
class Upscaling {
  RendererRef renderer;
  UpscaleSettings settings;
  std::unique_ptr<Upscaler> implementation;
  Uint64 stillSerial = 0;
  struct TextureCache;
  std::vector<std::weak_ptr<TextureCache>> textureCaches;
  void clearTextureCaches();

public:
  explicit Upscaling(RendererRef renderer);
  ~Upscaling();
  Upscaling(const Upscaling &) = delete;
  Upscaling &operator=(const Upscaling &) = delete;
  static Upscaling *forRenderer(RendererRef renderer);
  bool supports(UpscaleBackend backend) const;
  // Concrete backend for `backend` on this renderer (Auto resolved).
  UpscaleBackend resolve(UpscaleBackend backend) const;
  // Backend actually processing frames: None while disabled (factor <= 1).
  UpscaleBackend activeBackend() const;
  void configure(const UpscaleSettings &value);
  const UpscaleSettings &getSettings() const;
  const Upscaler *getImplementation() const;
  SDL_Texture &process(TextureRef texture, Uint64 contentSerial);
  void clear();
  // One-shot copy of a still picture at `scale` x its size (rounded). Scales
  // above 1 go through the backend while upscaling is enabled (factor > 1;
  // capped at 4, the remainder linear); otherwise the copy is linear. The
  // per-frame cache of process() is invalidated, never reused.
  TexturePtr scaled(TextureRef texture, float scale);
  // scaled() for CPU pixels: returns an RGBA32 surface.
  SurfacePtr scaled(const SDL_Surface &surface, float scale);
};

// Draw a game image through the renderer’s configured engine pipeline.
// Increment contentSerial only when pixels change; zero denotes a static
// texture.
bool RenderUpscaledTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_FRect *source,
                           const SDL_FRect *destination,
                           Uint64 contentSerial = 0);

// The same pipeline and crop mapping, preserving the game's rotation/flip.
bool RenderUpscaledTextureRotated(SDL_Renderer *renderer, SDL_Texture *texture,
                                  const SDL_FRect *source,
                                  const SDL_FRect *destination, double angle,
                                  const SDL_FPoint *center = nullptr,
                                  SDL_FlipMode flip = SDL_FLIP_NONE,
                                  Uint64 contentSerial = 0);

} // namespace Kotonoha
