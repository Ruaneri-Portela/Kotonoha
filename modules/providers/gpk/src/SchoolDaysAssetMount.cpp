#include <Kotonoha/SchoolDaysAssetMount.hpp>
// Retail GPK mount shared by the provider and KTRF adapters.
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/core/AssetProvider.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>

namespace Kotonoha {
namespace {

std::mutex gMountMutex;
std::shared_ptr<const SchoolDaysGpkAssetStore> gMount;
std::shared_ptr<const AssetOverlay> gOverlay;

std::shared_ptr<const SchoolDaysGpkAssetStore> CurrentMount() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  return gMount;
}

class MountedRawAssetStream final : public AssetStream {
  std::shared_ptr<const SchoolDaysGpkAssetStore> mount_;
  std::ifstream file_;
  std::vector<uint8_t> prefix_;
  uint64_t bodyOffset_ = 0u;
  uint64_t logicalSize_ = 0u;
  uint64_t position_ = 0u;

public:
  MountedRawAssetStream(std::shared_ptr<const SchoolDaysGpkAssetStore> mount,
                        const std::filesystem::path &path,
                        std::vector<uint8_t> prefix, uint64_t bodyOffset,
                        uint64_t logicalSize)
      : mount_(std::move(mount)), file_(path, std::ios::binary),
        prefix_(std::move(prefix)), bodyOffset_(bodyOffset),
        logicalSize_(logicalSize) {
    if (!file_)
      throw std::runtime_error(
          "School Days GPK raw stream could not open archive");
  }

  uint64_t size() const override { return logicalSize_; }
  uint64_t tell() const override { return position_; }

  std::size_t read(Uint8 *buffer, std::size_t size) override {
    if (size == 0u || position_ >= logicalSize_)
      return 0u;
    if (buffer == nullptr)
      throw std::invalid_argument("School Days GPK raw stream buffer is null");
    const uint64_t requested = std::min<uint64_t>(static_cast<uint64_t>(size),
                                                  logicalSize_ - position_);
    uint64_t done = 0u;
    if (position_ < prefix_.size()) {
      const uint64_t prefixBytes = std::min<uint64_t>(
          requested, prefix_.size() - static_cast<std::size_t>(position_));
      std::memcpy(buffer, prefix_.data() + static_cast<std::size_t>(position_),
                  static_cast<std::size_t>(prefixBytes));
      position_ += prefixBytes;
      done += prefixBytes;
    }
    const uint64_t remaining = requested - done;
    if (remaining == 0u)
      return static_cast<std::size_t>(done);
    const uint64_t bodyPosition = position_ - prefix_.size();
    const uint64_t physical = bodyOffset_ + bodyPosition;
    if (physical >
            static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        remaining > static_cast<uint64_t>(
                        std::numeric_limits<std::streamsize>::max())) {
      throw std::runtime_error(
          "School Days GPK raw stream range exceeds host I/O limits");
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(physical), std::ios::beg);
    if (!file_)
      throw std::runtime_error("School Days GPK raw stream seek failed");
    file_.read(reinterpret_cast<char *>(buffer + done),
               static_cast<std::streamsize>(remaining));
    const std::streamsize got = file_.gcount();
    if (got <= 0)
      throw std::runtime_error("School Days GPK raw stream read failed");
    position_ += static_cast<uint64_t>(got);
    done += static_cast<uint64_t>(got);
    return static_cast<std::size_t>(done);
  }

  bool seek(uint64_t position) override {
    if (position > logicalSize_)
      return false;
    position_ = position;
    return true;
  }
};

std::shared_ptr<const AssetOverlay> CurrentOverlay() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  return gOverlay;
}

// Translated scripts name resources with their original Kotonoha-side layout
// ("Se/Se00/...", "Music/Bgm/...", "Music/Vocal/..."); the retail packs expose
// them as "Se00/...", "BGM/SD_BGM/..." and "BGM/VOCAL/...".
std::string CanonicalGpkLogical(std::string_view logicalPath) {
  std::string path(logicalPath);
  for (char &ch : path)
    if (ch == '\\')
      ch = '/';
  const auto startsWith = [&path](std::string_view prefix) {
    if (path.size() < prefix.size())
      return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(path[i])) !=
          std::tolower(static_cast<unsigned char>(prefix[i])))
        return false;
    }
    return true;
  };
  if (startsWith("Music/Bgm/"))
    return "BGM/SD_BGM/" + path.substr(10);
  if (startsWith("Music/Vocal/"))
    return "BGM/VOCAL/" + path.substr(12);
  if (startsWith("Se/Se"))
    return path.substr(3);
  return path;
}

