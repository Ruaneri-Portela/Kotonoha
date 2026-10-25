#pragma once

#include <Kotonoha/AssetProvider.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace Kotonoha {

enum class SchoolDaysAssetResolutionStatus {
    Present,
    Missing,
    Collision,
    Invalid,
};

// Optional virtual-resource lookup installed by the School Days runtime.  It
// returns the same status vocabulary as the filesystem resolver and, for a
// Present resource, an engine-private reference suitable for downstream media
// loaders.  Keeping this hook header-only preserves all standalone model tests:
// they continue to exercise pure filesystem resolution unless the runtime
// explicitly mounts a provider.
using SchoolDaysVirtualAssetLookup = SchoolDaysAssetResolutionStatus (*)(
    const std::string& logicalPath, std::string* reference);
using SchoolDaysVirtualAssetIsReference = bool (*)(const std::string& reference);
using SchoolDaysVirtualAssetRead = bool (*)(const std::string& reference,
                                            std::vector<uint8_t>* bytes,
                                            std::string* error);
using SchoolDaysVirtualAssetListDirectory = bool (*)(
    const std::string& directoryReference, std::vector<std::string>* children,
    std::string* error);
inline SchoolDaysVirtualAssetLookup gSchoolDaysVirtualAssetLookup = nullptr;
inline SchoolDaysVirtualAssetIsReference gSchoolDaysVirtualAssetIsReference = nullptr;
inline SchoolDaysVirtualAssetRead gSchoolDaysVirtualAssetRead = nullptr;
inline SchoolDaysVirtualAssetListDirectory gSchoolDaysVirtualAssetListDirectory = nullptr;

inline void InstallSchoolDaysVirtualAssetLookup(SchoolDaysVirtualAssetLookup lookup) {
    gSchoolDaysVirtualAssetLookup = lookup;
}

inline void InstallSchoolDaysVirtualAssetIo(
    SchoolDaysVirtualAssetIsReference isReference,
    SchoolDaysVirtualAssetRead read,
    SchoolDaysVirtualAssetListDirectory listDirectory) {
    gSchoolDaysVirtualAssetIsReference = isReference;
    gSchoolDaysVirtualAssetRead = read;
    gSchoolDaysVirtualAssetListDirectory = listDirectory;
}

inline bool SchoolDaysAssetReferenceIsVirtual(const std::string& reference) {
    return gSchoolDaysVirtualAssetIsReference != nullptr &&
           gSchoolDaysVirtualAssetIsReference(reference);
}

inline bool ReadSchoolDaysAssetReference(const std::string& reference,
                                         std::vector<uint8_t>* bytes,
                                         std::string* error = nullptr) {
    if (error != nullptr) error->clear();
    if (bytes == nullptr || reference.empty()) {
        if (error != nullptr) *error = "School Days asset read argument is invalid";
        return false;
    }
    bytes->clear();
    if (SchoolDaysAssetReferenceIsVirtual(reference)) {
        if (gSchoolDaysVirtualAssetRead == nullptr) {
            if (error != nullptr) *error = "School Days virtual asset reader is unavailable";
            return false;
        }
        return gSchoolDaysVirtualAssetRead(reference, bytes, error);
    }
    std::ifstream file(std::filesystem::path(reference), std::ios::binary | std::ios::ate);
    if (!file) {
        if (error != nullptr) *error = "School Days asset file could not be opened";
        return false;
    }
    const std::streamoff end = file.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) >
                       static_cast<uintmax_t>(std::numeric_limits<size_t>::max()) ||
        static_cast<uintmax_t>(end) >
                       static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        if (error != nullptr) *error = "School Days asset file exceeds host read range";
        return false;
    }
    try {
        bytes->resize(static_cast<size_t>(end));
    } catch (...) {
        if (error != nullptr) *error = "School Days asset allocation failed";
        return false;
    }
    file.seekg(0, std::ios::beg);
    if (!bytes->empty() &&
        !file.read(reinterpret_cast<char*>(bytes->data()),
                   static_cast<std::streamsize>(bytes->size()))) {
        bytes->clear();
        if (error != nullptr) *error = "School Days asset file read failed";
        return false;
    }
    return true;
}

