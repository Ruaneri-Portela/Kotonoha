#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

// TTF glyphs are rasterized at the output DPI; image enhancement filters
// must never resample glyph textures or text surfaces.
inline constexpr char kTTFTextRasterProperty[] = "kotonoha.ttf.raster";

inline bool MarkTTFTextRaster(SDL_Surface &surface) {
  return SDL_SetBooleanProperty(SDL_GetSurfaceProperties(&surface),
                                kTTFTextRasterProperty, true);
}
inline bool MarkTTFTextRaster(SDL_Texture &texture) {
  return SDL_SetBooleanProperty(SDL_GetTextureProperties(&texture),
                                kTTFTextRasterProperty, true);
}
inline bool IsTTFTextRaster(const SDL_Surface &surface) {
  return SDL_GetBooleanProperty(
      SDL_GetSurfaceProperties(const_cast<SDL_Surface *>(&surface)),
      kTTFTextRasterProperty, false);
}
inline bool IsTTFTextRaster(const SDL_Texture &texture) {
  return SDL_GetBooleanProperty(
      SDL_GetTextureProperties(const_cast<SDL_Texture *>(&texture)),
      kTTFTextRasterProperty, false);
}

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

// Per-texture policy of a backend. MetalFX is a temporal spatial scaler that
// must see the fully composed frame: applying it to individual textures would
// rescale every sprite independently and break temporal reprojection. MPV
// (Anime4K/ArtCNN shader chains) and Linear copies are texture-local and safe.
// None performs no pipeline at all and Auto is resolved before drawing, so
// both report true here; query Upscaling::supportsPerTexture() for the
// effective policy of the active backend.
bool SupportsPerTexture(UpscaleBackend backend);

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
  // True for backends that may run on an individual texture. MetalFX returns
  // false because it must upscale the whole rendered frame once (see
  // UpscaleWholeFrameWithMetalFX in MetalFXUpscaler.hpp).
  bool supportsPerTexture() const { return SupportsPerTexture(backend()); }
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
  std::unordered_map<SDL_Texture *, std::weak_ptr<TextureCache>> textureCaches;
  void clearTextureCaches();

public:
  explicit Upscaling(RendererRef renderer);
  virtual ~Upscaling();
  Upscaling(const Upscaling &) = delete;
  Upscaling &operator=(const Upscaling &) = delete;
  static Upscaling *forRenderer(RendererRef renderer);
  bool supports(UpscaleBackend backend) const;
  // Concrete backend for `backend` on this renderer (Auto resolved).
  UpscaleBackend resolve(UpscaleBackend backend) const;
  // Backend actually processing frames: None while disabled (factor <= 1).
  UpscaleBackend activeBackend() const;
  // Policy of the active pipeline. MetalFX is whole-frame only, so texture
  // draws must use the plain SDL path while it is active. Virtual so hosts
  // managing their own whole-frame effect can force this behavior.
  virtual bool supportsPerTexture() const;
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
// texture. When the active backend does not support per-texture upscaling
// (MetalFX), the plain SDL path is used: the host applies MetalFX once to the
// composed frame at presentation instead (see MetalFXUpscaler.hpp).
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
