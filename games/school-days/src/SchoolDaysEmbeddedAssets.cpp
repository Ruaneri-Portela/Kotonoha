#include "SchoolDaysEmbeddedAssets.hpp"

#if defined(KOTONOHA_SCHOOL_DAYS_EMBEDDED_ASSETS)
#include "SchoolDaysEmbeddedAssets.generated.hpp"
#include <SDL3/SDL.h>
#include <zlib.h>

#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#endif

namespace SchoolDays {
#if defined(KOTONOHA_SCHOOL_DAYS_EMBEDDED_ASSETS)
namespace {
namespace fs = std::filesystem;

fs::path privateSession() {
  // SDL chooses the platform's application-private writable location. Use a
  // separate identity, never the game save/Documents directory or install.
  char *raw = SDL_GetPrefPath("Kotonoha", "school-days-embedded-assets");
  if (!raw)
    throw std::runtime_error("Cannot locate private embedded asset storage");
  const fs::path parent(raw);
  SDL_free(raw);
  const auto canonicalParent = fs::canonical(parent);
  std::random_device entropy;
  for (int attempt = 0; attempt < 32; ++attempt) {
    const auto token = std::to_string(SDL_GetPerformanceCounter()) + "-" +
                       std::to_string(entropy()) + "-" +
                       std::to_string(entropy());
    const auto session = canonicalParent / ("session-" + token);
    std::error_code error;
    if (fs::create_directory(session, error)) {
#ifndef _WIN32
      fs::permissions(session, fs::perms::owner_all, fs::perm_options::replace,
                      error);
      if (error) {
        std::error_code ignored;
        fs::remove(session, ignored);
        throw fs::filesystem_error("Cannot protect private asset session",
                                   session, error);
      }
#endif
      return session;
    }
    if (error)
      throw fs::filesystem_error("Cannot create private asset session", session,
                                 error);
  }
  throw std::runtime_error("Cannot allocate private embedded asset session");
}

fs::path validatedRelative(const char *logical) {
  const std::string name(logical);
  if (name.empty() || name.find_first_of("\\:") != std::string::npos)
    throw std::runtime_error("Invalid embedded asset path");
  const fs::path relative(name);
  if (relative.is_absolute() || relative.has_root_path())
    throw std::runtime_error("Absolute embedded asset path");
  for (const auto &part : relative)
    if (part.empty() || part == "." || part == "..")
      throw std::runtime_error("Unsafe embedded asset path");
  return relative;
}

std::vector<unsigned char>
decompress(const Kotonoha::EmbeddedAssetsData::Resource &resource) {
  if (resource.size == 0 || resource.size > (256u << 20) ||
      resource.compressedSize > (std::numeric_limits<uLong>::max)())
    throw std::runtime_error("Invalid embedded asset record");
  std::vector<unsigned char> bytes(resource.size);
  uLongf size = static_cast<uLongf>(bytes.size());
  if (uncompress(bytes.data(), &size, resource.data,
                 static_cast<uLong>(resource.compressedSize)) != Z_OK ||
      size != bytes.size())
    throw std::runtime_error(std::string("Cannot decompress embedded asset: ") +
                             resource.logical);
  return bytes;
}
} // namespace
#endif

std::vector<unsigned char> ReadEmbeddedAsset(std::string_view logical) {
#if defined(KOTONOHA_SCHOOL_DAYS_EMBEDDED_ASSETS)
  for (const auto &resource : Kotonoha::EmbeddedAssetsData::resources)
    if (logical == resource.logical)
      return decompress(resource);
#else
  (void)logical;
#endif
  return {};
}

EmbeddedAssets::EmbeddedAssets() {
#if defined(KOTONOHA_SCHOOL_DAYS_EMBEDDED_ASSETS)
  session_ = privateSession();
  try {
    const auto root = session_ / "assets";
    std::filesystem::create_directory(root);
    std::set<std::filesystem::path> written;
    for (const auto &resource : Kotonoha::EmbeddedAssetsData::resources) {
      const auto relative = validatedRelative(resource.logical);
      if (!written.insert(relative).second)
        throw std::runtime_error("Invalid embedded asset record");
      const auto bytes = decompress(resource);
      const auto destination = root / relative;
      std::filesystem::create_directories(destination.parent_path());
      // The directory was exclusively created by this process. No preexisting
      // files are replaced. All callers see only a fully populated root.
      std::ofstream output(destination, std::ios::binary);
      output.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
      output.close();
      if (!output)
        throw std::runtime_error("Cannot materialize embedded asset: " +
                                 relative.generic_string());
    }
    registration_ = std::make_unique<Kotonoha::ScopedEngineAssetRoot>(root);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(session_, ignored);
    throw;
  }
#endif
}

EmbeddedAssets::~EmbeddedAssets() {
  registration_.reset();
  if (!session_.empty()) {
    std::error_code ignored;
    std::filesystem::remove_all(session_, ignored);
  }
}

} // namespace SchoolDays
