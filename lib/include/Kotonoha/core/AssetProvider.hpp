#pragma once

#include <Kotonoha/core/AssetOverlay.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <filesystem>
#include <memory>
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

// Optional random-access byte source for assets that can be streamed without
// first materializing their complete contents. Implementations keep their own
// cursor and must support absolute seeks within [0, size()].
class AssetStream {
public:
  virtual ~AssetStream() = default;
  virtual std::uint64_t size() const = 0;
  virtual std::uint64_t tell() const = 0;
  virtual std::size_t read(Uint8 *buffer, std::size_t size) = 0;
  virtual bool seek(std::uint64_t position) = 0;
};

// Source of every game asset the engine loads (pictures, sounds, media and
// data). Games replace the default file system provider to read archives,
// disc images or custom formats; overriding a decoder changes how an asset
// becomes an SDL object without changing its consumers.
class AssetProvider {
  std::unordered_map<std::string, TexturePtr> textures;
  SDL_Renderer *textureRenderer = nullptr;
  AssetOverlay overlay;

public:
  AssetProvider();
  virtual ~AssetProvider();
  AssetProvider(const AssetProvider &) = delete;
  AssetProvider &operator=(const AssetProvider &) = delete;

  virtual std::string name() const = 0;
  void setOverlay(const std::filesystem::path &directory);
  const std::filesystem::path &overlayDirectory() const {
    return overlay.root();
  }
  std::string overlayPath(const std::string &id) const;

protected:
  virtual std::string overlayId(const std::string &id) const { return id; }
  virtual bool containsAsset(const std::string &) const = 0;
  virtual std::vector<Uint8> readAsset(const std::string &) const = 0;
  virtual std::vector<std::string> listAsset(const std::string &) const;
  virtual std::string localPathAsset(const std::string &) const;
  virtual SurfacePtr loadSurfaceAsset(const std::string &) const;
  virtual SurfacePtr loadFrameAsset(const std::string &, double) const;
  virtual AudioClip loadAudioAsset(const std::string &) const;

public:
  bool contains(const std::string &id) const;
  // Raw bytes of an asset; throws when it does not exist.
  std::vector<Uint8> read(const std::string &id) const;
  // Identifiers under a prefix, when the provider can enumerate them.
  std::vector<std::string> list(const std::string &prefix = {}) const;
  // A file streaming consumers may open directly; empty for in-memory assets.
  virtual std::string localPath(const std::string &id) const;
  // Optional seekable source for archived/disc-backed media. Returning null
  // asks consumers to fall back to read(id), preserving providers that only
  // expose complete in-memory assets.
  virtual std::unique_ptr<AssetStream> openStream(const std::string &id) const;
  // Default decoders: SDL first (BMP/PNG/JPG, WAV), then FFmpeg for every
  // other picture, video (first frame) or audio format it can probe.
  SurfacePtr loadSurface(const std::string &id) const;
  AudioClip loadAudio(const std::string &id) const;
  // The video frame shown at `seconds` (a picture for still images).
  SurfacePtr loadFrame(const std::string &id, double seconds) const;

  // Texture of loadSurface(id), cached per renderer until clearCache().
  SDL_Texture &texture(RendererRef renderer, const std::string &id);
  // Upload a picture decoded on a worker into this renderer's texture cache.
  // Must be called on the renderer thread.
  SDL_Texture &stageTexture(RendererRef renderer, const std::string &id,
                            SurfacePtr surface);
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
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;
  std::string localPathAsset(const std::string &id) const override;
};

// Loose legacy data with case-insensitive, indexed names. Uses the same
// normalization, collision and symlink policy as overlays, but mounts exactly
// the supplied data directory (no overlay-folder discovery).
class IndexedFileSystemAssetProvider : public FileSystemAssetProvider {
  AssetOverlay index;

public:
  explicit IndexedFileSystemAssetProvider(const std::filesystem::path &root);
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;
  std::string localPathAsset(const std::string &id) const override;
};

} // namespace Kotonoha
