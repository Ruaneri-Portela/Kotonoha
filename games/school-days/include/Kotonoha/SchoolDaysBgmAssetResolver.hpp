#pragma once

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <string>

namespace Kotonoha {

inline SchoolDaysImageResolution ResolveSchoolDaysMediaAsset(
        const char* assetRoot, const std::string& logical) {
    return ResolveSchoolDaysAssetPath(assetRoot, logical);
}

/* Compatibility wrapper for existing audio call sites. Present resolves to the
   physical spelling. Missing preserves the full requested candidate so decoder
   failure remains non-fatal. Collision/Invalid fail closed with an empty path. */
inline std::string ResolveSchoolDaysBgmAsset(const char* assetRoot,
                                              const std::string& logical) {
    const auto result = ResolveSchoolDaysMediaAsset(assetRoot, logical);
    if (result.status == SchoolDaysAssetResolutionStatus::Present ||
        result.status == SchoolDaysAssetResolutionStatus::Missing) {
        return result.physical;
    }
    return {};
}

} // namespace Kotonoha
