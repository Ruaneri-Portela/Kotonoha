#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/SurfaceTexture.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <cmath>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace Kotonoha {
namespace {
constexpr std::uintmax_t maximumAssetSize = 1024ull * 1024 * 1024;

IOStreamPtr memoryStream(const std::vector<Uint8> &bytes) {
  IOStreamPtr stream{SDL_IOFromConstMem(bytes.data(), bytes.size())};
  if (!stream)
    throw std::runtime_error(SDL_GetError());
  return stream;
}

#ifdef __EMSCRIPTEN__
// clang-format treats JavaScript operators inside EM_ASYNC_JS as C++ tokens.
// clang-format off
EM_ASYNC_JS(int, browserDecodeImage, (const Uint8 *data, int size), {
  if (typeof KotonohaDecodeImage !== "function") return 0;
  return await KotonohaDecodeImage(HEAPU8.slice(data, data + size));
});
// clang-format on

bool browserImageFormat(const std::vector<Uint8> &bytes) {
  const auto starts = [&](const char *magic, std::size_t size) {
    return bytes.size() >= size && std::memcmp(bytes.data(), magic, size) == 0;
  };
  return starts("\x89PNG", 4) || starts("\xff\xd8\xff", 3) ||
         starts("GIF8", 4) || starts("BM", 2) ||
         (starts("RIFF", 4) && bytes.size() >= 12 &&
          std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) ||
         (bytes.size() >= 12 &&
          (std::memcmp(bytes.data() + 4, "ftypavif", 8) == 0 ||
           std::memcmp(bytes.data() + 4, "ftypavis", 8) == 0));
}

SurfacePtr browserPicture(const std::vector<Uint8> &bytes) {
  if (!browserImageFormat(bytes) || bytes.size() > INT_MAX)
    return {};
  const int token = browserDecodeImage(bytes.data(), int(bytes.size()));
  if (!token)
    return {};
  const int width = EM_ASM_INT({ return KotonohaImageWidth($0); }, token);
  const int height = EM_ASM_INT({ return KotonohaImageHeight($0); }, token);
  SurfacePtr surface;
  if (width > 0 && height > 0 && width <= 16384 && height <= 16384)
    surface.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
  if (surface && !EM_ASM_INT(
                     { return KotonohaCopyImage($0, $1, $2); }, token,
                     surface->pixels, surface->pitch))
    surface.reset();
  EM_ASM({ KotonohaReleaseImage($0); }, token);
  return surface;
}

// clang-format off
EM_ASYNC_JS(int, browserDecodeAudio, (const Uint8 *data, int size), {
  if (typeof KotonohaDecodeAudio !== "function") return 0;
  return await KotonohaDecodeAudio(HEAPU8.slice(data, data + size));
});
// clang-format on

bool browserAudioFormat(const std::vector<Uint8> &bytes) {
  if (bytes.size() < 4)
    return false;
  const auto starts = [&](const char *magic, std::size_t size) {
    return std::memcmp(bytes.data(), magic, size) == 0;
  };
  return starts("OggS", 4) || starts("fLaC", 4) || starts("ID3", 3) ||
         starts("\x1a\x45\xdf\xa3", 4) ||
         (starts("RIFF", 4) && bytes.size() >= 12 &&
          std::memcmp(bytes.data() + 8, "WAVE", 4) == 0) ||
         (bytes.size() >= 12 &&
          std::memcmp(bytes.data() + 4, "ftyp", 4) == 0) ||
         (bytes[0] == 0xff && (bytes[1] & 0xe0) == 0xe0);
}

AudioClip browserAudio(const std::vector<Uint8> &bytes) {
  if (!browserAudioFormat(bytes) || bytes.size() > INT_MAX)
    return {};
  const int token = browserDecodeAudio(bytes.data(), int(bytes.size()));
  if (!token)
    return {};
  const int channels = EM_ASM_INT({ return KotonohaAudioChannels($0); }, token);
  const int rate = EM_ASM_INT({ return KotonohaAudioRate($0); }, token);
  const int frames = EM_ASM_INT({ return KotonohaAudioFrames($0); }, token);
  AudioClip clip;
  if (channels > 0 && channels <= 8 && rate > 0 && frames > 0 &&
      std::uint64_t(frames) * channels * sizeof(float) <=
          1024ull * 1024 * 1024) {
    clip.spec = {SDL_AUDIO_F32, channels, rate};
    clip.data.resize(std::size_t(frames) * channels * sizeof(float));
    if (!EM_ASM_INT(
            { return KotonohaCopyAudio($0, $1); }, token, clip.data.data()))
      clip = {};
  }
  EM_ASM({ KotonohaReleaseAudio($0); }, token);
  return clip;
}
#endif

} // namespace

