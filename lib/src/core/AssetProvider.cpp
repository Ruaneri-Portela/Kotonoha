#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <cmath>

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace Kotonoha {
namespace {
constexpr std::uintmax_t maximumAssetSize = 1024ull * 1024 * 1024;

SDL_IOStream *memoryStream(const std::vector<Uint8> &bytes) {
  SDL_IOStream *stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
  if (!stream)
    throw std::runtime_error(SDL_GetError());
  return stream;
}
} // namespace

AssetProvider::AssetProvider() = default;
AssetProvider::~AssetProvider() = default;

std::vector<std::string> AssetProvider::list(const std::string &) const {
  return {};
}

std::string AssetProvider::localPath(const std::string &) const { return {}; }

SurfacePtr AssetProvider::loadSurface(const std::string &id) const {
  const auto bytes = read(id);
  if (SurfacePtr surface{SDL_LoadSurface_IO(memoryStream(bytes), true)})
    return surface;
  const std::string sdlError = SDL_GetError();
  try {
    return MediaDecoder::decodePicture(bytes);
  } catch (const std::exception &error) {
    throw std::runtime_error("Cannot decode picture " + id + ": " + sdlError +
                             "; FFmpeg: " + error.what());
  }
}

SurfacePtr AssetProvider::loadFrame(const std::string &id,
                                    double seconds) const {
  if (seconds <= 0)
    return loadSurface(id);
  try {
    return MediaDecoder::decodePicture(read(id), seconds);
  } catch (const std::exception &error) {
    throw std::runtime_error("Cannot decode frame of " + id + ": " +
                             error.what());
  }
}

AudioClip AssetProvider::loadAudio(const std::string &id) const {
  const auto bytes = read(id);
  AudioClip clip;
  Uint8 *data = nullptr;
  Uint32 length = 0;
  if (SDL_LoadWAV_IO(memoryStream(bytes), true, &clip.spec, &data, &length)) {
    clip.data.assign(data, data + length);
    SDL_free(data);
    return clip;
  }
  const std::string sdlError = SDL_GetError();
  try {
    return MediaDecoder::decodeAudio(bytes);
  } catch (const std::exception &error) {
    throw std::runtime_error("Cannot decode audio " + id + ": " + sdlError +
                             "; FFmpeg: " + error.what());
  }
}

SDL_Texture &AssetProvider::texture(RendererRef renderer,
                                    const std::string &id) {
  if (textureRenderer != &renderer) {
    textures.clear();
    textureRenderer = &renderer;
  }
  if (auto found = textures.find(id); found != textures.end())
    return *found->second;
  const SurfacePtr surface = loadSurface(id);
  TexturePtr created(SDL_CreateTextureFromSurface(&renderer, surface.get()));
  if (!created)
    throw std::runtime_error("Cannot upload picture " + id + ": " +
                             SDL_GetError());
  SDL_Texture &result = *created;
  textures.emplace(id, std::move(created));
  return result;
}

SDL_Texture &AssetProvider::texture(RendererRef renderer, const std::string &id,
                                    float scale, Upscaling *upscaling) {
  if (!std::isfinite(scale) || scale <= 0)
    throw std::invalid_argument("Texture scale must be positive");
  if (scale == 1)
    return texture(renderer, id);
  if (textureRenderer != &renderer) {
    textures.clear();
    textureRenderer = &renderer;
  }
  const std::string key = id + "@" + std::to_string(scale);
  if (auto found = textures.find(key); found != textures.end())
    return *found->second;
  TexturePtr created;
  if (upscaling) {
    created = upscaling->scaled(texture(renderer, id), scale);
  } else {
    const SurfacePtr source = loadSurface(id);
    SurfacePtr resized(SDL_ScaleSurface(
        source.get(), std::max(1, int(std::lround(source->w * scale))),
        std::max(1, int(std::lround(source->h * scale))),
        SDL_SCALEMODE_LINEAR));
    if (!resized)
      throw std::runtime_error("Cannot scale picture " + id + ": " +
                               SDL_GetError());
    created.reset(SDL_CreateTextureFromSurface(&renderer, resized.get()));
  }
  if (!created)
    throw std::runtime_error("Cannot upload picture " + id + ": " +
                             SDL_GetError());
  SDL_Texture &result = *created;
  textures.emplace(key, std::move(created));
  return result;
}

SurfacePtr AssetProvider::surface(RendererRef renderer, const std::string &id,
                                  float scale, Upscaling *upscaling) const {
  if (!std::isfinite(scale) || scale <= 0)
    throw std::invalid_argument("Surface scale must be positive");
  const SurfacePtr source = loadSurface(id);
  if (upscaling)
    return upscaling->scaled(*source, scale);
  (void)renderer;
  SurfacePtr resized(SDL_ScaleSurface(
      source.get(), std::max(1, int(std::lround(source->w * scale))),
      std::max(1, int(std::lround(source->h * scale))), SDL_SCALEMODE_LINEAR));
  if (!resized)
    throw std::runtime_error("Cannot scale picture " + id + ": " +
                             SDL_GetError());
  return resized;
}

std::size_t AssetProvider::cachedTextures() const { return textures.size(); }

void AssetProvider::clearCache() {
  textures.clear();
  textureRenderer = nullptr;
}

FileSystemAssetProvider::FileSystemAssetProvider(std::filesystem::path value)
    : root(value.empty() ? value : std::filesystem::absolute(value)) {}

const std::filesystem::path &FileSystemAssetProvider::getRoot() const {
  return root;
}

std::string FileSystemAssetProvider::name() const {
  return root.empty() ? "Files" : "Files (" + root.string() + ")";
}

std::filesystem::path
FileSystemAssetProvider::resolve(const std::string &id) const {
  if (id.empty() || id.find('\0') != std::string::npos)
    throw std::invalid_argument("Invalid asset identifier");
  const std::filesystem::path path(id);
  if (root.empty())
    return path;
  if (path.is_absolute() || path.has_root_name())
    throw std::invalid_argument("Asset must be relative to its root: " + id);
  const auto resolved = (root / path).lexically_normal();
  const auto relative = resolved.lexically_relative(root);
  if (relative.empty() || *relative.begin() == "..")
    throw std::invalid_argument("Asset escapes its root: " + id);
  return resolved;
}

bool FileSystemAssetProvider::contains(const std::string &id) const {
  try {
    std::error_code error;
    return std::filesystem::is_regular_file(resolve(id), error);
  } catch (const std::invalid_argument &) {
    return false;
  }
}

std::vector<Uint8> FileSystemAssetProvider::read(const std::string &id) const {
  const auto path = resolve(id);
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || !std::filesystem::is_regular_file(path))
    throw std::runtime_error("Asset not found: " + id);
  if (size > maximumAssetSize)
    throw std::runtime_error("Asset exceeds 1 GiB: " + id);
  std::ifstream input(path, std::ios::binary);
  std::vector<Uint8> bytes(static_cast<std::size_t>(size));
  if (!input.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size())))
    throw std::runtime_error("Cannot read asset: " + id);
  return bytes;
}

std::vector<std::string>
FileSystemAssetProvider::list(const std::string &prefix) const {
  const auto base = root.empty() ? std::filesystem::current_path() : root;
  std::vector<std::string> result;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator it(base, error), end;
       !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error))
      continue;
    auto id = it->path().lexically_relative(base).generic_string();
    if (id.compare(0, prefix.size(), prefix) == 0)
      result.push_back(std::move(id));
  }
  return result;
}

std::string FileSystemAssetProvider::localPath(const std::string &id) const {
  return resolve(id).string();
}

} // namespace Kotonoha
