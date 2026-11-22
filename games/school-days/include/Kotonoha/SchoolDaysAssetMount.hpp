#pragma once

#include <Kotonoha/SchoolDaysGpkArchive.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Kotonoha {

class AssetStream;

// Configure language-specific script archives before opening the retail pack
// mount. Locale "en" MUST leave scriptGpk empty: its HQ scripts always come
// from the original Packs/Script.GPK; when enableLxh is true, an optional
// en-lxh.GPK changes only the seven epilogue ORS. Non-English locales always
// require their certified base Script.GPK, and require their matching LxH GPK
// only when enableLxh is true. Raw zh-CN is incompatible and rejected pending
// a separately certified conversion. No GPK is modified: the 29 retail
// archives and optional Packs/LxH.GPK remain authoritative.
struct SchoolDaysTranslatedGpkSelection {
  std::string locale = "en";
  std::filesystem::path scriptGpk;
  std::filesystem::path lxhGpk;
  bool enableLxh = true;
};

enum class SchoolDaysLxhMountMode {
  Required, // Strict mode: fail mount if Packs/LxH.GPK is missing or invalid.
  Optional, // Mount Packs/LxH.GPK if valid; otherwise mount 29 retail packs only.
  Disabled  // Never mount Packs/LxH.GPK; pure 29 original retail GPKs.
};

struct SchoolDaysLxhMountStatus {
  bool requested = false;
  bool mounted = false;
  std::string notice;
};

// Rejects changes while mounted; call before MountSchoolDaysRetailGpks.
// Archive structure, name compatibility and content readability are validated
// again by MountSchoolDaysRetailGpks against the actual retail installation.
bool ConfigureSchoolDaysTranslatedGpks(
    const SchoolDaysTranslatedGpkSelection &selection,
    std::string *error = nullptr);
// Restores the default English/retail selection. Call only after unmounting.
bool ClearSchoolDaysTranslatedGpks(std::string *error = nullptr);

// Process-wide School Days retail resource mount.  KTRF has one outer game
// session at a time, while media decoders may read concurrently; the
// implementation publishes immutable stores only after all 29 retail packs
// (and Packs/LxH.GPK when enabled/valid) have validated successfully.
bool MountSchoolDaysRetailGpks(
    const std::filesystem::path &gameOrPacksRoot, std::string *error = nullptr,
    std::shared_ptr<const Filesystem::IMount> source = {},
    SchoolDaysLxhMountMode lxhMode = SchoolDaysLxhMountMode::Optional,
    SchoolDaysLxhMountStatus *lxhStatus = nullptr);
// Shared engine overlay; IDs use their complete logical path for every pack.
bool SetSchoolDaysAssetOverlay(const std::filesystem::path& directory,
                                 std::string* error = nullptr);
bool SchoolDaysAssetOverlaySet();
bool ResolveSchoolDaysOverlayFile(std::string_view logicalPath,
                                         std::string* physical);
void UnmountSchoolDaysRetailGpks();
void InstallMountedSchoolDaysAssetResolver();
void UninstallMountedSchoolDaysAssetResolver();
bool SchoolDaysRetailGpksMounted();
bool SchoolDaysLxhMounted();
size_t MountedSchoolDaysGpkEntryCount();

SchoolDaysGpkLookupStatus LookupMountedSchoolDaysAsset(
    std::string_view logicalPath, bool ignoreOverlay = false);
bool ReadMountedSchoolDaysAsset(std::string_view logicalPath,
                               std::vector<uint8_t>* out,
                               std::string* error = nullptr,
                               bool ignoreOverlay = false);
// Opens raw (uncompressed) GPK entries as direct seekable streams. Compressed
// entries intentionally return null so callers can use the normal decoded
// memory path instead.
std::unique_ptr<AssetStream> OpenMountedSchoolDaysAssetStream(
    std::string_view logicalPath, bool ignoreOverlay = false);

// Existing render/audio/video plumbing carries string references.  A mounted
// GPK resource is represented by an engine-private URI rather than materialized
// as a temporary file.  Consumers must resolve it through ReadSchoolDaysAssetRef
// (or the FFmpeg input bridge), never pass it to the host filesystem.
constexpr std::string_view kSchoolDaysGpkUriPrefix = "sdgpk://";
std::string MakeSchoolDaysGpkUri(std::string_view logicalPath);
bool IsSchoolDaysGpkUri(std::string_view reference);
bool SchoolDaysLogicalFromGpkUri(std::string_view reference,
                                std::string* logicalPath);
bool ReadSchoolDaysAssetRef(std::string_view reference,
                            std::vector<uint8_t>* out,
                            std::string* error = nullptr);

} // namespace Kotonoha
