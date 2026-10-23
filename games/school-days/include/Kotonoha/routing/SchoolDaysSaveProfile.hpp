#pragma once

#include <string>

namespace Kotonoha {

// Authenticated FILMENGINE.INI persistence profile.  Keeping these strings out
// of the binary save codec mirrors the original separation between config and
// FlagsHash/SLog serialization.
class SchoolDaysSaveProfile {
public:
    bool useEnglish = false;
    bool textInput = false;
    std::string fontData;
    std::string saveFolder;
    std::string saveFileName;
    std::string flagFileName;
    std::string saveConfig;

    static bool Load(const std::string& assetsRoot, SchoolDaysSaveProfile* out,
                     std::string* error = nullptr);
    static bool FormatIndexed(const std::string& pattern, int slot,
                              std::string* out,
                              std::string* error = nullptr);
};

} // namespace Kotonoha
