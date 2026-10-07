#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <string>
#include <vector>

namespace Kotonoha {

struct SchoolDaysImageResolution {
    std::string logical;
    std::string physical;
    bool present = false;
    bool collision = false;
};

inline SchoolDaysImageResolution ResolveSchoolDaysImageAsset(
        const char* assetRoot, const std::string& logicalStem) {
    SchoolDaysImageResolution result;
    result.logical = logicalStem + ".PNG";
    std::string current = assetRoot && *assetRoot ? assetRoot : ".";
    size_t pos = 0;
    while (pos < result.logical.size()) {
        const size_t slash = result.logical.find('/', pos);
        const std::string part = result.logical.substr(pos, slash - pos);
        struct Scan { std::string part; std::vector<std::string> matches; } scan{part, {}};
        SDL_EnumerateDirectory(current.c_str(),
            [](void* user, const char*, const char* name) -> SDL_EnumerationResult {
                auto* scan = static_cast<Scan*>(user);
                if (SDL_strcasecmp(scan->part.c_str(), name) == 0)
                    scan->matches.emplace_back(name);
                return SDL_ENUM_CONTINUE;
            }, &scan);
        if (scan.matches.empty()) {
            result.physical = current + "/" + part;
            return result;
        }
        std::sort(scan.matches.begin(), scan.matches.end());
        if (scan.matches.size() != 1) {
            result.collision = true;
            return result; // Never choose an arbitrary asset on Android.
        }
        if (!current.empty() && current.back() != '/' && current.back() != '\\')
            current += '/';
        current += scan.matches.front();
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    result.physical = current;
    result.present = true;
    return result;
}

} // namespace Kotonoha
