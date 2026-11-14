#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>
#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/Filesystem/Mounts/ISO9660Mount.hpp>
#include <Kotonoha/Filesystem/Mounts/TarMount.hpp>
#include <Kotonoha/Filesystem/Sources/SDLFileSource.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>

namespace Kotonoha::Filesystem {

Filesystem::Filesystem(std::shared_ptr<IMount> base)
    : layers_(std::make_shared<LayeredMount>(std::move(base))) {}

std::shared_ptr<Filesystem> Filesystem::mount(std::string source) {
  if (source.empty())
    throw FilesystemError(ErrorCode::InvalidPath, "Mount source is empty");
#ifdef __EMSCRIPTEN__
  if (auto remote = remoteGameAssets()) {
    const std::filesystem::path path(source);
    bool isGameAssets = false;
    for (const auto &part : path)
      if (part == "game-assets") isGameAssets = true;
    if (isGameAssets)
      return mountGameAssets(path);
    // An explicitly selected HTTP source is valid only after the async
    // manifest bootstrap has installed its exact mount.
    if (source == remote->baseUrl() ||
        source + "/" == remote->baseUrl())
      return mount(std::static_pointer_cast<IMount>(remote));
  }
#endif
  if (source.find("://") != std::string::npos)
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "HTTP mounts require a Fetch backend (not yet implemented)");
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
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) {
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

std::shared_ptr<Filesystem>
Filesystem::mount(std::shared_ptr<IMount> source) {
  return std::shared_ptr<Filesystem>(new Filesystem(std::move(source)));
}

void Filesystem::addOverlay(std::shared_ptr<IMount> source, int priority) {
  layers_->addOverlay(std::move(source), priority);
}

void Filesystem::addOverlay(std::string source, int priority) {
  if (source.find("://") != std::string::npos)
    throw FilesystemError(ErrorCode::UnsupportedMount,
                          "Remote overlay backend is not implemented");
  layers_->addOverlay(std::make_shared<DirectoryMount>(source), priority);
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
    const auto chunk = std::min<std::size_t>(bytes.size() - cursor, 1024u * 1024u);
    const auto count = file.read(bytes.data() + cursor, chunk);
    if (count <= 0)
      throw FilesystemError(ErrorCode::IOError,
                            count == 0 ? "Unexpected end of resource"
                                       : "Resource read failed");
    cursor += static_cast<std::size_t>(count);
  }
  return bytes;
}

std::vector<std::uint8_t> readLocalFile(
    const std::filesystem::path &path, std::size_t maxBytes) {
  SDLFileSource input(path);
  return readAll(input, maxBytes);
}

} // namespace Kotonoha::Filesystem
