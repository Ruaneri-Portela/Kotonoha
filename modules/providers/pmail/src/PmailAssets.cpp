#include <Kotonoha/media/MediaDecoder.hpp>
#include <Pmail/Assets.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace Kotonoha::Providers {

PmailAssets::PmailAssets(const std::filesystem::path &directory)
    : root_(directory) {}

std::string PmailAssets::name() const { return "Pmail"; }

bool PmailAssets::containsAsset(const std::string &id) const {
  std::filesystem::path full_path = root_ / id;
  std::cerr << "[PmailAssets Debug] checking for: " << full_path.string()
            << std::endl;
  return std::filesystem::exists(full_path);
}

std::vector<Uint8> PmailAssets::readAsset(const std::string &id) const {
  std::filesystem::path full_path = root_ / id;
  std::cerr << "[PmailAssets Debug] reading asset: " << full_path.string()
            << std::endl;
  std::ifstream file(full_path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "[PmailAssets Debug] FAILED to open file: "
              << full_path.string() << std::endl;
    return {};
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<Uint8> buffer(size);
  if (file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    return buffer;
  }
  return {};
}

std::vector<std::string>
PmailAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> assets;
  if (!std::filesystem::exists(root_))
    return assets;

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root_)) {
    if (entry.is_regular_file()) {
      std::string rel_path =
          std::filesystem::relative(entry.path(), root_).generic_string();
      if (prefix.empty() || rel_path.compare(0, prefix.size(), prefix) == 0) {
        assets.push_back(rel_path);
      }
    }
  }
  return assets;
}

std::string PmailAssets::localPathAsset(const std::string &id) const {
  return (root_ / id).generic_string();
}

SurfacePtr PmailAssets::loadSurfaceAsset(const std::string &id) const {
  // For PMAIL, we might need to implement .CRGB decoding if it's not handled by
  // MediaDecoder For now, fall back to default.
  return nullptr;
}

AudioClip PmailAssets::loadAudioAsset(const std::string &id) const {
  const auto bytes = readAsset(id);
  if (bytes.empty())
    return {};
  return Kotonoha::MediaDecoder::decodeAudio(bytes);
}

} // namespace Kotonoha::Providers
