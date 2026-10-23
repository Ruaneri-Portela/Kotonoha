#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <limits>

namespace Kotonoha {
namespace {

bool Fail(std::string* error, const char* text) {
    if (error != nullptr) *error = text != nullptr ? text : "School Days save profile error";
    return false;
}

bool ReadAll(const std::string& path, std::string* out) {
    if (out == nullptr) return false;
    std::vector<uint8_t> bytes;
    if (!ReadSchoolDaysAssetReference(path, &bytes, nullptr)) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool Value(const std::string& text, const char* key, std::string* out) {
    if (key == nullptr || out == nullptr) return false;
    const std::string prefix = "[" + std::string(key) + "]=\"";
    const size_t begin = text.find(prefix);
    if (begin == std::string::npos) return false;
    const size_t valueBegin = begin + prefix.size();
    const size_t end = text.find('"', valueBegin);
    if (end == std::string::npos) return false;
    *out = text.substr(valueBegin, end - valueBegin);
    return true;
}

} // namespace

bool SchoolDaysSaveProfile::Load(const std::string& assetsRoot,
                                 SchoolDaysSaveProfile* out,
                                 std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) return Fail(error, "School Days save profile output is null");
    *out = {};

    std::string path;
    std::string text;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), "ini/FILMENGINE.INI", &path, error,
            "FILMENGINE.INI") ||
        !ReadAll(path, &text)) {
        if (error != nullptr && error->empty())
            *error = "School Days FILMENGINE.INI could not be read";
        return false;
    }
    SchoolDaysSaveProfile profile;
    std::string english;
    std::string textInput;
    if (!Value(text, "UseEnglish", &english) ||
        (english != "0" && english != "1") ||
        !Value(text, "TextInput", &textInput) ||
        (textInput != "0" && textInput != "1") ||
        !Value(text, "FontData", &profile.fontData) || profile.fontData.empty() ||
        !Value(text, "SaveFolder", &profile.saveFolder) || profile.saveFolder.empty() ||
        !Value(text, "SaveFileName", &profile.saveFileName) || profile.saveFileName.empty() ||
        !Value(text, "FlagFileName", &profile.flagFileName) || profile.flagFileName.empty() ||
        !Value(text, "SaveConfig", &profile.saveConfig) || profile.saveConfig.empty()) {
        return Fail(error, "School Days FILMENGINE.INI persistence keys are malformed");
    }
    profile.useEnglish = english == "1";
    profile.textInput = textInput == "1";
    *out = std::move(profile);
    return true;
}

bool SchoolDaysSaveProfile::FormatIndexed(const std::string& pattern, int slot,
                                          std::string* out,
                                          std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr || slot < 0 || slot >= 100) {
        return Fail(error, "School Days indexed profile arguments are invalid");
    }
    const size_t marker = pattern.find("%d");
    if (marker == std::string::npos || pattern.find('%', marker + 2) != std::string::npos ||
        pattern.find('%') != marker) {
        return Fail(error, "School Days indexed profile supports exactly one %d token");
    }
    *out = pattern.substr(0, marker) + std::to_string(slot) + pattern.substr(marker + 2);
    return true;
}

} // namespace Kotonoha
