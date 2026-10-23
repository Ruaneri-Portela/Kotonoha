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
// Optional loose-file directory that overrides the Script pack.  A logical
// path "Script/English/<dir>/<name>.ENG.ORS" is looked up as
// <directory>/<dir>/<name>*.txt (the English language directory is dropped, the
// extension is always .txt, and the stem is the name up to its first '.');
// when nothing matches it falls back to the mounted GPK.  Other packs are never
// read from it.  An empty path clears the override; hits resolve to real
// filesystem paths.
bool SetSchoolDaysScriptDirectory(const std::filesystem::path& directory,
                                 std::string* error = nullptr);
bool SchoolDaysScriptDirectorySet();
bool ResolveSchoolDaysScriptDirectoryFile(std::string_view logicalPath,
                                         std::string* physical);
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
