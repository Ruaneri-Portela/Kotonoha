#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/Filesystem/Mounts/ISO9660Mount.hpp>
#include <Kotonoha/Filesystem/Mounts/TarMount.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>

namespace Kotonoha::Filesystem {
namespace {

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F')
    return 10 + (ch - 'A');
  return -1;
}

std::string percentDecode(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const int hi = hexValue(input[i + 1]);
      const int lo = hexValue(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

std::string normalizeFileUrl(std::string source) {
  constexpr std::string_view kFileScheme = "file://";
  if (source.rfind(kFileScheme, 0) != 0)
    return source;
  std::string_view rest(source);
  rest.remove_prefix(kFileScheme.size());
  constexpr std::string_view kLocalhost = "localhost/";
  if (rest.rfind(kLocalhost, 0) == 0)
    rest.remove_prefix(kLocalhost.size() - 1);
  return percentDecode(rest);
}

} // namespace

Filesystem::Filesystem(std::shared_ptr<IMount> base)
    : layers_(std::make_shared<LayeredMount>(std::move(base))) {}

std::shared_ptr<Filesystem> Filesystem::mount(std::string source) {
  if (source.empty())
    throw FilesystemError(ErrorCode::InvalidPath, "Mount source is empty");

  if (source.rfind("file://", 0) == 0)
    source = normalizeFileUrl(std::move(source));

  if (source.rfind("content://", 0) == 0) {
#ifdef __ANDROID__
    // On Android, SDL_IOFromFile natively opens single-file content:// URIs
    // (such as .iso/.bin/.img picked via ACTION_OPEN_DOCUMENT).
    try {
      auto base = std::make_shared<ISO9660Mount>(std::filesystem::path(source));
      return std::shared_ptr<Filesystem>(new Filesystem(std::move(base)));
    } catch (const FilesystemError &) {
      throw FilesystemError(
          ErrorCode::UnsupportedMount,
          "Android SAF content:// directory tree requires AndroidSafMount: " +
              source);
    }
#else
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "content:// mounts are available only on Android");
#endif
  }

  // Explicit HTTP(S) source: use the configured provider when it matches
  // (sharing its cache), otherwise create a direct on-demand Fetch mount.
  // Do not treat URL components named game-assets as physical directories.
  if (source.find("://") != std::string::npos) {
#ifdef __EMSCRIPTEN__
    if (source.rfind("https://", 0) != 0 && source.rfind("http://", 0) != 0)
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Only HTTP(S) direct mounts are supported");
    if (auto remote = remoteGameAssets())
      if (source == remote->baseUrl() || source + "/" == remote->baseUrl())
        return mount(std::static_pointer_cast<IMount>(remote));
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "HTTP(S) sources require a non-empty asset manifest");
#else
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "HTTP mounts are available only on Emscripten");
#endif
  }
#ifdef __EMSCRIPTEN__
  if (remoteGameAssets()) {
    const std::filesystem::path path(source);
    for (const auto &part : path)
      if (part == "game-assets")
        return mountGameAssets(path);
  }
#endif
  std::error_code ec;
  const auto root = std::filesystem::canonical(source, ec);
  if (ec)
    throw FilesystemError(ErrorCode::NotFound,
                          "Mount source not found: " + source);
  std::shared_ptr<IMount> base;
  if (std::filesystem::is_directory(root)) {
    base = std::make_shared<DirectoryMount>(root);
  } else if (std::filesystem::is_regular_file(root)) {
    auto ext = root.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    if (ext == ".tar")
      base = std::make_shared<TarMount>(root);
    else if (ext == ".iso" || ext == ".bin" || ext == ".img")
      base = std::make_shared<ISO9660Mount>(root);
    else
      throw FilesystemError(ErrorCode::UnsupportedMount,
                            "Unsupported mount format: " + root.string());
  } else {
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "Not a mountable source: " + source);
  }
  return std::shared_ptr<Filesystem>(new Filesystem(std::move(base)));
}

std::shared_ptr<Filesystem> Filesystem::mountForDetection(std::string source) {
  // Avoid building an entire recursive directory index just to look for
  // well-known game files. Disc images, URLs and SAF retain their own mounts.
  if (source.rfind("file://", 0) == 0)
    source = normalizeFileUrl(std::move(source));
  std::error_code ec;
  if (std::filesystem::is_directory(std::filesystem::path(source), ec)) {
    return mount(
        std::static_pointer_cast<IMount>(std::make_shared<DirectoryMount>(
            std::filesystem::path(source), false)));
  }
  return mount(std::move(source));
}

std::shared_ptr<Filesystem> Filesystem::mount(std::shared_ptr<IMount> source) {
  return std::shared_ptr<Filesystem>(new Filesystem(std::move(source)));
}

void Filesystem::addOverlay(std::shared_ptr<IMount> source, int priority) {
  layers_->addOverlay(std::move(source), priority);
}

void Filesystem::addOverlay(std::string source, int priority) {
  // Reuse the same source selection as a top-level mount: browser-supplied
  // directory, archive (TAR/ISO), or HTTP Fetch. Keeping the nested
  // Filesystem alive through shared ownership preserves its file handles.
  auto overlay = mount(std::move(source));
  layers_->addOverlay(overlay->layers_, priority);
}

std::uint64_t Filesystem::generation() const noexcept {
  return layers_->generation();
}
bool Filesystem::exists(std::string_view path) const {
  return layers_->exists(path);
}
FileInfo Filesystem::stat(std::string_view path) const {
  return layers_->stat(path);
}
std::vector<FileInfo> Filesystem::list(std::string_view directory) const {
  return layers_->list(directory);
}
std::unique_ptr<FileHandle> Filesystem::open(std::string_view path) const {
  return layers_->open(path);
}
std::unique_ptr<FileHandle>
Filesystem::tryOpenLogicalOverride(std::string_view id) const {
  return layers_->tryOpenLogicalOverride(id);
}

std::vector<std::uint8_t> readAll(FileHandle &file, std::size_t maxBytes) {
  const auto size = file.size();
  if (size < 0 || static_cast<std::uint64_t>(size) > maxBytes)
    throw FilesystemError(ErrorCode::IOError,
                          "Unknown or excessive resource size");
  if (file.seek(0, SeekOrigin::Begin) < 0)
    throw FilesystemError(ErrorCode::IOError, "Cannot rewind resource");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::size_t cursor = 0;
  while (cursor < bytes.size()) {
    const auto chunk =
        std::min<std::size_t>(bytes.size() - cursor, 1024u * 1024u);
    const auto count = file.read(bytes.data() + cursor, chunk);
    if (count <= 0)
      throw FilesystemError(ErrorCode::IOError,
                            count == 0 ? "Unexpected end of resource"
                                       : "Resource read failed");
    cursor += static_cast<std::size_t>(count);
  }
  return bytes;
}

std::vector<std::uint8_t> readLocalFile(const std::filesystem::path &path,
                                        std::size_t maxBytes) {
  SDLFileSource input(path);
  return readAll(input, maxBytes);
}

} // namespace Kotonoha::Filesystem
