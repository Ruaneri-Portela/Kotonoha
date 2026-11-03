#include "AirAssets.hpp"
#include <Air/Images.hpp>
#include <Air/Voice.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <cstring>
#include <stdexcept>
namespace Air {
AirAssets::AirAssets(const std::filesystem::path &directory,
                     const std::filesystem::path &overlay)
    : IndexedFileSystemAssetProvider(directory), config("") {
  setOverlay(overlay);
  const auto ini = read("Gameexe.ini");
  configSource.assign(ini.begin(), ini.end());
  config = Config(configSource);
  if (!contains("Seen.txt"))
    throw std::runtime_error("AIR Seen.txt not found");
}
std::string AirAssets::graphic(const std::string &name) const {
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
  throw std::runtime_error("AIR graphic missing: " + name);
}
Kotonoha::SurfacePtr AirAssets::loadSurfaceAsset(const std::string &id) const {
  const auto extension =
      canonical(std::filesystem::path(id).extension().string());
  if (extension != ".G00" && extension != ".PDT")
    return AssetProvider::loadSurfaceAsset(id);
  const auto image =
      extension == ".PDT" ? decodePDT(read(id)) : decodeG00(read(id));
  Kotonoha::SurfacePtr surface(
      SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    throw std::runtime_error(SDL_GetError());
  for (unsigned y = 0; y < image.height; ++y)
    std::memcpy(static_cast<unsigned char *>(surface->pixels) +
                    y * surface->pitch,
                image.rgba.data() + y * image.width * 4, image.width * 4);
  return surface;
}
Kotonoha::AudioClip AirAssets::loadAudioAsset(const std::string &id) const {
  if (canonical(std::filesystem::path(id).extension().string()) == ".NWA")
    return Kotonoha::MediaDecoder::decodeAudio(nwaToWav(read(id)));
  return AssetProvider::loadAudioAsset(id);
}
} // namespace Air
