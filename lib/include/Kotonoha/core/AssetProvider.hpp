#pragma once

#include <Kotonoha/core/SDL.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kotonoha {
class Upscaling;

// Decoded PCM in the layout described by `spec`.
struct AudioClip {
  SDL_AudioSpec spec{};
  std::vector<Uint8> data;
};

// Source of every game asset the engine loads (pictures, sounds, media and
// data). Games replace the default file system provider to read archives,
// disc images or custom formats; overriding a decoder changes how an asset
// becomes an SDL object without changing its consumers.
class AssetProvider {
  std::unordered_map<std::string, TexturePtr> textures;
  SDL_Renderer *textureRenderer = nullptr;

public:
  AssetProvider();
  virtual ~AssetProvider();
  AssetProvider(const AssetProvider &) = delete;
  AssetProvider &operator=(const AssetProvider &) = delete;

  virtual std::string name() const = 0;
  virtual bool contains(const std::string &id) const = 0;
  // Raw bytes of an asset; throws when it does not exist.
  virtual std::vector<Uint8> read(const std::string &id) const = 0;
  // Identifiers under a prefix, when the provider can enumerate them.
  virtual std::vector<std::string> list(const std::string &prefix = {}) const;
  // A file streaming consumers may open directly; empty for in-memory assets.
  virtual std::string localPath(const std::string &id) const;
  // Default decoders: SDL first (BMP/PNG/JPG, WAV), then FFmpeg for every
  // other picture, video (first frame) or audio format it can probe.
  virtual SurfacePtr loadSurface(const std::string &id) const;
  virtual AudioClip loadAudio(const std::string &id) const;
  // The video frame shown at `seconds` (a picture for still images).
  virtual SurfacePtr loadFrame(const std::string &id, double seconds) const;

  // Texture of loadSurface(id), cached per renderer until clearCache().
  SDL_Texture &texture(RendererRef renderer, const std::string &id);
  // loadSurface(id) at `scale` x its size. With an upscaling pipeline whose
  // backend is enabled, scales above 1 go through it (Upscaling::scaled);
  // otherwise (or with nullptr) the picture is scaled linearly. Cached per
  // renderer, id and scale.
  SDL_Texture &texture(RendererRef renderer, const std::string &id,
                       float scale, Upscaling *upscaling);
  // CPU copy of the same scaled picture (RGBA32), not cached.
  SurfacePtr surface(RendererRef renderer, const std::string &id, float scale,
                     Upscaling *upscaling) const;
  std::size_t cachedTextures() const;
  void clearCache();
};

// Plain files. Without a root, identifiers are paths as given (relative to the
// working directory). With a root, identifiers are relative to it and may not
// escape it.
class FileSystemAssetProvider : public AssetProvider {
  std::filesystem::path root;
  std::filesystem::path resolve(const std::string &id) const;

public:
  explicit FileSystemAssetProvider(std::filesystem::path root = {});
  const std::filesystem::path &getRoot() const;
  std::string name() const override;
  bool contains(const std::string &id) const override;
  std::vector<Uint8> read(const std::string &id) const override;
  std::vector<std::string> list(const std::string &prefix = {}) const override;
  std::string localPath(const std::string &id) const override;
};

} // namespace Kotonoha