inline bool ListSchoolDaysAssetDirectoryReference(
    const std::string& directoryReference, std::vector<std::string>* children,
    std::string* error = nullptr) {
    if (children == nullptr || !SchoolDaysAssetReferenceIsVirtual(directoryReference) ||
        gSchoolDaysVirtualAssetListDirectory == nullptr) {
        return false;
    }
    return gSchoolDaysVirtualAssetListDirectory(directoryReference, children, error);
}

class SchoolDaysImageAssetProvider final : public AssetProvider {
public:
    bool IsVirtualReference(const std::string& reference) const override {
        return SchoolDaysAssetReferenceIsVirtual(reference);
    }

    bool ReadFile(const std::string& reference,
                  std::vector<uint8_t>* bytes,
                  std::string* error) const override {
        return ReadSchoolDaysAssetReference(reference, bytes, error);
    }

    bool ListDirectory(const std::string& reference,
                       std::vector<std::string>* children,
                       std::string* error) const override {
        return ListSchoolDaysAssetDirectoryReference(reference, children, error);
    }
};

inline const AssetProvider* SchoolDaysImageAssetProviderInstance() {
    static const SchoolDaysImageAssetProvider provider;
    return &provider;
}

inline void InstallSchoolDaysImageAssetProvider() {
    SetAssetProvider(SchoolDaysImageAssetProviderInstance());
}

inline void UninstallSchoolDaysImageAssetProvider() {
    if (GetAssetProvider() == SchoolDaysImageAssetProviderInstance()) {
        SetAssetProvider(nullptr);
    }
}

struct SchoolDaysImageResolution {
    std::string logical;
    std::string physical;
    SchoolDaysAssetResolutionStatus status = SchoolDaysAssetResolutionStatus::Invalid;
    bool present = false;
    bool collision = false;
};

struct SchoolDaysAssetRootSelection {
    const char* root = "";
    bool available = false;
    bool dedicated = false;
};

inline SchoolDaysAssetRootSelection SelectSchoolDaysAssetRoot(
        const char* dedicatedRoot, const char* genericRoot) {
    if (dedicatedRoot != nullptr && dedicatedRoot[0] != '\0')
        return {dedicatedRoot, true, true};
    if (genericRoot != nullptr && genericRoot[0] != '\0')
        return {genericRoot, true, false};
    return {"", false, false};
}

inline bool SchoolDaysAssetLogicalPathIsAbsolute(const std::string& logical) {
    if (logical.empty()) return false;
    if (logical.front() == '/' || logical.front() == '\\') return true;
    if (logical.size() >= 7 &&
        (logical.rfind("http://", 0) == 0 || logical.rfind("https://", 0) == 0 ||
         logical.rfind("HTTP://", 0) == 0 || logical.rfind("HTTPS://", 0) == 0)) {
        return true;
    }
    return logical.size() >= 2u &&
           std::isalpha(static_cast<unsigned char>(logical[0])) != 0 &&
           logical[1] == ':';
}

inline void SchoolDaysAppendPhysicalComponent(std::string* path,
                                              const std::string& component) {
    if (path == nullptr || component.empty()) return;
    if (!path->empty() && path->back() != '/' && path->back() != '\\')
        *path += '/';
    *path += component;
}

