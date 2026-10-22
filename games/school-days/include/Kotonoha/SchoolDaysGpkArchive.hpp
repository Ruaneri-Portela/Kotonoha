#pragma once

#include <Kotonoha/SchoolDaysGpkIndex.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Kotonoha {

enum class SchoolDaysGpkLookupStatus {
    Present,
    Missing,
    Collision,
    Invalid,
};

struct SchoolDaysRetailGpkSpec {
    const char* filename;
    const char* logicalPrefix;
};

// Canonical v1.02 pack set.  Prefixes mirror the extracted SchoolDays asset
// root, so e.g. System.gpk:BACKLOG/X.PNG is addressed as System/BACKLOG/X.PNG.
const std::array<SchoolDaysRetailGpkSpec, 29>& SchoolDaysRetailGpkSpecs();

class SchoolDaysGpkArchive final {
public:
    bool Open(const std::filesystem::path& path, std::string* error = nullptr);
    void Close();

    bool IsOpen() const { return !path_.empty(); }
    const std::filesystem::path& Path() const { return path_; }
    size_t EntryCount() const { return index_.Entries().size(); }
    uint64_t PidxOffset() const { return pidxOffset_; }
    const SchoolDaysGpkIndex& Index() const { return index_; }

    SchoolDaysGpkLookupStatus Lookup(std::string_view archiveRelativePath,
                                    size_t* entryIndex = nullptr) const;
    // Lists direct children using canonical PIDX spelling.  An empty relative
    // directory lists the archive root.  Ambiguous folded child names fail closed.
    SchoolDaysGpkLookupStatus ListDirectory(
        std::string_view archiveRelativeDirectory,
        std::vector<std::string>* relativeChildren) const;
    bool Read(std::string_view archiveRelativePath,
              std::vector<uint8_t>* out,
              std::string* error = nullptr) const;
    bool ReadEntry(size_t entryIndex, std::vector<uint8_t>* out,
                   std::string* error = nullptr) const;

private:
    std::filesystem::path path_;
    uint64_t pidxOffset_ = 0;
    SchoolDaysGpkIndex index_;
    std::vector<std::string> canonicalEntries_;
    std::unordered_map<std::string, size_t> foldedEntries_;
    std::unordered_set<std::string> foldedCollisions_;
};

class SchoolDaysGpkAssetStore final {
public:
    // Opens the authenticated 29-pack v1.02 set from the supplied Packs folder.
    // The operation is atomic: on any missing/invalid pack the store stays closed.
    bool OpenRetailDirectory(const std::filesystem::path& packDirectory,
                             std::string* error = nullptr);
    void Close();

    bool IsOpen() const { return archives_.size() == SchoolDaysRetailGpkSpecs().size(); }
    size_t ArchiveCount() const { return archives_.size(); }
    size_t EntryCount() const { return entryCount_; }

    SchoolDaysGpkLookupStatus Lookup(std::string_view logicalPath,
                                    size_t* archiveIndex = nullptr,
                                    size_t* entryIndex = nullptr) const;
    // Returns canonical full logical paths for direct children, e.g.
    // System/BACKLOG/BACKLOG_HORIZON.CMAP.
    SchoolDaysGpkLookupStatus ListDirectory(
        std::string_view logicalDirectory,
        std::vector<std::string>* logicalChildren) const;
    bool Read(std::string_view logicalPath, std::vector<uint8_t>* out,
              std::string* error = nullptr) const;

    const SchoolDaysGpkArchive* Archive(size_t index) const {
        return index < archives_.size() ? &archives_[index] : nullptr;
    }

private:
    std::filesystem::path packDirectory_;
    std::vector<SchoolDaysGpkArchive> archives_;
    std::unordered_map<std::string, size_t> foldedPrefixes_;
    size_t entryCount_ = 0;
};

} // namespace Kotonoha