bool Fail(std::string *error, const char *text) {
  if (error != nullptr)
    *error = text != nullptr ? text : "School Days asset error";
  return false;
}

SchoolDaysAssetResolutionStatus
MountedResolverLookup(const std::string &logicalPath, std::string *reference) {
  if (reference != nullptr)
    reference->clear();
  std::string physical;
  if (ResolveSchoolDaysOverlayFile(logicalPath, &physical)) {
    if (reference != nullptr)
      *reference = physical;
    return SchoolDaysAssetResolutionStatus::Present;
  }
  const auto status = LookupMountedSchoolDaysAsset(logicalPath, true);
  switch (status) {
  case SchoolDaysGpkLookupStatus::Present:
    if (reference != nullptr)
      *reference = MakeSchoolDaysGpkUri(CanonicalGpkLogical(logicalPath));
    return SchoolDaysAssetResolutionStatus::Present;
  case SchoolDaysGpkLookupStatus::Missing:
    return SchoolDaysAssetResolutionStatus::Missing;
  case SchoolDaysGpkLookupStatus::Collision:
    return SchoolDaysAssetResolutionStatus::Collision;
  case SchoolDaysGpkLookupStatus::Invalid:
    return SchoolDaysAssetResolutionStatus::Invalid;
  }
  return SchoolDaysAssetResolutionStatus::Invalid;
}

bool MountedResolverIsReference(const std::string &reference) {
  return IsSchoolDaysGpkUri(reference);
}

bool MountedResolverRead(const std::string &reference,
                         std::vector<uint8_t> *bytes, std::string *error) {
  std::string logical;
  if (!SchoolDaysLogicalFromGpkUri(reference, &logical))
    return Fail(error, "School Days GPK reference is invalid");
  return ReadMountedSchoolDaysAsset(logical, bytes, error, true);
}

bool MountedResolverListDirectory(const std::string &directoryReference,
                                  std::vector<std::string> *children,
                                  std::string *error) {
  if (children == nullptr)
    return Fail(error, "School Days GPK directory output is null");
  children->clear();
  std::string logical;
  if (!SchoolDaysLogicalFromGpkUri(directoryReference, &logical))
    return Fail(error, "School Days GPK directory reference is invalid");
  const auto mount = CurrentMount();
  if (mount == nullptr)
    return Fail(error, "School Days retail GPKs are not mounted");
  std::vector<std::string> logicalChildren;
  const auto status = mount->ListDirectory(logical, &logicalChildren);
  if (status != SchoolDaysGpkLookupStatus::Present) {
    if (error != nullptr) {
      switch (status) {
      case SchoolDaysGpkLookupStatus::Missing:
        *error = "School Days GPK directory is missing";
        break;
      case SchoolDaysGpkLookupStatus::Collision:
        *error = "School Days GPK directory has a case-insensitive collision";
        break;
      default:
        *error = "School Days GPK directory is invalid";
        break;
      }
    }
    return false;
  }
  children->reserve(logicalChildren.size());
  for (const auto &child : logicalChildren)
    children->push_back(MakeSchoolDaysGpkUri(child));
  return true;
}

bool ReadFilesystem(std::string_view reference, std::vector<uint8_t> *out,
                    std::string *error) {
  if (out == nullptr)
    return Fail(error, "School Days asset output is null");
  out->clear();
  std::ifstream file(std::filesystem::path(std::string(reference)),
                     std::ios::binary | std::ios::ate);
  if (!file)
    return Fail(error, "School Days asset file could not be opened");
  const std::streamoff end = file.tellg();
  if (end < 0 ||
      static_cast<uintmax_t>(end) >
          static_cast<uintmax_t>(std::numeric_limits<size_t>::max()) ||
      static_cast<uintmax_t>(end) >
          static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    return Fail(error, "School Days asset file exceeds host read range");
  }
  try {
    out->resize(static_cast<size_t>(end));
  } catch (...) {
    return Fail(error, "School Days asset buffer allocation failed");
  }
  file.seekg(0, std::ios::beg);
  if (!out->empty() && !file.read(reinterpret_cast<char *>(out->data()),
                                  static_cast<std::streamsize>(out->size()))) {
    out->clear();
    return Fail(error, "School Days asset file read failed");
  }
  return true;
}

} // namespace

