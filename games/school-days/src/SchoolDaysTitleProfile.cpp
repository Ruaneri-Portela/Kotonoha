#include <Kotonoha/routing/SchoolDaysTitleProfile.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <limits>

namespace Kotonoha {
namespace {

bool Fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message != nullptr ? message : "School Days title profile error";
    return false;
}

bool ReadAll(const std::string& path, std::string* out) {
    if (out == nullptr) return false;
    std::vector<uint8_t> bytes;
    if (!ReadSchoolDaysAssetReference(path, &bytes, nullptr)) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool Value(const std::string& text, const std::string& key, std::string* out) {
    if (out == nullptr) return false;
    const std::string prefix = "[" + key + "]=\"";
    const size_t begin = text.find(prefix);
    if (begin == std::string::npos) return false;
    const size_t valueBegin = begin + prefix.size();
    const size_t end = text.find('"', valueBegin);
    if (end == std::string::npos) return false;
    *out = text.substr(valueBegin, end - valueBegin);
    return true;
}

bool Decimal(const std::string& text, const std::string& key, int* out) {
    std::string value;
    if (out == nullptr || !Value(text, key, &value) || value.empty()) return false;
    int result = 0;
    for (char ch : value) {
        if (ch < '0' || ch > '9' || result > (std::numeric_limits<int>::max() - (ch - '0')) / 10) {
            return false;
        }
        result = result * 10 + (ch - '0');
    }
    *out = result;
    return true;
}

} // namespace

bool SchoolDaysTitleProfile::Load(
    const std::string& assetsRoot, SchoolDaysEndBackgroundResolver::Profile* out,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) return Fail(error, "School Days title profile output is null");
    *out = {};

    std::string startPath;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), "ini/STARTSCRIPT.INI", &startPath, error,
            "STARTSCRIPT.INI")) {
        return false;
    }
    std::string start;
    if (!ReadAll(startPath, &start)) {
        return Fail(error, "School Days STARTSCRIPT.INI could not be read");
    }

    std::string base;
    std::string titleBgm;
    std::string systemBgm;
    int endBgView = 0;
    if (!Value(start, "BaseFile", &base) || base.empty() ||
        !Value(start, "TitleBGM", &titleBgm) || titleBgm.empty() ||
        !Value(start, "SystemBGM", &systemBgm) || systemBgm.empty() ||
        !Decimal(start, "EndBGView", &endBgView) ||
        (endBgView != 0 && endBgView != 1)) {
        return Fail(error, "School Days STARTSCRIPT title keys are malformed");
    }

    SchoolDaysEndBackgroundResolver::Profile profile;
    profile.baseAsset = base;
    profile.titleBgmStem = titleBgm;
    profile.systemBgmStem = systemBgm;
    profile.endBgView = endBgView != 0;
    if (!profile.endBgView) {
        *out = std::move(profile);
        return true;
    }

    std::string endPath;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), "ini/ENDLIST.INI", &endPath, error,
            "ENDLIST.INI")) {
        return false;
    }
    std::string endings;
    if (!ReadAll(endPath, &endings)) {
        return Fail(error, "School Days ENDLIST.INI could not be read");
    }

    int endingMax = 0;
    if (!Decimal(endings, "EndingMax", &endingMax) || endingMax <= 0 || endingMax > 999) {
        return Fail(error, "School Days EndingMax is malformed or unsupported");
    }
    profile.endingMax = endingMax;
    profile.endingAssets.reserve(static_cast<size_t>(endingMax));
    for (int index = 1; index <= endingMax; ++index) {
        char key[32]{};
        std::snprintf(key, sizeof(key), "Ending%02d", index);
        std::string asset;
        if (!Value(endings, key, &asset) || asset.empty()) {
            return Fail(error, "School Days ENDLIST ending entry is missing");
        }
        profile.endingAssets.push_back(std::move(asset));
    }
    if (!Value(endings, "AllClear", &profile.allClearAsset) ||
        profile.allClearAsset.empty()) {
        return Fail(error, "School Days ENDLIST AllClear entry is missing");
    }

    *out = std::move(profile);
    return true;
}

} // namespace Kotonoha