AssetProvider::AssetProvider() = default;
AssetProvider::~AssetProvider() = default;
void AssetProvider::setOverlay(const std::filesystem::path &directory) {
  AssetOverlay mounted(directory, !directory.empty());
  overlay = std::move(mounted);
  clearCache();
}
std::string AssetProvider::overlayPath(const std::string &id) const {
  return overlay.resolve(overlayId(id));
}
bool AssetProvider::hasOverlay(const std::string &id) const {
  return !overlayPath(id).empty();
}
bool AssetProvider::discardOverlay(const std::string &id) {
  const auto resolved = overlayPath(id);
  if (resolved.empty())
    return false;
  const auto root = overlay.root();
  if (root.empty())
    return false;
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(resolved, error);
  if (error || canonical.parent_path().empty() ||
      canonical.lexically_relative(root).empty() ||
      *canonical.lexically_relative(root).begin() == "..")
    throw std::runtime_error("Overlay asset escapes its mounted directory");
  if (!std::filesystem::remove(canonical, error) || error)
    throw std::runtime_error("Cannot discard overlay asset: " +
                             error.message());
  setOverlay(root);
  return true;
}
std::string AssetProvider::overlayWriteId(const std::string &id) const {
  return AssetOverlay::key(overlayId(id));
}
bool AssetProvider::contains(const std::string &id) const {
  return !overlayPath(id).empty() || containsAsset(id);
}
bool AssetProvider::contains(const std::string &id, AssetSource source) const {
  return source == AssetSource::Original ? containsOriginalAsset(id)
                                         : contains(id);
}
std::vector<Uint8> AssetProvider::read(const std::string &id) const {
  const auto file = overlayPath(id);
  if (file.empty())
    return readAsset(id);
  return Filesystem::readLocalFile(file, maximumAssetSize);
}
std::vector<Uint8> AssetProvider::read(const std::string &id,
                                       AssetSource source) const {
  return source == AssetSource::Original ? readOriginalAsset(id) : read(id);
}
std::uint64_t AssetProvider::dumpAsset(const std::string &id,
                                       const std::filesystem::path &destination,
                                       AssetSource source) const {
  if (id.empty() || destination.empty() || std::filesystem::exists(destination))
    throw std::invalid_argument(
        "Select a non-empty asset and a new destination for dumping");
  if (!contains(id, source))
    throw std::runtime_error("Cannot dump missing provider asset: " + id);

  // Never use an archive stream for effective bytes when an overlay wins.
  // Some legacy providers implement openOriginalStream by delegating to
  // the effective stream. The raw original export must bypass that route.
  auto stream = source == AssetSource::Original || hasOverlay(id)
                    ? std::unique_ptr<AssetStream>{}
                    : openStream(id);
  if (stream && stream->size() > maximumAssetSize)
    throw std::runtime_error("Asset dump exceeds the 1 GiB safety limit");

  if (!destination.parent_path().empty())
    std::filesystem::create_directories(destination.parent_path());
  auto temporary = destination;
  temporary += ".partial";
  if (std::filesystem::exists(temporary))
    throw std::runtime_error("An unfinished asset dump already exists: " +
                             temporary.string());

  std::uint64_t total = 0;
  try {
    {
      Filesystem::OutputFile output(temporary, std::ios::binary);
      if (!output)
        throw std::runtime_error("Cannot open asset dump destination");
      if (stream) {
        std::array<Uint8, 64 * 1024> buffer{};
        const auto expected = stream->size();
        while (total < expected) {
          const auto request = static_cast<std::size_t>(
              std::min<std::uint64_t>(buffer.size(), expected - total));
          const auto count = stream->read(buffer.data(), request);
          if (!count || count > request)
            throw std::runtime_error("Asset stream ended early or over-read");
          output.write(reinterpret_cast<const char *>(buffer.data()),
                       static_cast<std::streamsize>(count));
          if (!output)
            throw std::runtime_error("Cannot write streamed asset bytes");
          total += count;
        }
      } else {
        // Providers with only readAsset() are supported without a special
        // debug implementation. The normal provider size cap still applies.
        const auto bytes = read(id, source);
        if (bytes.size() > maximumAssetSize)
          throw std::runtime_error("Asset dump exceeds the 1 GiB safety limit");
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output)
          throw std::runtime_error("Cannot write asset bytes");
        total = bytes.size();
      }
      output.close();
      if (!output)
        throw std::runtime_error("Cannot finalize asset dump");
    }
    // Never clobber an earlier dump.
    if (std::filesystem::exists(destination))
      throw std::runtime_error("Asset dump destination already exists");
    std::filesystem::rename(temporary, destination);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
  return total;
}

