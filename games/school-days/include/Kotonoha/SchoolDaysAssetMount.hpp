#pragma once

#include <Kotonoha/SchoolDaysGpkArchive.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Kotonoha {

// Process-wide School Days retail resource mount.  KTRF has one outer game
// session at a time, while media decoders may read concurrently; the
// implementation publishes an immutable shared store only after all 29 retail
// packs have validated successfully.
bool MountSchoolDaysRetailGpks(const std::filesystem::path& gameOrPacksRoot,
                              std::string* error = nullptr);
void UnmountSchoolDaysRetailGpks();
void InstallMountedSchoolDaysAssetResolver();
void UninstallMountedSchoolDaysAssetResolver();
bool SchoolDaysRetailGpksMounted();
size_t MountedSchoolDaysGpkEntryCount();

SchoolDaysGpkLookupStatus LookupMountedSchoolDaysAsset(
    std::string_view logicalPath);
bool ReadMountedSchoolDaysAsset(std::string_view logicalPath,
                               std::vector<uint8_t>* out,
                               std::string* error = nullptr);

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
