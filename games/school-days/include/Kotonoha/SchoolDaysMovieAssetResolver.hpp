#pragma once

#include <Kotonoha/SchoolDaysBgmAssetResolver.hpp>
#include <string>

namespace Kotonoha {

/* Both ORS commands use MovieEvent and the same PATH.wmv resource rule.
   Reuse the existing component-wise case resolver for Android filesystems. */
inline std::string ResolveSchoolDaysMovieAsset(const char* assetRoot,
                                               const std::string& resource) {
    return ResolveSchoolDaysBgmAsset(assetRoot, resource);
}

} // namespace Kotonoha
