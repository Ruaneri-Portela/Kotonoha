#include <SummerRadish/SummerAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <cstring>
#include <algorithm>
#include <utility>
#include <optional>
#include <stdexcept>
namespace Kotonoha::Providers {
using namespace SummerRadish;
namespace {
std::shared_ptr<Kotonoha::Filesystem::Filesystem>
mountSummer(std::filesystem::path root, std::filesystem::path overlay) {
  // A PACK subfolder is a legacy CLI input: mount its parent so
  // movies and sibling sources remain visible as virtual files.
  std::error_code ec;
  if (std::filesystem::is_directory(root, ec) &&
      canonical(root.filename().string()) == "PACK")
    root = root.parent_path();
  auto source = Kotonoha::Filesystem::Filesystem::mount(root.string());
  if (!overlay.empty())
    source->addOverlay(overlay.string());
  return source;
}
} // namespace
SummerAssets::SummerAssets(std::filesystem::path root,
                           std::filesystem::path overlay)
    : SummerAssets(mountSummer(root, overlay)) {
  // The game still uses a physical media path in legacy MediaPlayer calls.
  // Package sources themselves are already opened through the V2 filesystem.
  if (source_->exists("DISC1/PACK") &&
      source_->stat("DISC1/PACK").type ==
          Kotonoha::Filesystem::FileType::Directory) {
    discDirectory = std::filesystem::absolute(root) / "DISC1";
  } else if (source_->exists("PACK") &&
             source_->stat("PACK").type ==
                 Kotonoha::Filesystem::FileType::Directory) {
    discDirectory = std::filesystem::absolute(root);
  } else {
    discDirectory = std::filesystem::absolute(root).parent_path();
  }
}

SummerAssets::SummerAssets(
    std::shared_ptr<Kotonoha::Filesystem::Filesystem> mounted)
    : source_(std::move(mounted)) {
  if (!source_)
    throw std::invalid_argument("Summer CMP mount is missing");
  std::string packRoot;
  const auto remote = Kotonoha::Filesystem::remoteGameAssets();
  if (remote && remote->manifestFree()) {
    // An ordinary HTTP server cannot enumerate PACK directories. Probe the
    // known CMP archive names explicitly, including the supported disc
    // layouts, so the provider can register their virtual parent directories.
    // This also avoids treating an HTTP 404 for /DISC1/PACK as proof that its
    // children do not exist (many servers disable directory listings).
    bool found = false;
    for (const char *candidate : {"DISC1/PACK", "PACK", ""}) {
      const std::string prefix = *candidate ? std::string(candidate) + "/" : "";
      if (source_->exists(prefix + "INI.CMP")) {
        packRoot = candidate;
        found = true;
        break;
      }
    }
    if (found) {
      const auto prefix = packRoot.empty() ? "" : packRoot + "/";
      for (const char *type : {"SCRIPT", "GRAPHIC", "SOUND", "VOICE"}) {
        const std::string archive = prefix + type + ".CMP";
        // A named request (rather than list()) resolves metadata on demand.
        (void)source_->exists(archive);
      }
    }
  } else {
    if (source_->exists("DISC1/PACK") &&
        source_->stat("DISC1/PACK").type ==
            Kotonoha::Filesystem::FileType::Directory)
      packRoot = "DISC1/PACK";
    else if (source_->exists("PACK") &&
             source_->stat("PACK").type ==
                 Kotonoha::Filesystem::FileType::Directory)
      packRoot = "PACK";
  }

  const auto files = source_->list(packRoot);
  for (const auto *type : {"INI", "SCRIPT", "GRAPHIC", "SOUND", "VOICE"}) {
    const auto desired = std::string(type) + ".CMP";
    auto entry = std::find_if(files.begin(), files.end(), [&](const auto &file) {
      return file.type == Kotonoha::Filesystem::FileType::Regular &&
             canonical(std::filesystem::path(file.path).filename().string()) ==
                 desired;
    });
    if (entry == files.end())
      throw std::runtime_error("Missing " + desired + " in virtual source");
    packs.emplace(type, std::make_unique<Pack>(source_, entry->path));
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
std::unique_ptr<Kotonoha::Filesystem::FileHandle>
SummerAssets::movieHandle(const std::string &filename) const {
  const auto name = Kotonoha::Filesystem::normalizePath(filename);
  std::vector<std::string> candidates;
  const bool discLayout = source_->exists("DISC1/PACK");
  const auto prefix = discLayout ? std::string("DISC1/") : std::string();
  candidates.push_back(name);
  if (name.size() >= 6 && canonical(name.substr(0, 6)) == "DISC1/")
    candidates.push_back(name.substr(6));
  else if (!prefix.empty())
    candidates.push_back(prefix + name);

  // Keep the known release alias used by the original moviePath().
  for (const auto &candidate : std::vector<std::string>(candidates)) {
    if (canonical(std::filesystem::path(candidate).filename().string()) ==
        "OPENING.MPG") {
      auto path = std::filesystem::path(candidate);
      path.replace_filename("SUMMER_OP.MPG");
      candidates.push_back(path.generic_string());
    }
  }
  for (const auto &candidate : candidates) {
    if (source_->exists(candidate) &&
        source_->stat(candidate).type ==
            Kotonoha::Filesystem::FileType::Regular)
      return source_->open(candidate);
  }
  throw std::runtime_error("Missing Summer virtual movie: " + filename);
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
