#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

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

namespace Kotonoha {
namespace {

std::mutex gMountMutex;
std::shared_ptr<const SchoolDaysGpkAssetStore> gMount;
std::filesystem::path gScriptDirectory;

std::shared_ptr<const SchoolDaysGpkAssetStore> CurrentMount() {
    std::lock_guard<std::mutex> guard(gMountMutex);
    return gMount;
}

std::filesystem::path CurrentScriptDirectory() {
    std::lock_guard<std::mutex> guard(gMountMutex);
    return gScriptDirectory;
}

// Translated scripts name resources with their original Kotonoha-side layout
// ("Se/Se00/...", "Music/Bgm/...", "Music/Vocal/..."); the retail packs expose them as
// "Se00/...", "BGM/SD_BGM/..." and "BGM/VOCAL/...".
std::string CanonicalGpkLogical(std::string_view logicalPath) {
    std::string path(logicalPath);
    for (char& ch : path) if (ch == '\\') ch = '/';
    const auto startsWith = [&path](std::string_view prefix) {
        if (path.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(path[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        }
        return true;
    };
    if (startsWith("Music/Bgm/")) return "BGM/SD_BGM/" + path.substr(10);
    if (startsWith("Music/Vocal/")) return "BGM/VOCAL/" + path.substr(12);
    if (startsWith("Se/Se")) return path.substr(3);
    return path;
}

bool Fail(std::string* error, const char* text) {
    if (error != nullptr) *error = text != nullptr ? text : "School Days asset error";
    return false;
}

SchoolDaysAssetResolutionStatus MountedResolverLookup(
    const std::string& logicalPath, std::string* reference) {
    if (reference != nullptr) reference->clear();
    std::string physical;
    if (ResolveSchoolDaysScriptDirectoryFile(logicalPath, &physical)) {
        if (reference != nullptr) *reference = physical;
        return SchoolDaysAssetResolutionStatus::Present;
    }
    const auto status = LookupMountedSchoolDaysAsset(logicalPath);
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

bool MountedResolverIsReference(const std::string& reference) {
    return IsSchoolDaysGpkUri(reference);
}

bool MountedResolverRead(const std::string& reference,
                         std::vector<uint8_t>* bytes,
                         std::string* error) {
    std::string logical;
    if (!SchoolDaysLogicalFromGpkUri(reference, &logical))
        return Fail(error, "School Days GPK reference is invalid");
    return ReadMountedSchoolDaysAsset(logical, bytes, error);
}

bool MountedResolverListDirectory(const std::string& directoryReference,
                                  std::vector<std::string>* children,
                                  std::string* error) {
    if (children == nullptr) return Fail(error, "School Days GPK directory output is null");
    children->clear();
    std::string logical;
    if (!SchoolDaysLogicalFromGpkUri(directoryReference, &logical))
        return Fail(error, "School Days GPK directory reference is invalid");
    const auto mount = CurrentMount();
    if (mount == nullptr) return Fail(error, "School Days retail GPKs are not mounted");
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
    for (const auto& child : logicalChildren) children->push_back(MakeSchoolDaysGpkUri(child));
    return true;
}

bool ReadFilesystem(std::string_view reference, std::vector<uint8_t>* out,
                    std::string* error) {
    if (out == nullptr) return Fail(error, "School Days asset output is null");
    out->clear();
    std::ifstream file(std::filesystem::path(std::string(reference)),
                       std::ios::binary | std::ios::ate);
    if (!file) return Fail(error, "School Days asset file could not be opened");
    const std::streamoff end = file.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) >
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
    if (!out->empty() &&
        !file.read(reinterpret_cast<char*>(out->data()),
                   static_cast<std::streamsize>(out->size()))) {
        out->clear();
        return Fail(error, "School Days asset file read failed");
    }
    return true;
}

} // namespace

bool MountSchoolDaysRetailGpks(const std::filesystem::path& gameOrPacksRoot,
                              std::string* error) {
    if (error != nullptr) error->clear();
    std::filesystem::path packs = gameOrPacksRoot;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(packs / "BGM.GPK", ec)) {
        ec.clear();
        packs /= "Packs";
    }

    auto candidate = std::make_shared<SchoolDaysGpkAssetStore>();
    if (!candidate->OpenRetailDirectory(packs, error)) return false;
    {
        std::lock_guard<std::mutex> guard(gMountMutex);
        gMount = std::move(candidate);
    }
    return true;
}

bool SetSchoolDaysScriptDirectory(const std::filesystem::path& directory,
                                 std::string* error) {
    if (error != nullptr) error->clear();
    std::error_code ec;
    std::filesystem::path canonical;
    if (!directory.empty()) {
        if (!std::filesystem::is_directory(directory, ec))
            return Fail(error, "School Days script directory does not exist");
        canonical = std::filesystem::weakly_canonical(directory, ec);
        if (ec) canonical = directory;
    }
    std::lock_guard<std::mutex> guard(gMountMutex);
    gScriptDirectory = std::move(canonical);
    return true;
}

bool SchoolDaysScriptDirectorySet() {
    return !CurrentScriptDirectory().empty();
}

bool ResolveSchoolDaysScriptDirectoryFile(std::string_view logicalPath,
                                         std::string* physical) {
    const auto root = CurrentScriptDirectory();
    if (root.empty() || logicalPath.empty()) return false;
    std::string normalized(logicalPath);
    for (char& ch : normalized) if (ch == '\\') ch = '/';
    // Only the Script pack can be overridden: "Script/<rest>" maps to <dir>/<rest>.
    constexpr std::string_view kScriptPrefix = "Script/";
    if (normalized.size() <= kScriptPrefix.size()) return false;
    for (size_t i = 0; i < kScriptPrefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(normalized[i])) !=
            std::tolower(static_cast<unsigned char>(kScriptPrefix[i])))
            return false;
    }
    normalized.erase(0, kScriptPrefix.size());
    // The language directory is not part of the loose layout.
    constexpr std::string_view kLanguage = "English/";
    if (normalized.size() > kLanguage.size()) {
        bool language = true;
        for (size_t i = 0; i < kLanguage.size() && language; ++i) {
            language = std::tolower(static_cast<unsigned char>(normalized[i])) ==
                       std::tolower(static_cast<unsigned char>(kLanguage[i]));
        }
        if (language) normalized.erase(0, kLanguage.size());
    }
    const std::filesystem::path relative(normalized);
    if (relative.is_absolute() || relative.has_root_name()) return false;
    for (const auto& part : relative) {
        if (part == "..") return false;
    }

