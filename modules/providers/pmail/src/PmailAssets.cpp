#include <Pmail/Assets.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/Filesystem/Path.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Kotonoha::Providers {

PmailAssets::PmailAssets(const std::filesystem::path &directory)
    : PmailAssets(Kotonoha::Filesystem::Filesystem::mount(directory.string())) {}

PmailAssets::PmailAssets(
    std::shared_ptr<Kotonoha::Filesystem::Filesystem> source)
    : source_(std::move(source)) {
  if (!source_)
    throw std::invalid_argument("PmailAssets requires a mounted source");
}

std::string PmailAssets::name() const { return "Pmail"; }

bool PmailAssets::containsAsset(const std::string &id) const {
  try {
    return source_->exists(id) &&
           source_->stat(id).type == Kotonoha::Filesystem::FileType::Regular;
  } catch (const Kotonoha::Filesystem::FilesystemError &e) {
    if (e.code() == Kotonoha::Filesystem::ErrorCode::InvalidPath ||
        e.code() == Kotonoha::Filesystem::ErrorCode::NotFound)
      return false;
    throw;
  }
}

std::vector<Uint8> PmailAssets::readAsset(const std::string &id) const {
  // Do not silently convert I/O, HTTP or malformed archive errors to EOF.
  return Kotonoha::Filesystem::readAll(*source_->open(id));
}

std::vector<std::string>
PmailAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> entries;
  std::vector<std::string> pending{""};
  const auto normalized = Kotonoha::Filesystem::lookupKey(prefix, true);
  while (!pending.empty()) {
    auto dir = std::move(pending.back());
    pending.pop_back();
    for (const auto &info : source_->list(dir)) {
      if (info.type == Kotonoha::Filesystem::FileType::Directory) {
        pending.push_back(info.path);
      } else if (Kotonoha::Filesystem::lookupKey(info.path).compare(
                     0, normalized.size(), normalized) == 0) {
        entries.push_back(info.path);
      }
    }
  }
  std::sort(entries.begin(), entries.end());
  return entries;
}

std::string PmailAssets::localPathAsset(const std::string &) const {
  // No provider consumer may depend on a physical path for virtual mounts.
  return {};
}

SurfacePtr PmailAssets::loadSurfaceAsset(const std::string &) const {
  // The original provider did not handle the CRGB format here.
  return nullptr;
}

AudioClip PmailAssets::loadAudioAsset(const std::string &id) const {
  const auto bytes = readAsset(id);
  if (bytes.empty())
    return {};
  return Kotonoha::MediaDecoder::decodeAudio(bytes);
}

} // namespace Kotonoha::Providers
