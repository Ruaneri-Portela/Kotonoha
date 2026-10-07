#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace Kotonoha {

/* Resolve extracted asset spelling, retaining the ORS logical path unchanged.
   In particular, Android needs to find uppercase OGG names on a case-sensitive
   filesystem. An absent component is returned as requested, so media load can
   fail safely without substituting an undocumented resource. */
inline std::string ResolveSchoolDaysBgmAsset(const char* assetRoot,
                                              const std::string& logical) {
    std::string current = assetRoot && *assetRoot ? assetRoot : ".";
    size_t pos = 0;
    while (pos < logical.size()) {
        const size_t slash = logical.find('/', pos);
        const std::string part = logical.substr(pos, slash - pos);
        struct Match {
            const std::string* wanted;
            std::string exact;
            std::string folded;
        } match{&part, "", ""};
        SDL_EnumerateDirectory(current.c_str(),
            [](void* opaque, const char*, const char* name) -> SDL_EnumerationResult {
                auto* m = static_cast<Match*>(opaque);
                if (m->wanted->compare(name) == 0) {
                    m->exact = name;
                    return SDL_ENUM_SUCCESS;
                }
                if (m->folded.empty() &&
                    SDL_strcasecmp(name, m->wanted->c_str()) == 0)
                    m->folded = name;
                return SDL_ENUM_CONTINUE;
            }, &match);
        const std::string& resolved = !match.exact.empty() ? match.exact
                                    : !match.folded.empty() ? match.folded : part;
        if (!current.empty() && current.back() != '/' && current.back() != '\\')
            current += '/';
        current += resolved;
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return current;
}
} // namespace Kotonoha