bool MountSchoolDaysRetailGpks(const std::filesystem::path &gameOrPacksRoot,
                               std::string *error) {
  if (error != nullptr)
    error->clear();
  std::filesystem::path packs = gameOrPacksRoot;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(packs / "BGM.GPK", ec)) {
    ec.clear();
    packs /= "Packs";
  }

  auto candidate = std::make_shared<SchoolDaysGpkAssetStore>();
  if (!candidate->OpenRetailDirectory(packs, error))
    return false;
  {
    std::lock_guard<std::mutex> guard(gMountMutex);
    gMount = std::move(candidate);
  }
  return true;
}

bool SetSchoolDaysAssetOverlay(const std::filesystem::path &directory,
                               std::string *error) {
  try {
    auto mounted = std::make_shared<AssetOverlay>(directory);
    std::lock_guard<std::mutex> guard(gMountMutex);
    gOverlay = std::move(mounted);
    return true;
  } catch (const std::exception &e) {
    return Fail(error, e.what());
  }
}
bool SchoolDaysAssetOverlaySet() {
  auto overlay = CurrentOverlay();
  return overlay && !overlay->root().empty();
}
bool ResolveSchoolDaysOverlayFile(std::string_view logical,
                                  std::string *physical) {
  auto overlay = CurrentOverlay();
  if (!overlay)
    return false;
  auto file = overlay->resolve(std::string(logical));
  // Compatibility names are mapped onto the same indexed engine overlay;
  // there is no separate filesystem script mount.
  if (file.empty()) {
    auto key = AssetOverlay::key(std::string(logical));
    const std::string prefix = "script/english/";
    if (key.compare(0, prefix.size(), prefix) == 0) {
      // Translated scripts shipped in the overlay may live under
      // Script/ENGLISH/<n>/<name>.PT.txt while the game looks up
      // Script/English/<name>.ENG.ORS. Match case-insensitively by bare file
      // name, trying both .ENG.ORS and .ORS targets.
      std::string bare = std::filesystem::path(key).filename().generic_string();
      // Strip every extension (e.g. 04-SA-E05.PT.ENG.ORS -> 04-SA-E05.PT).
      // Guard against stem() being a no-op on names such as ".DS_Store".
      while (bare.find('.') != std::string::npos) {
        const auto next = std::filesystem::path(bare).stem().generic_string();
        if (next == bare)
          break;
        bare = next;
      }

      for (const auto &candidate : overlay->list("")) {
        std::filesystem::path cp(candidate);
        std::string cname = cp.filename().generic_string();
        while (cname.find('.') != std::string::npos) {
          const auto next =
              std::filesystem::path(cname).stem().generic_string();
          if (next == cname)
            break;
          cname = next;
        }
        if (cname == bare) {
          file = overlay->resolve(candidate);
          if (!file.empty())
            break;
        }
      }
    }
  }

  if (file.empty())
    return false;
  if (physical)
    *physical = std::move(file);
  return true;
}

void UnmountSchoolDaysRetailGpks() {
  std::lock_guard<std::mutex> guard(gMountMutex);
  gMount.reset();
}

void InstallMountedSchoolDaysAssetResolver() {
  InstallSchoolDaysVirtualAssetLookup(&MountedResolverLookup);
  InstallSchoolDaysVirtualAssetIo(&MountedResolverIsReference,
                                  &MountedResolverRead,
                                  &MountedResolverListDirectory);
}

void UninstallMountedSchoolDaysAssetResolver() {
  if (gSchoolDaysVirtualAssetLookup == &MountedResolverLookup) {
    InstallSchoolDaysVirtualAssetLookup(nullptr);
    InstallSchoolDaysVirtualAssetIo(nullptr, nullptr, nullptr);
  }
}

bool SchoolDaysRetailGpksMounted() {
  const auto mount = CurrentMount();
  return mount != nullptr && mount->IsOpen();
}

