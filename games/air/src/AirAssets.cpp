#include "AirAssets.hpp"
#include <Air/Images.hpp>
#include <Air/Voice.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace Air {
namespace {
std::string loadConfig(const std::filesystem::path &root) {
  for (auto &file : std::filesystem::directory_iterator(root))
    if (canonical(file.path().filename().string()) == "GAMEEXE.INI") {
      std::ifstream stream(file.path(), std::ios::binary);
      if (!stream)
        break;
      return std::string((std::istreambuf_iterator<char>(stream)), {});
    }
  throw std::runtime_error("Gameexe.ini not found in " + root.string());
}
} // namespace
AirAssets::AirAssets(const std::filesystem::path &directory)
    : root(std::filesystem::canonical(directory)),
      configSource(loadConfig(root)), config(configSource) {
  for (auto &entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_regular_file())
      files.emplace(
          canonical(
              std::filesystem::relative(entry.path(), root).generic_string()),
          entry.path());
  if (!contains("Seen.txt"))
    throw std::runtime_error("AIR Seen.txt not found");
}
bool AirAssets::contains(const std::string &id) const {
  return files.count(canonical(id)) != 0;
}
std::string AirAssets::localPath(const std::string &id) const {
  auto i = files.find(canonical(id));
  if (i == files.end())
    throw std::runtime_error("AIR asset missing: " + id);
  return i->second.string();
}
std::vector<Uint8> AirAssets::read(const std::string &id) const {
  std::ifstream input(localPath(id), std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot read AIR asset: " + id);
  return std::vector<Uint8>((std::istreambuf_iterator<char>(input)), {});
}
std::vector<std::string> AirAssets::list(const std::string &prefix) const {
  std::vector<std::string> result;
  const auto key = canonical(prefix);
  for (auto &[id, file] : files)
    if (id.compare(0, key.size(), key) == 0)
      result.push_back(id);
  return result;
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
Kotonoha::SurfacePtr AirAssets::loadSurface(const std::string &id) const {
  const auto extension =
      canonical(std::filesystem::path(id).extension().string());
  if (extension != ".G00" && extension != ".PDT")
    return AssetProvider::loadSurface(id);
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
Kotonoha::AudioClip AirAssets::loadAudio(const std::string &id) const {
  if (canonical(std::filesystem::path(id).extension().string()) == ".NWA")
    return Kotonoha::MediaDecoder::decodeAudio(nwaToWav(read(id)));
  return AssetProvider::loadAudio(id);
}
} // namespace Air