    // Loose scripts are always "<stem>*.txt": 00/00-00-A00.ENG.ORS opens the
    // first 00/00-00-A00*.txt (the stem is the name up to its first '.').
    const std::string fileName = relative.filename().string();
    const std::string stem = fileName.substr(0, fileName.find('.'));
    if (stem.empty()) return false;
    const auto directory = (root / relative).parent_path();
    const auto lower = [](std::string text) {
        for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return text;
    };
    const std::string stemLower = lower(stem);
    std::error_code ec;
    std::string best;
    for (std::filesystem::directory_iterator it(directory, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        const std::string nameLower = lower(name);
        if (nameLower.size() < stemLower.size() + 4u ||
            nameLower.compare(0, stemLower.size(), stemLower) != 0 ||
            nameLower.compare(nameLower.size() - 4u, 4u, ".txt") != 0)
            continue;
        if (best.empty() || name < best) best = name;
    }
    if (best.empty()) return false;
    if (physical != nullptr) *physical = (directory / best).string();
    return true;
}

void UnmountSchoolDaysRetailGpks() {
    std::lock_guard<std::mutex> guard(gMountMutex);
    gMount.reset();
}

void InstallMountedSchoolDaysAssetResolver() {
    InstallSchoolDaysVirtualAssetLookup(&MountedResolverLookup);
    InstallSchoolDaysVirtualAssetIo(&MountedResolverIsReference, &MountedResolverRead,
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

SchoolDaysGpkLookupStatus LookupMountedSchoolDaysAsset(
    std::string_view logicalPath) {
    if (ResolveSchoolDaysScriptDirectoryFile(logicalPath, nullptr))
        return SchoolDaysGpkLookupStatus::Present;
    const auto mount = CurrentMount();
    return mount != nullptr ? mount->Lookup(CanonicalGpkLogical(logicalPath))
                            : SchoolDaysGpkLookupStatus::Missing;
}

bool ReadMountedSchoolDaysAsset(std::string_view logicalPath,
                               std::vector<uint8_t>* out,
                               std::string* error) {
    std::string physical;
    if (ResolveSchoolDaysScriptDirectoryFile(logicalPath, &physical))
        return ReadFilesystem(physical, out, error);
    const auto mount = CurrentMount();
    if (mount == nullptr) return Fail(error, "School Days retail GPKs are not mounted");
    return mount->Read(CanonicalGpkLogical(logicalPath), out, error);
}

std::string MakeSchoolDaysGpkUri(std::string_view logicalPath) {
    std::string result(kSchoolDaysGpkUriPrefix);
    result.reserve(result.size() + logicalPath.size());
    for (char ch : logicalPath) result.push_back(ch == '\\' ? '/' : ch);
    return result;
}

bool IsSchoolDaysGpkUri(std::string_view reference) {
    return reference.size() > kSchoolDaysGpkUriPrefix.size() &&
           reference.substr(0u, kSchoolDaysGpkUriPrefix.size()) ==
               kSchoolDaysGpkUriPrefix;
}

bool SchoolDaysLogicalFromGpkUri(std::string_view reference,
                                std::string* logicalPath) {
    if (logicalPath == nullptr || !IsSchoolDaysGpkUri(reference)) return false;
    logicalPath->assign(reference.substr(kSchoolDaysGpkUriPrefix.size()));
    return !logicalPath->empty();
}

bool ReadSchoolDaysAssetRef(std::string_view reference,
                            std::vector<uint8_t>* out,
                            std::string* error) {
    if (error != nullptr) error->clear();
    std::string logical;
    if (SchoolDaysLogicalFromGpkUri(reference, &logical))
        return ReadMountedSchoolDaysAsset(logical, out, error);
    return ReadFilesystem(reference, out, error);
}

} // namespace Kotonoha
