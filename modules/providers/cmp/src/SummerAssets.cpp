#include <SummerRadish/SummerAssets.hpp>
#include <cstring>
#include <optional>
#include <stdexcept>
namespace Kotonoha::Providers {
using namespace SummerRadish;
SummerAssets::SummerAssets(std::filesystem::path root) {
  if (std::filesystem::is_directory(root / "DISC1/PACK"))
    root /= "DISC1/PACK";
  else if (std::filesystem::is_directory(root / "PACK"))
    root /= "PACK";
  discDirectory = std::filesystem::absolute(root).parent_path();
  for (auto type : {"INI", "SCRIPT", "GRAPHIC", "SOUND", "VOICE"}) {
    std::filesystem::path found;
    for (auto &f : std::filesystem::directory_iterator(root))
      if (canonical(f.path().filename().string()) == std::string(type) + ".CMP")
        found = f.path();
    if (found.empty())
      throw std::runtime_error("Missing " + std::string(type) + ".cmp in " +
                               root.string());
    packs.emplace(type, std::make_unique<Pack>(found));
  }
}
std::filesystem::path
SummerAssets::moviePath(const std::string &filename) const {
  const std::filesystem::path requested(filename);
  if (requested.empty() || requested.is_absolute())
    throw std::runtime_error("Invalid Summer movie filename: " + filename);

  const auto findMovie = [](const std::filesystem::path &path)
      -> std::filesystem::path {
    const auto directory = path.parent_path();
    if (!std::filesystem::is_directory(directory))
      return {};
    const auto wanted = canonical(path.filename().string());
    for (const auto &entry : std::filesystem::directory_iterator(directory))
      if (entry.is_regular_file() &&
          canonical(entry.path().filename().string()) == wanted)
        return entry.path();
    return {};
  };

  // 1. Direct attempt
  auto target = (discDirectory / requested).lexically_normal();
  if (auto found = findMovie(target); !found.empty())
    return found;

  // 2. If the candidate starts with the name of the discDirectory, strip it.
  // e.g. discDirectory is ".../DISC1", filename is "DISC1/movie.mpg"
  auto discName = discDirectory.filename();
  auto it = requested.begin();
  if (it != requested.end() && it->string() == discName.string()) {
    std::filesystem::path subPath;
    auto next_it = std::next(it);
    for (; next_it != requested.end(); ++next_it) {
      subPath /= *next_it;
    }
    target = (discDirectory / subPath).lexically_normal();
    if (auto found = findMovie(target); !found.empty())
      return found;
  }

  // The first disc calls its opening summer_op.mpg while some scripts ask
  // for opening.mpg. Resolve this known edition alias in the same directory.
  if (canonical(target.filename().string()) == "OPENING.MPG") {
    target.replace_filename("summer_op.mpg");
    if (auto found = findMovie(target); !found.empty())
      return found;
  }

  throw std::runtime_error("Missing Summer movie: " + filename);
}
bool SummerAssets::containsAsset(const std::string &id) const {
  auto s = canonical(id);
  auto slash = s.find('/');
  if (slash == s.npos)
    return false;
  auto pack = packs.find(s.substr(0, slash));
  return pack != packs.end() && pack->second->contains(s.substr(slash + 1));
}
Bytes SummerAssets::readAsset(const std::string &id) const {
  auto s = canonical(id);
  auto slash = s.find('/');
  auto pack = packs.find(s.substr(0, slash));
  if (slash == s.npos || pack == packs.end())
    throw std::runtime_error("Unknown Summer asset: " + id);
  return pack->second->read(s.substr(slash + 1));
}

std::optional<Bytes> SummerAssets::tryReadAsset(const std::string &id) const {
  if (!containsAsset(id))
    return std::nullopt;
  try {
    return readAsset(id);
  } catch (...) {
    return std::nullopt;
  }
}
std::vector<std::string>
SummerAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> result;
  auto search = canonical(prefix);
  for (auto &[type, pack] : packs)
    for (auto &[n, e] : pack->entries()) {
      auto id = type + "/" + n;
      if (id.compare(0, search.size(), search) == 0)
        result.push_back(id);
    }
  return result;
}
Image SummerAssets::picture(const std::string &id) const {
  return decodeImage(read(id), id.substr(id.find_last_of('.') + 1));
}
Kotonoha::SurfacePtr
SummerAssets::loadSurfaceAsset(const std::string &id) const {
  auto img = picture(id);
  Kotonoha::SurfacePtr s(
      SDL_CreateSurface(img.width, img.height, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    throw std::runtime_error(SDL_GetError());
  for (int y = 0; y < img.height; ++y)
    std::memcpy(static_cast<uint8_t *>(s->pixels) + y * s->pitch,
                img.rgba.data() + size_t(y) * img.width * 4, img.width * 4);
  return s;
}
} // namespace Kotonoha::Providers