inline bool SchoolDaysAsciiCaseEqual(const std::string& a,
                                     const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

inline SchoolDaysAssetResolutionStatus SchoolDaysSelectUniqueFoldedMatch(
        std::vector<std::string> matches, std::string* selected) {
    if (selected != nullptr) selected->clear();
    if (matches.empty()) return SchoolDaysAssetResolutionStatus::Missing;
    std::sort(matches.begin(), matches.end());
    if (matches.size() != 1u)
        return SchoolDaysAssetResolutionStatus::Collision;
    if (selected != nullptr) *selected = std::move(matches.front());
    return SchoolDaysAssetResolutionStatus::Present;
}

/*
 * Resolve one logical School Days filesystem asset below assetRoot.
 *
 * Retail GPK lookup is case-insensitive; the shipped 29-pack/69,936-entry
 * corpus contains no case-fold collisions.  On a modern extracted filesystem
 * we therefore require exactly one folded match per component and fail closed
 * on an ambiguous mount rather than choosing directory-enumeration order.
 *
 * Missing is deliberately distinct from Invalid/Collision.  The returned
 * physical string remains the full requested candidate path for Missing so
 * optional audio/movie callers can preserve their native non-blocking
 * "decoder fails, timeline continues" behavior without inventing a fallback.
 */
inline SchoolDaysImageResolution ResolveSchoolDaysAssetPath(
        const char* assetRoot, const std::string& logicalPath) {
    SchoolDaysImageResolution result;
    result.logical = logicalPath;
    if (logicalPath.empty())
        return result;
    if (SchoolDaysAssetLogicalPathIsAbsolute(logicalPath)) {
        // This resolver accepts logical names below the selected School Days
        // mount only.  Treat drive/UNC/URL-style logical input as invalid so an
        // authored or malformed path cannot bypass the mounted namespace.
        result.status = SchoolDaysAssetResolutionStatus::Invalid;
        return result;
    }

    if (assetRoot != nullptr && assetRoot[0] != '\0') {
        std::string root(assetRoot);
        if (root.size() >= 7 &&
            (root.rfind("http://", 0) == 0 || root.rfind("https://", 0) == 0 ||
             root.rfind("HTTP://", 0) == 0 || root.rfind("HTTPS://", 0) == 0)) {
            std::string joined = root;
            if (!joined.empty() && joined.back() != '/' && logicalPath.front() != '/') {
                joined += '/';
            } else if (!joined.empty() && joined.back() == '/' && logicalPath.front() == '/') {
                joined.pop_back();
            }
            joined += logicalPath;
            result.present = true;
            result.physical = joined;
            result.status = SchoolDaysAssetResolutionStatus::Present;
            return result;
        }
    }

    // Validate the complete logical path before touching the filesystem.  This
    // matters after a missing prefix: optional media still returns a candidate
    // path, but that candidate must never be able to smuggle ".." or a drive
    // component past the first failed lookup.
    std::vector<std::string> components;
    size_t pos = 0u;
    while (pos < logicalPath.size()) {
        while (pos < logicalPath.size() &&
               (logicalPath[pos] == '/' || logicalPath[pos] == '\\')) {
            ++pos;
        }
        if (pos >= logicalPath.size()) break;
        const size_t begin = pos;
        while (pos < logicalPath.size() &&
               logicalPath[pos] != '/' && logicalPath[pos] != '\\') {
            ++pos;
        }
        const std::string part = logicalPath.substr(begin, pos - begin);
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find(':') != std::string::npos) {
            result.status = SchoolDaysAssetResolutionStatus::Invalid;
            return result;
        }
        components.push_back(part);
    }
    if (components.empty()) return result;

    // Retail archives are the authoritative School Days resource backend when
    // mounted.  Missing remains eligible for the extracted-filesystem fallback
    // below, while Invalid/Collision stay fail-closed exactly like the physical
    // component resolver.
    if (gSchoolDaysVirtualAssetLookup != nullptr) {
        std::string reference;
        const auto virtualStatus = gSchoolDaysVirtualAssetLookup(logicalPath, &reference);
        if (virtualStatus == SchoolDaysAssetResolutionStatus::Present) {
            if (reference.empty()) return result;
            result.status = virtualStatus;
            result.present = true;
            result.physical = std::move(reference);
            return result;
        }
        if (virtualStatus == SchoolDaysAssetResolutionStatus::Collision) {
            result.status = virtualStatus;
            result.collision = true;
            return result;
        }
        if (virtualStatus == SchoolDaysAssetResolutionStatus::Invalid) {
            result.status = virtualStatus;
            return result;
        }
    }

    if (assetRoot == nullptr || *assetRoot == '\0') {
        // Preserve the legacy fail-closed contract when no virtual namespace is
        // installed.  With a mounted provider, Missing is meaningful and lets
        // callers explicitly fall back to an extracted ORS/resource root.
        if (gSchoolDaysVirtualAssetLookup != nullptr) {
            result.status = SchoolDaysAssetResolutionStatus::Missing;
            result.physical = logicalPath;
        }
        return result;
    }

    std::string current = assetRoot;
    {
        std::error_code rootError;
        if (!std::filesystem::is_directory(std::filesystem::path(current),
                                           rootError) || rootError) {
            result.status = SchoolDaysAssetResolutionStatus::Invalid;
            return result;
        }
    }
    for (size_t index = 0u; index < components.size(); ++index) {
        const std::string& part = components[index];
        std::error_code ec;
        std::vector<std::string> matches;
        for (std::filesystem::directory_iterator it(current, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (SchoolDaysAsciiCaseEqual(part, name))
                matches.push_back(name);
        }

        if (ec) {
            // An unreadable/non-directory component is not equivalent to a
            // cleanly absent asset.  Fail closed rather than fabricating a
            // decoder candidate through an I/O error.
            result.status = SchoolDaysAssetResolutionStatus::Invalid;
            return result;
        }
        if (matches.empty()) {
            for (size_t rest = index; rest < components.size(); ++rest)
                SchoolDaysAppendPhysicalComponent(&current, components[rest]);
            result.physical = current;
            result.status = SchoolDaysAssetResolutionStatus::Missing;
            return result;
        }

        std::string selected;
        const auto selection =
            SchoolDaysSelectUniqueFoldedMatch(std::move(matches), &selected);
        if (selection == SchoolDaysAssetResolutionStatus::Collision) {
            result.status = SchoolDaysAssetResolutionStatus::Collision;
            result.collision = true;
            return result;
        }
        SchoolDaysAppendPhysicalComponent(&current, selected);

        std::error_code typeError;
        const std::filesystem::path resolved(current);
        const bool correctType = index + 1u < components.size()
            ? std::filesystem::is_directory(resolved, typeError)
            : std::filesystem::is_regular_file(resolved, typeError);
        if (typeError || !correctType) {
            result.status = SchoolDaysAssetResolutionStatus::Invalid;
            return result;
        }
    }

    result.physical = current;
    result.status = SchoolDaysAssetResolutionStatus::Present;
    result.present = true;
    return result;
}

inline bool ResolveSchoolDaysRequiredAssetPath(
        const char* assetRoot, const std::string& logicalPath,
        std::string* physical, std::string* error, const char* resourceLabel) {
    if (physical == nullptr) {
        if (error != nullptr) *error = "School Days required asset output is null";
        return false;
    }
    const auto result = ResolveSchoolDaysAssetPath(assetRoot, logicalPath);
    if (result.status != SchoolDaysAssetResolutionStatus::Present) {
        if (error != nullptr) {
            const std::string label = resourceLabel != nullptr && *resourceLabel != '\0'
                ? resourceLabel : "asset";
            switch (result.status) {
            case SchoolDaysAssetResolutionStatus::Missing:
                *error = "School Days " + label + " is missing";
                break;
            case SchoolDaysAssetResolutionStatus::Collision:
                *error = "School Days " + label + " has a case-insensitive collision";
                break;
            case SchoolDaysAssetResolutionStatus::Invalid:
                *error = "School Days " + label + " path is invalid";
                break;
            case SchoolDaysAssetResolutionStatus::Present:
                break;
            }
        }
        return false;
    }
    *physical = result.physical;
    return true;
}

inline SchoolDaysImageResolution ResolveSchoolDaysImageAsset(
        const char* assetRoot, const std::string& logicalStem) {
    return ResolveSchoolDaysAssetPath(assetRoot, logicalStem + ".PNG");
}

} // namespace Kotonoha
