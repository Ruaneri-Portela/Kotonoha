#include <Kotonoha/Filesystem/Mounts/LayeredMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>

#include <algorithm>
#include <map>
#include <stdexcept>

namespace Kotonoha::Filesystem {
LayeredMount::LayeredMount(std::shared_ptr<IMount> base) {
  if (!base)
    throw std::invalid_argument("A base mount is required");
  layers_.push_back({std::move(base), 0, sequence_++, false});
}

void LayeredMount::addOverlay(std::shared_ptr<IMount> overlay, int priority) {
  if (!overlay)
    throw std::invalid_argument("Overlay mount is null");
  layers_.push_back({std::move(overlay), priority, sequence_++, true});
  std::stable_sort(layers_.begin(), layers_.end(),
                   [](const Layer &a, const Layer &b) {
                     return a.priority != b.priority ? a.priority > b.priority
                                                      : a.order > b.order;
                   });
  ++generation_;
}

bool LayeredMount::blocked(std::size_t index,
                           const std::string &path) const {
  for (auto pos = path.find('/'); pos != std::string::npos;
       pos = path.find('/', pos + 1)) {
    const auto ancestor = path.substr(0, pos);
    for (std::size_t higher = 0; higher <= index; ++higher) {
      // Asking exists() for every parent used to dispatch HTTP HEAD
      // requests for "data", "data/pack", etc. With Asyncify each missing
      // directory could delay the game boot for up to 30 seconds.
      // Only shadow a child with a *known* higher regular file.
      if (layers_[higher].mount->isKnownRegularFile(ancestor))
        return true;
      if (layers_[higher].mount->isKnownDirectory(ancestor))
        break; // A higher visible directory permits child layers.
      // Unknown remote ancestors cannot trigger a synchronous HEAD request.
    }
  }
  return false;
}

const LayeredMount::Layer *
LayeredMount::visible(const std::string &path, bool overlayOnly) const {
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    const auto &layer = layers_[i];
    if (overlayOnly && !layer.overlay)
      continue;
    if (blocked(i, path))
      continue;
    if (layer.mount->exists(path))
      return &layer;
  }
  return nullptr;
}

bool LayeredMount::isKnownRegularFile(std::string_view path) const {
  const auto key = normalizePath(path, true);
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    const auto &mount = layers_[i].mount;
    if (blocked(i, key)) continue;
    if (mount->isKnownRegularFile(key)) return true;
    if (mount->isKnownDirectory(key)) return false;
  }
  return false;
}
bool LayeredMount::isKnownDirectory(std::string_view path) const {
  const auto key = normalizePath(path, true);
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    const auto &mount = layers_[i].mount;
    if (blocked(i, key)) continue;
    if (mount->isKnownRegularFile(key)) return false;
    if (mount->isKnownDirectory(key)) return true;
  }
  return false;
}
bool LayeredMount::exists(std::string_view path) const {
  return visible(normalizePath(path, true), false) != nullptr;
}

FileInfo LayeredMount::stat(std::string_view path) const {
  const auto name = normalizePath(path, true);
  const auto *layer = visible(name, false);
  if (!layer)
    throw FilesystemError(ErrorCode::NotFound, "Virtual file not found: " + name);
  return layer->mount->stat(name);
}

std::vector<FileInfo> LayeredMount::list(std::string_view directory) const {
  const auto dir = normalizePath(directory, true);
  const auto node = stat(dir);
  if (node.type != FileType::Directory)
    throw FilesystemError(ErrorCode::NotDirectory,
                          "Not a virtual directory: " + dir);

  std::map<std::string, FileInfo> merged;
  for (const auto &layer : layers_) {
    if (!layer.mount->exists(dir) ||
        layer.mount->stat(dir).type != FileType::Directory)
      continue;
    for (const auto &entry : layer.mount->list(dir)) {
      if (visible(normalizePath(entry.path), false) != &layer)
        continue;
      merged.emplace(lookupKey(entry.path), entry);
    }
  }
  std::vector<FileInfo> entries;
  entries.reserve(merged.size());
  for (const auto &[key, file] : merged)
    entries.push_back(file);
  return entries;
}

std::unique_ptr<FileHandle> LayeredMount::open(std::string_view path) const {
  const auto name = normalizePath(path);
  const auto *layer = visible(name, false);
  if (!layer)
    throw FilesystemError(ErrorCode::NotFound, "Virtual file not found: " + name);
  return layer->mount->open(name);
}

std::unique_ptr<FileHandle>
LayeredMount::tryOpenLogicalOverride(std::string_view path) const {
  const auto name = normalizePath(path);
  const auto *layer = visible(name, true);
  if (!layer)
    return {};
  return layer->mount->open(name);
}

} // namespace Kotonoha::Filesystem