std::string AssetProvider::localPath(const std::string &id) const {
  auto file = overlayPath(id);
  return file.empty() ? localPathAsset(id) : file;
}
std::string AssetProvider::loadTextUtf8(const std::string &id,
                                       const char *encoding,
                                       AssetSource source) const {
  if (!encoding || !*encoding)
    throw std::invalid_argument("Text encoding must not be empty");
  return decodeTextAsset(id, read(id, source), encoding);
}
std::string AssetProvider::decodeTextAsset(const std::string &,
                                          const std::vector<Uint8> &bytes,
                                          const char *encoding) const {
  if (bytes.empty())
    return {};
  size_t offset = 0;
  // UTF-32 must precede UTF-16: their little-endian BOMs share a prefix.
  if (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xfe &&
      bytes[2] == 0 && bytes[3] == 0) {
    offset = 4;
    encoding = "UTF-32LE";
  } else if (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0 &&
             bytes[2] == 0xfe && bytes[3] == 0xff) {
    offset = 4;
    encoding = "UTF-32BE";
  } else if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
             bytes[2] == 0xbf) {
    offset = 3;
    encoding = "UTF-8";
  } else if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
    offset = 2;
    encoding = "UTF-16LE";
  } else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
    offset = 2;
    encoding = "UTF-16BE";
  }
  return ConvertEncoding(
      std::string_view(reinterpret_cast<const char *>(bytes.data() + offset),
                       bytes.size() - offset),
      encoding);
}
std::string AssetProvider::localPath(const std::string &id,
                                     AssetSource source) const {
  return source == AssetSource::Original ? localPathAsset(id) : localPath(id);
}
std::vector<std::string> AssetProvider::list(const std::string &prefix) const {
  auto result = listAsset(prefix);
  auto overrides = overlay.list(prefix);
  result.reserve(result.size() + overrides.size());
  result.insert(result.end(), overrides.begin(), overrides.end());
  std::unordered_map<std::string, std::string> unique;
  unique.reserve(result.size());
  for (const auto &id : result) {
    const auto key = AssetOverlay::key(id);
    // Provider IDs like "voice:100" are valid logical IDs but not overlay
    // paths. Preserve each such ID instead of grouping them under an empty key.
    unique[key.empty() ? id : key] = id;
  }
  result.clear();
  result.reserve(unique.size());
  for (const auto &[key, id] : unique)
    result.push_back(id);
  // Stable ordering is important for huge asset catalogs in the shared Debug
  // Inspector and Voice Dubbing applet, regardless of provider map type.
  std::sort(result.begin(), result.end());
  return result;
}
SurfacePtr AssetProvider::loadSurface(const std::string &id) const {
  if (!overlayPath(id).empty()) {
    try {
      const auto bytes = read(id);
#ifdef __EMSCRIPTEN__
      if (auto surface = browserPicture(bytes))
        return surface;
#endif
      return MediaDecoder::decodePicture(bytes);
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
AudioClip AssetProvider::loadAudio(const std::string &id,
                                   bool forceOriginal) const {
  if (forceOriginal)
    return loadOriginalAudio(id);
  AudioClip clip;
  if (hasOverlay(id)) {
    try {
      const auto bytes = read(id);
#ifdef __EMSCRIPTEN__
      clip = browserAudio(bytes);
      if (clip.data.empty())
#endif
        clip = MediaDecoder::decodeAudio(bytes);
      noteAudio(id);
      return clip;
    } catch (const std::exception &) {
    }
  }
  clip = loadAudioAsset(id);
  noteAudio(id, clip);
  return clip;
}
AudioClip AssetProvider::loadAudio(const std::string &id,
                                   AssetSource source) const {
  return loadAudio(id, source == AssetSource::Original);
}

AudioClip AssetProvider::loadOriginalAudio(const std::string &id) const {
  // A force-original request must not trust the normal playback cache. Some
  // runtimes resolve a logical ID before loading it, and an older/cache-warm
  // session may therefore have associated overlay PCM with that ID. Decode
  // through the provider's original-only path every time.
  auto clip = loadAudioAsset(id);
  noteAudio(id, clip);
  return clip;
}

void AssetProvider::noteAudio(const std::string &id,
                              const AudioClip &clip) const {
  if (id.empty())
    return;
  std::lock_guard lock(debugAudioMutex_);
  auto recent = std::find(recentAudio_.begin(), recentAudio_.end(), id);
  if (recent != recentAudio_.end())
    recentAudio_.erase(recent);
  recentAudio_.push_front(id);
  if (recentAudio_.size() > 64)
    recentAudio_.pop_back();
  // Voice lines are cheap to retain for waveform editing. Large music tracks
  // remain discoverable but are reloaded on selection instead of duplicated.
  if (clip.data.size() <= 128u * 1024u * 1024u)
    originalAudio_[id] = clip;
  if (originalAudio_.size() > 64)
    for (auto i = recentAudio_.rbegin(); i != recentAudio_.rend(); ++i)
      if (*i != id && originalAudio_.erase(*i))
        break;
}

void AssetProvider::noteAudio(const std::string &id) const {
  if (id.empty())
    return;
  std::lock_guard lock(debugAudioMutex_);
  auto found = std::find(recentAudio_.begin(), recentAudio_.end(), id);
  if (found != recentAudio_.end())
    recentAudio_.erase(found);
  recentAudio_.push_front(id);
  if (recentAudio_.size() > 64)
    recentAudio_.pop_back();
}

std::vector<std::string> AssetProvider::recentAudio() const {
  std::lock_guard lock(debugAudioMutex_);
  return {recentAudio_.begin(), recentAudio_.end()};
}

std::optional<AudioClip>
AssetProvider::originalAudio(const std::string &id) const {
  std::lock_guard lock(debugAudioMutex_);
  auto found = originalAudio_.find(id);
  return found == originalAudio_.end()
             ? std::nullopt
             : std::optional<AudioClip>(found->second);
}

std::vector<std::string> AssetProvider::listAsset(const std::string &) const {
  return {};
}

std::string AssetProvider::localPathAsset(const std::string &) const {
  return {};
}

std::unique_ptr<AssetStream> AssetProvider::openStream(const std::string &) const {
  return {};
}

std::unique_ptr<AssetStream>
AssetProvider::openOriginalStream(const std::string &id) const {
  return openStream(id);
}

std::unique_ptr<AssetStream>
AssetProvider::openStream(const std::string &id, AssetSource source) const {
  return source == AssetSource::Original ? openOriginalStream(id)
                                         : openStream(id);
}

SurfacePtr AssetProvider::loadSurfaceAsset(const std::string &id) const {
  const auto bytes = readAsset(id);
  // An empty picture is a valid transition placeholder in upstream runtimes.
  if (bytes.empty()) {
    SurfacePtr fallback(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
    if (fallback)
      SDL_FillSurfaceRect(fallback.get(), nullptr,
                          SDL_MapSurfaceRGB(fallback.get(), 0, 0, 0));
    return fallback;
  }
#ifdef __EMSCRIPTEN__
  if (auto surface = browserPicture(bytes))
    return surface;
#endif
  // SDL_LoadSurface_IO requires a newer SDL3 than Debian 13's SDL 3.2.
  // SDL_LoadBMP_IO handles BMP on older systems; MediaDecoder handles other
  // image formats below, so this keeps the same FFmpeg fallback.
#if SDL_VERSION_ATLEAST(3, 4, 0)
  SurfacePtr surface(SDL_LoadSurface_IO(memoryStream(bytes).release(), true));
#else
  SurfacePtr surface(SDL_LoadBMP_IO(memoryStream(bytes).release(), true));
#endif
  if (surface)
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
    return loadSurfaceAsset(id);
  try {
    return MediaDecoder::decodePicture(readAsset(id), seconds);
  } catch (const std::exception &error) {
    throw std::runtime_error("Cannot decode frame of " + id + ": " +
                             error.what());
  }
}

AudioClip AssetProvider::loadAudioAsset(const std::string &id) const {
  const auto bytes = readAsset(id);
#ifdef __EMSCRIPTEN__
  if (auto browser = browserAudio(bytes); !browser.data.empty())
    return browser;
#endif
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
  return stageTexture(renderer, id, loadSurface(id));
}

SDL_Texture &AssetProvider::stageTexture(RendererRef renderer,
                                         const std::string &id,
                                         SurfacePtr surface) {
  if (textureRenderer != &renderer) {
    textures.clear();
    textureRenderer = &renderer;
  }
  if (auto found = textures.find(id); found != textures.end())
    return *found->second;
  if (!surface)
    throw std::invalid_argument("Staged asset surface is empty");
  const SurfacePtr prepared = prepareSurfaceForLinearSampling(surface.get());
  if (!prepared)
    throw std::runtime_error("Cannot prepare picture " + id + ": " +
                             SDL_GetError());
  TexturePtr created(SDL_CreateTextureFromSurface(&renderer, prepared.get()));
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
    const SurfacePtr prepared = prepareSurfaceForLinearSampling(source.get());
    if (!prepared)
      throw std::runtime_error("Cannot prepare picture " + id + ": " +
                               SDL_GetError());
    SurfacePtr resized(SDL_ScaleSurface(
        prepared.get(), std::max(1, int(std::lround(source->w * scale))),
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
  const SurfacePtr prepared = prepareSurfaceForLinearSampling(source.get());
  if (!prepared)
    throw std::runtime_error("Cannot prepare picture " + id + ": " +
                             SDL_GetError());
  if (upscaling)
    return upscaling->scaled(*prepared, scale);
  (void)renderer;
  SurfacePtr resized(SDL_ScaleSurface(
      prepared.get(), std::max(1, int(std::lround(source->w * scale))),
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

namespace {
class MountedAssetStream final : public AssetStream {
  std::unique_ptr<Filesystem::FileHandle> file_;

public:
  explicit MountedAssetStream(std::unique_ptr<Filesystem::FileHandle> file)
      : file_(std::move(file)) {}
  std::uint64_t size() const override {
    const auto n = file_->size();
    if (n < 0)
      throw std::runtime_error("Unknown asset size");
    return static_cast<std::uint64_t>(n);
  }
  std::uint64_t tell() const override {
    const auto n = file_->tell();
    if (n < 0)
      throw std::runtime_error("Unable to tell asset stream");
    return static_cast<std::uint64_t>(n);
  }
  std::size_t read(Uint8 *out, std::size_t amount) override {
    const auto n = file_->read(out, amount);
    if (n < 0)
      throw std::runtime_error("Asset stream read failure");
    return static_cast<std::size_t>(n);
  }
  bool seek(std::uint64_t position) override {
    return position <= static_cast<std::uint64_t>(INT64_MAX) &&
           file_->seek(static_cast<std::int64_t>(position),
                       Filesystem::SeekOrigin::Begin) ==
               static_cast<std::int64_t>(position);
  }
};
} // namespace

FileSystemAssetProvider::FileSystemAssetProvider(std::filesystem::path value)
    : root(value.empty()
               ? value
               : std::filesystem::absolute(gameDataDirectory(value))) {
#ifdef __EMSCRIPTEN__
  if (root.empty() && Filesystem::remoteGameAssets())
    fallback_ = Filesystem::mountGameAssets("/game-assets");
  if (!root.empty()) {
    for (const auto &part : root) {
      if (part == "game-assets") {
        std::error_code ec;
        if (Filesystem::remoteGameAssets() ||
            std::filesystem::is_directory(root, ec))
          mounted_ = Filesystem::mountGameAssets(root);
        break;
      }
    }
  }
#endif
}

FileSystemAssetProvider::FileSystemAssetProvider(
    std::shared_ptr<Filesystem::Filesystem> mounted)
    : mounted_(std::move(mounted)) {
  if (!mounted_)
    throw std::invalid_argument("Virtual asset filesystem is null");
}

const std::filesystem::path &FileSystemAssetProvider::getRoot() const {
  return root;
}

std::string FileSystemAssetProvider::name() const {
  if (mounted_)
    return "Mounted files (virtual)";
  return root.empty() ? "Files" : "Files (" + root.string() + ")";
}

std::filesystem::path
FileSystemAssetProvider::resolve(const std::string &id) const {
  if (id.empty() || id.find('\0') != std::string::npos)
    throw std::invalid_argument("Invalid asset identifier");
  const std::filesystem::path path(id);
  if (root.empty()) {
    if (auto found = FileResolver::findAsset(path))
      return *found;
    return path;
  }
  if (path.is_absolute() || path.has_root_name())
    throw std::invalid_argument("Asset must be relative to its root: " + id);
  const auto resolved = (root / path).lexically_normal();
  const auto relative = resolved.lexically_relative(root);
  if (relative.empty() || *relative.begin() == "..")
    throw std::invalid_argument("Asset escapes its root: " + id);
  return resolved;
}

bool FileSystemAssetProvider::containsAsset(const std::string &id) const {
  if (mounted_) {
    try {
      return mounted_->exists(id) &&
             mounted_->stat(id).type == Filesystem::FileType::Regular;
    } catch (const Filesystem::FilesystemError &error) {
      if (error.code() == Filesystem::ErrorCode::InvalidPath ||
          error.code() == Filesystem::ErrorCode::NotFound)
        return false;
      throw;
    }
  }
  if (fallback_) {
    const auto name =
        id.compare(0, 12, "game-assets/") == 0 ? id.substr(12) : id;
    try {
      if (fallback_->exists(name) &&
          fallback_->stat(name).type == Filesystem::FileType::Regular)
        return true;
    } catch (const Filesystem::FilesystemError &error) {
      if (error.code() != Filesystem::ErrorCode::InvalidPath &&
          error.code() != Filesystem::ErrorCode::NotFound)
        throw;
    }
  }
  try {
    std::error_code error;
    return std::filesystem::is_regular_file(resolve(id), error);
  } catch (const std::invalid_argument &) {
    return false;
  }
}

std::vector<Uint8>
FileSystemAssetProvider::readAsset(const std::string &id) const {
  if (mounted_)
    return Filesystem::readAll(*mounted_->open(id), maximumAssetSize);
  const auto path = resolve(id);
  if (fallback_ && id.compare(0, 12, "game-assets/") == 0 &&
      fallback_->exists(id.substr(12)))
    return Filesystem::readAll(*fallback_->open(id.substr(12)),
                               maximumAssetSize);
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("Asset not found: " + id);
  return Filesystem::readLocalFile(path, maximumAssetSize);
}

std::vector<std::string>
FileSystemAssetProvider::listAsset(const std::string &prefix) const {
  if (mounted_) {
    std::vector<std::string> result;
    std::vector<std::string> directories{""};
    while (!directories.empty()) {
      auto dir = std::move(directories.back());
      directories.pop_back();
      for (const auto &entry : mounted_->list(dir)) {
        if (entry.type == Filesystem::FileType::Directory)
          directories.push_back(entry.path);
        else if (entry.path.compare(0, prefix.size(), prefix) == 0)
          result.push_back(entry.path);
      }
    }
    return result;
  }
  std::vector<std::string> result;
  const auto collect = [&](const std::filesystem::path &base,
                           const std::string &mount) {
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(base, error), end;
         !error && it != end; it.increment(error)) {
      if (!it->is_regular_file(error))
        continue;
      auto id = mount + it->path().lexically_relative(base).generic_string();
      if (id.compare(0, prefix.size(), prefix) == 0)
        result.push_back(std::move(id));
    }
  };
  if (!root.empty()) {
    collect(root, "");
  } else {
    const auto directories = applicationDirectories();
    for (const auto &base : directories.engineAssets)
      collect(base, "assets/");
    for (const auto &base : directories.gameAssets)
      collect(base, "game-assets/");
  }
  if (fallback_) {
    std::vector<std::string> dirs{""};
    while (!dirs.empty()) {
      auto dir = std::move(dirs.back());
      dirs.pop_back();
      for (const auto &entry : fallback_->list(dir)) {
        if (entry.type == Filesystem::FileType::Directory)
          dirs.push_back(entry.path);
        else {
          const auto id = std::string("game-assets/") + entry.path;
          if (id.compare(0, prefix.size(), prefix) == 0)
            result.push_back(id);
        }
      }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
  }
  return result;
}

std::string
FileSystemAssetProvider::localPathAsset(const std::string &id) const {
  // Virtual sources (TAR/ISO/HTTP) never require a physical path.
  if (mounted_)
    return {};
  const auto path = resolve(id);
  if (fallback_ && id.compare(0, 12, "game-assets/") == 0 &&
      fallback_->exists(id.substr(12))) {
    const auto info = fallback_->stat(id.substr(12));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) ||
        std::filesystem::file_size(path, ec) !=
            static_cast<std::uintmax_t>(info.size))
      return {};
  }
  return path.string();
}

std::unique_ptr<AssetStream>
FileSystemAssetProvider::openStream(const std::string &id) const {
  if (mounted_)
    return std::make_unique<MountedAssetStream>(mounted_->open(id));
  if (fallback_ && id.compare(0, 12, "game-assets/") == 0 &&
      fallback_->exists(id.substr(12)))
    return std::make_unique<MountedAssetStream>(fallback_->open(id.substr(12)));
  const auto local = resolve(id);
  if (std::filesystem::is_regular_file(local))
    return std::make_unique<MountedAssetStream>(
        std::make_unique<Filesystem::SDLFileSource>(local));
  return {};
}

IndexedFileSystemAssetProvider::IndexedFileSystemAssetProvider(
    const std::filesystem::path &root)
    : FileSystemAssetProvider(
          std::filesystem::canonical(gameDataDirectory(root))),
      index(getRoot(), false) {}
IndexedFileSystemAssetProvider::IndexedFileSystemAssetProvider(
    std::shared_ptr<Filesystem::Filesystem> mounted)
    : FileSystemAssetProvider(std::move(mounted)), index({}, false) {}

bool IndexedFileSystemAssetProvider::containsAsset(
    const std::string &id) const {
  if (mounted())
    return FileSystemAssetProvider::containsAsset(id);
  return !index.resolve(id).empty();
}
std::string
IndexedFileSystemAssetProvider::localPathAsset(const std::string &id) const {
  if (mounted())
    return {};
  const auto path = index.resolve(id);
  if (path.empty())
    throw std::runtime_error("Asset not found in " + getRoot().string() + ": " +
                             id);
  return path;
}
std::vector<Uint8>
IndexedFileSystemAssetProvider::readAsset(const std::string &id) const {
  if (mounted())
    return FileSystemAssetProvider::readAsset(id);
  const auto relative =
      std::filesystem::path(localPathAsset(id)).lexically_relative(getRoot());
  return FileSystemAssetProvider::readAsset(relative.generic_string());
}
std::vector<std::string>
IndexedFileSystemAssetProvider::listAsset(const std::string &prefix) const {
  if (mounted())
    return FileSystemAssetProvider::listAsset(prefix);
  return index.list(prefix);
}

} // namespace Kotonoha
