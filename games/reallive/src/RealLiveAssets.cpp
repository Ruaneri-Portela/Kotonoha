#include "RealLiveAssets.hpp"
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <RealLive/Images.hpp>
#include <RealLive/Voice.hpp>
#include <cstring>
#include <stdexcept>
namespace RealLive {
RealLiveAssets::RealLiveAssets(const std::filesystem::path &directory,
                               const std::filesystem::path &overlay)
    : root(std::filesystem::canonical(directory)), config("") {
  if (Kotonoha::isISOImage(root)) {
    auto disc = std::make_unique<Kotonoha::DiscImageAssetProvider>(root);
    SDL_Log("Reading REALLIVE ISO directly: %s (%zu files; no extraction)",
            root.string().c_str(), disc->getDisc().files().size());
    source = std::move(disc);
  } else {
    source = std::make_unique<Kotonoha::IndexedFileSystemAssetProvider>(root);
  }
  source->setOverlay({});
  setOverlay(overlay);
  const auto ini = read("Gameexe.ini");
  configSource.assign(ini.begin(), ini.end());
  config = Config(configSource);
  if (!contains("Seen.txt"))
    throw std::runtime_error("REALLIVE Seen.txt not found");
}
bool RealLiveAssets::containsAsset(const std::string &id) const {
  return source->contains(id);
}
std::vector<Uint8> RealLiveAssets::readAsset(const std::string &id) const {
  try {
    return source->read(id);
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Warning: Asset not found: %s (%s)", id.c_str(), e.what());
    return {};
  }
}
std::vector<std::string>
RealLiveAssets::listAsset(const std::string &prefix) const {
  return source->list(prefix);
}
std::string RealLiveAssets::localPathAsset(const std::string &id) const {
  return source->localPath(id);
}
ScriptArchive RealLiveAssets::scripts() const {
  std::map<int, std::vector<std::uint8_t>> overrides;
  for (const auto &id : list()) {
    const auto name = canonical(id);
    if (name.size() != 12 || name.substr(0, 4) != "SEEN" ||
        name.substr(8) != ".TXT" ||
        name.find_first_not_of("0123456789", 4) != 8)
      continue;
    overrides.emplace(std::stoi(name.substr(4, 4)), read(id));
  }
  return ScriptArchive(read("Seen.txt"), overrides);
}
std::string RealLiveAssets::graphic(const std::string &name) const {
  std::string file = name;
  auto question = file.find('?');
  if (question != file.npos)
    file.resize(question);
  for (auto prefix : {"G00/", "PDT/", ""})
    for (auto suffix : {"", ".G00", ".PDT", ".PNG", ".BMP"}) {
      auto id = std::string(prefix) + file + suffix;
      if (contains(id))
        return canonical(id);
    }
  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
              "Warning: REALLIVE graphic missing: %s (attempted: "
              "G00/%s.{G00,PDT,PNG,BMP}, PDT/%s.{G00,PDT}, %s.{G00,PDT})",
              name.c_str(), file.c_str(), file.c_str(), file.c_str());
  return "";
}
Kotonoha::SurfacePtr
RealLiveAssets::loadSurfaceAsset(const std::string &id) const {
  const auto extension =
      canonical(std::filesystem::path(id).extension().string());
  if (extension != ".G00" && extension != ".PDT")
    return AssetProvider::loadSurfaceAsset(id);
  try {
    const auto bytes = read(id);
    if (bytes.empty()) {
      Kotonoha::SurfacePtr fallback(
          SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
      if (fallback)
        SDL_FillSurfaceRect(fallback.get(), nullptr,
                            SDL_MapSurfaceRGB(fallback.get(), 0, 0, 0));
      return fallback;
    }
    const auto image =
        extension == ".PDT" ? decodePDT(bytes) : decodeG00(bytes);

    // Validar dimensões da imagem decodificada
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
      SDL_LogError(
          SDL_LOG_CATEGORY_APPLICATION,
          "REALLIVE: Invalid image dimensions for %s: %ux%u (%zu bytes)",
          id.c_str(), image.width, image.height, image.rgba.size());
      Kotonoha::SurfacePtr fallback(
          SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
      if (fallback)
        SDL_FillSurfaceRect(fallback.get(), nullptr,
                            SDL_MapSurfaceRGB(fallback.get(), 0, 0, 0));
      return fallback;
    }

    Kotonoha::SurfacePtr surface(
        SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32));
    if (!surface)
      throw std::runtime_error(SDL_GetError());
    for (unsigned y = 0; y < image.height; ++y)
      std::memcpy(static_cast<unsigned char *>(surface->pixels) +
                      y * surface->pitch,
                  image.rgba.data() + y * image.width * 4, image.width * 4);
    return surface;
  } catch (const std::exception &e) {
    SDL_Log("Warning: Failed to load surface %s: %s", id.c_str(), e.what());
    Kotonoha::SurfacePtr fallback(
        SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
    if (fallback)
      SDL_FillSurfaceRect(fallback.get(), nullptr,
                          SDL_MapSurfaceRGB(fallback.get(), 0, 0, 0));
    return fallback;
  }
}
Kotonoha::AudioClip
RealLiveAssets::loadAudioAsset(const std::string &id) const {
  try {
    const auto ext = canonical(std::filesystem::path(id).extension().string());
    if (ext == ".NWA") {
      return Kotonoha::MediaDecoder::decodeAudio(nwaToWav(read(id)));
    }
    if (ext == ".OGG" || ext == ".WAV" || ext == ".FLAC" || ext == ".MP3") {
      return AssetProvider::loadAudioAsset(id);
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Warning: Unsupported audio format for %s (ext: %s)",
                id.c_str(), ext.c_str());
    return Kotonoha::AudioClip();
  } catch (const std::exception &e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Warning: Failed to load audio %s: %s", id.c_str(), e.what());
    return Kotonoha::AudioClip();
  }
}
} // namespace RealLive
