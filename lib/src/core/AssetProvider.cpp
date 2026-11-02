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

IOStreamPtr memoryStream(const std::vector<Uint8> &bytes) {
  IOStreamPtr stream{SDL_IOFromConstMem(bytes.data(), bytes.size())};
  if (!stream)
    throw std::runtime_error(SDL_GetError());
  return stream;
}
} // namespace

AssetProvider::AssetProvider() = default;
AssetProvider::~AssetProvider() = default;
void AssetProvider::setOverlay(const std::filesystem::path &directory) {
  AssetOverlay mounted(directory);
  overlay = std::move(mounted);
  clearCache();
}
std::string AssetProvider::overlayPath(const std::string &id) const {
  return overlay.resolve(overlayId(id));
}
bool AssetProvider::contains(const std::string &id) const {
  return !overlayPath(id).empty() || containsAsset(id);
}
std::vector<Uint8> AssetProvider::read(const std::string &id) const {
  const auto file = overlayPath(id);
  if (file.empty())
    return readAsset(id);
  const auto size = std::filesystem::file_size(file);
  if (size > maximumAssetSize)
    throw std::runtime_error("Overlay asset is too large: " + id);
  std::ifstream input(file, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open overlay asset: " + id);
  std::vector<Uint8> bytes(size);
  if (size && !input.read(reinterpret_cast<char *>(bytes.data()),
                          std::streamsize(size)))
    throw std::runtime_error("Cannot read overlay asset: " + id);
  return bytes;
}
std::string AssetProvider::localPath(const std::string &id) const {
  auto file = overlayPath(id);
  return file.empty() ? localPathAsset(id) : file;
}
std::vector<std::string> AssetProvider::list(const std::string &prefix) const {
  auto result = listAsset(prefix);
  auto overrides = overlay.list(prefix);
  result.insert(result.end(), overrides.begin(), overrides.end());
  std::map<std::string, std::string> unique;
  for (auto &id : result)
    unique[AssetOverlay::key(id)] = id;
  result.clear();
  for (auto &[key, id] : unique)
    result.push_back(id);
  return result;
}
SurfacePtr AssetProvider::loadSurface(const std::string &id) const {
  if (!overlayPath(id).empty()) {
    try {
      return MediaDecoder::decodePicture(read(id));
    } catch (const std::exception
                 &) { /* Native asset formats use the game's decoder below. */
    }
  }
  return loadSurfaceAsset(id);
}
SurfacePtr AssetProvider::loadFrame(const std::string &id,
                                    double seconds) const {
  if (!overlayPath(id).empty()) {
    try {
      return MediaDecoder::decodePicture(read(id), seconds);
    } catch (const std::exception &) {
    }
  }
  return loadFrameAsset(id, seconds);
}
AudioClip AssetProvider::loadAudio(const std::string &id) const {
  if (!overlayPath(id).empty()) {
    try {
      return MediaDecoder::decodeAudio(read(id));
    } catch (const std::exception &) {
    }
  }
  return loadAudioAsset(id);
}

std::vector<std::string> AssetProvider::listAsset(const std::string &) const {
  return {};
}

std::string AssetProvider::localPathAsset(const std::string &) const {
  return {};
}

SurfacePtr AssetProvider::loadSurfaceAsset(const std::string &id) const {
  const auto bytes = read(id);
  if (SurfacePtr surface{
          SDL_LoadSurface_IO(memoryStream(bytes).release(), true)})
    return surface;
  const std::string sdlError = SDL_GetError();
  try {
    return MediaDecoder::decodePicture(bytes);
  } catch (const std::exception &error) {
    throw std::runtime_error("Cannot decode picture " + id + ": " + sdlError +
                             "; FFmpeg: " + error.what());
  }
}

SurfacePtr AssetProvider::loadFrameAsset(const std::string &id,
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

AudioClip AssetProvider::loadAudioAsset(const std::string &id) const {
  const auto bytes = read(id);
  AudioClip clip;
  Uint8 *data = nullptr;
  Uint32 length = 0;
  if (SDL_LoadWAV_IO(memoryStream(bytes).release(), true, &clip.spec, &data,
                     &length)) {
    SDLMemoryPtr<Uint8> owned(data);
    clip.data.assign(owned.get(), owned.get() + length);
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
  if (auto *pipeline = Upscaling::forRenderer(renderer))
    pipeline->process(*created, 0);
  SDL_Texture &result = *created;
  textures.emplace(id, std::move(created));
  return result;
}

SDL_Texture &AssetProvider::texture(RendererRef renderer, const std::string &id,
                                    float scale, Upscaling *upscaling) {
  if (!std::isfinite(scale) || scale <= 0)
    throw std::invalid_argument("Texture scale must be positive");
  if (upscaling)
    return upscaling->process(texture(renderer, id), 0);
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
  {
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

bool FileSystemAssetProvider::containsAsset(const std::string &id) const {
  try {
    std::error_code error;
    return std::filesystem::is_regular_file(resolve(id), error);
  } catch (const std::invalid_argument &) {
    return false;
  }
}

std::vector<Uint8>
FileSystemAssetProvider::readAsset(const std::string &id) const {
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
FileSystemAssetProvider::listAsset(const std::string &prefix) const {
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

std::string
FileSystemAssetProvider::localPathAsset(const std::string &id) const {
  return resolve(id).string();
}

} // namespace Kotonoha
