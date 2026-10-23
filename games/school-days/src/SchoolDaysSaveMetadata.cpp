#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>

#include <array>
#include <cstdio>
#include <cstring>

namespace Kotonoha {
namespace {

std::u16string ToU16(const char* ascii) {
    std::u16string out;
    if (ascii == nullptr) return out;
    while (*ascii != '\0') out.push_back(static_cast<char16_t>(*ascii++));
    return out;
}

void AppendAscii(std::u16string* out, const char* ascii) {
    const auto value = ToU16(ascii);
    out->append(value);
}

void AppendInt(std::u16string* out, int value, int width, bool zeroPad) {
    char buffer[32]{};
    if (zeroPad) {
        std::snprintf(buffer, sizeof(buffer), "%0*d", width, value);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%*d", width, value);
    }
    AppendAscii(out, buffer);
}

constexpr std::array<const char*, 7> kEnglishWeekdays = {
    "Sun.", "Mon.", "Tue.", "Wed.", "Thu.", "Fri.", "Sat.",
};

constexpr std::array<char16_t, 7> kJapaneseWeekdays = {
    u'\u65e5', u'\u6708', u'\u706b', u'\u6c34', u'\u6728', u'\u91d1', u'\u571f',
};

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) *error = message != nullptr ? message : "School Days save metadata error";
    return false;
}

} // namespace

int SchoolDaysSaveMetadata::StoryFor(int64_t route, int64_t endClear) {
    if (route < 0 || route > 54) return endClear != 0 ? 6 : 0;
    if (route == 0) return 1;
    if (route <= 3) return 2;
    if (route <= 5) return 3;
    if (route <= 18) return 4;
    if (route <= 31) return 5;
    return 6;
}

bool SchoolDaysSaveMetadata::ValidateTime(const LocalTime& time,
                                          std::string* error) {
    if (time.year < 0 || time.year > 9999 || time.month < 1 || time.month > 12 ||
        time.day < 1 || time.day > 31 || time.weekday < 0 || time.weekday > 6 ||
        time.hour < 0 || time.hour > 23 || time.minute < 0 || time.minute > 59) {
        return SetError(error, "School Days save metadata local time is invalid");
    }
    return true;
}

bool SchoolDaysSaveMetadata::Format(const LocalTime& time, bool useEnglish,
                                    int story,
                                    const std::u16string& comment, Result* out,
                                    std::string* error) {
    if (out == nullptr) return SetError(error, "School Days save metadata result is null");
    if (!ValidateTime(time, error)) return false;
    if (story < 0 || story > 99) {
        return SetError(error, "School Days save metadata story is outside native display range");
    }

    Result result;
    result.story = story;
    if (useEnglish) {
        // SysMenu 0x10011B40: L"%2d/%2d/%4d(%s)%02d:%02d"
        AppendInt(&result.date, time.month, 2, false);
        result.date.push_back(u'/');
        AppendInt(&result.date, time.day, 2, false);
        result.date.push_back(u'/');
        AppendInt(&result.date, time.year, 4, false);
        result.date.push_back(u'(');
        AppendAscii(&result.date, kEnglishWeekdays[static_cast<size_t>(time.weekday)]);
        result.date.push_back(u')');
        AppendInt(&result.date, time.hour, 2, true);
        result.date.push_back(u':');
        AppendInt(&result.date, time.minute, 2, true);

        // SysMenu 0x10011B40: L"%02d"
        AppendInt(&result.storyText, story, 2, true);
    } else {
        // SysMenu 0x10011B40: L"%4d年%2d月%2d日(%s)%02d:%02d"
        AppendInt(&result.date, time.year, 4, false);
        result.date.push_back(u'\u5e74');
        AppendInt(&result.date, time.month, 2, false);
        result.date.push_back(u'\u6708');
        AppendInt(&result.date, time.day, 2, false);
        result.date.push_back(u'\u65e5');
        result.date.push_back(u'(');
        result.date.push_back(kJapaneseWeekdays[static_cast<size_t>(time.weekday)]);
        result.date.push_back(u')');
        AppendInt(&result.date, time.hour, 2, true);
        result.date.push_back(u':');
        AppendInt(&result.date, time.minute, 2, true);

        // SysMenu 0x10011B40: L"第%d話"
        result.storyText.push_back(u'\u7b2c');
        AppendInt(&result.storyText, story, 1, false);
        result.storyText.push_back(u'\u8a71');
    }

    result.primary = result.date;
    result.primary += result.storyText;
    result.sub = comment;
    *out = std::move(result);
    if (error != nullptr) error->clear();
    return true;
}

bool SchoolDaysSaveMetadata::SplitStoredPrimary(
    const std::u16string& primary, bool useEnglish, std::u16string* date,
    std::u16string* storyText, std::string* error) {
    if (date == nullptr || storyText == nullptr) {
        return SetError(error, "School Days stored metadata outputs are null");
    }

    const size_t storyUnits = useEnglish ? 2u : 3u;
    if (primary.size() < storyUnits) {
        return SetError(error,
                        "School Days stored SaveConfig is shorter than its native story suffix");
    }

    const size_t split = primary.size() - storyUnits;
    *date = primary.substr(0, split);
    *storyText = primary.substr(split, storyUnits);
    if (error != nullptr) error->clear();
    return true;
}

bool SchoolDaysSaveMetadata::ParseUseEnglish(const std::string& filmEngineIni,
                                             bool* useEnglish,
                                             std::string* error) {
    if (useEnglish == nullptr) return SetError(error, "UseEnglish output is null");
    constexpr const char* kKey = "[UseEnglish]=\"";
    const size_t pos = filmEngineIni.find(kKey);
    if (pos == std::string::npos) {
        return SetError(error, "FILMENGINE.INI lacks [UseEnglish]=\" key");
    }
    const size_t value = pos + std::strlen(kKey);
    if (value >= filmEngineIni.size() ||
        (filmEngineIni[value] != '0' && filmEngineIni[value] != '1')) {
        return SetError(error, "FILMENGINE.INI UseEnglish value is not 0/1");
    }
    if (value + 1 >= filmEngineIni.size() || filmEngineIni[value + 1] != '"') {
        return SetError(error, "FILMENGINE.INI UseEnglish value is not quoted exactly");
    }
    *useEnglish = filmEngineIni[value] == '1';
    if (error != nullptr) error->clear();
    return true;
}

} // namespace Kotonoha
