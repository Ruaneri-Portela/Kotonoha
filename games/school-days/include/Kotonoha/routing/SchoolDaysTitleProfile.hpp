#pragma once

#include <string>

#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>

namespace Kotonoha {

// Loader for the two authenticated configuration surfaces consumed by the
// original title/end-background path: STARTSCRIPT.INI and ENDLIST.INI.
class SchoolDaysTitleProfile {
public:
    static bool Load(const std::string& assetsRoot,
                     SchoolDaysEndBackgroundResolver::Profile* out,
                     std::string* error = nullptr);
};

} // namespace Kotonoha