size_t MountedSchoolDaysGpkEntryCount() {
  const auto mount = CurrentMount();
  return mount != nullptr ? mount->EntryCount() : 0u;
}

SchoolDaysGpkLookupStatus
LookupMountedSchoolDaysAsset(std::string_view logicalPath, bool ignoreOverlay) {
  if (!ignoreOverlay && ResolveSchoolDaysOverlayFile(logicalPath, nullptr))
    return SchoolDaysGpkLookupStatus::Present;
  const auto mount = CurrentMount();
  return mount != nullptr ? mount->Lookup(CanonicalGpkLogical(logicalPath))
                          : SchoolDaysGpkLookupStatus::Missing;
}

bool ReadMountedSchoolDaysAsset(std::string_view logicalPath,
                                std::vector<uint8_t> *out, std::string *error,
                                bool ignoreOverlay) {
  std::string physical;
  if (!ignoreOverlay && ResolveSchoolDaysOverlayFile(logicalPath, &physical))
    return ReadFilesystem(physical, out, error);
  const auto mount = CurrentMount();
  if (mount == nullptr)
    return Fail(error, "School Days retail GPKs are not mounted");
  return mount->Read(CanonicalGpkLogical(logicalPath), out, error);
}

std::unique_ptr<AssetStream>
OpenMountedSchoolDaysAssetStream(std::string_view logicalPath,
                                 bool ignoreOverlay) {
  if (!ignoreOverlay && ResolveSchoolDaysOverlayFile(logicalPath, nullptr))
    return {};
  const auto mount = CurrentMount();
  if (mount == nullptr)
    return {};
  size_t archiveIndex = 0u;
  size_t entryIndex = 0u;
  const std::string canonical = CanonicalGpkLogical(logicalPath);
  if (mount->Lookup(canonical, &archiveIndex, &entryIndex) !=
      SchoolDaysGpkLookupStatus::Present) {
    return {};
  }
  const SchoolDaysGpkArchive *archive = mount->Archive(archiveIndex);
  if (archive == nullptr || entryIndex >= archive->Index().Entries().size())
    return {};
  const auto &entry = archive->Index().Entries()[entryIndex];
  if (!entry.IsRaw())
    return {};
  const uint64_t bodySize = static_cast<uint64_t>(entry.PhysicalBodyLength());
  const uint64_t physicalEnd = static_cast<uint64_t>(entry.offset) + bodySize;
  if (physicalEnd < entry.offset || physicalEnd > archive->PidxOffset() ||
      entry.inlinePrefix.size() > entry.storedLength) {
    return {};
  }
  try {
    return std::make_unique<MountedRawAssetStream>(
        mount, archive->Path(), entry.inlinePrefix, entry.offset,
        static_cast<uint64_t>(entry.storedLength));
  } catch (...) {
    return {};
  }
}

std::string MakeSchoolDaysGpkUri(std::string_view logicalPath) {
  std::string result(kSchoolDaysGpkUriPrefix);
  result.reserve(result.size() + logicalPath.size());
  for (char ch : logicalPath)
    result.push_back(ch == '\\' ? '/' : ch);
  return result;
}

bool IsSchoolDaysGpkUri(std::string_view reference) {
  return reference.size() > kSchoolDaysGpkUriPrefix.size() &&
         reference.substr(0u, kSchoolDaysGpkUriPrefix.size()) ==
             kSchoolDaysGpkUriPrefix;
}

bool SchoolDaysLogicalFromGpkUri(std::string_view reference,
                                 std::string *logicalPath) {
  if (logicalPath == nullptr || !IsSchoolDaysGpkUri(reference))
    return false;
  logicalPath->assign(reference.substr(kSchoolDaysGpkUriPrefix.size()));
  return !logicalPath->empty();
}

bool ReadSchoolDaysAssetRef(std::string_view reference,
                            std::vector<uint8_t> *out, std::string *error) {
  if (error != nullptr)
    error->clear();
  std::string logical;
  if (SchoolDaysLogicalFromGpkUri(reference, &logical))
    return ReadMountedSchoolDaysAsset(logical, out, error, true);
  return ReadFilesystem(reference, out, error);
}

} // namespace Kotonoha
