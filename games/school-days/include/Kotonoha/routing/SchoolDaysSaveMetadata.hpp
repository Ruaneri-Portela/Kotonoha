#pragma once

#include <cstdint>
#include <string>

#include <Kotonoha/routing/KtrfStateStore.hpp>

namespace Kotonoha {

// Pure formatter for MENU::SaveLoad -> EXE SaveConfig type-8 metadata.
// The native SysMenu formatter uses GetLocalTime and RouteProc::_GetStory@4;
// callers inject the wall-clock fields here so tests remain deterministic.
class SchoolDaysSaveMetadata {
public:
    struct LocalTime {
        int year = 0;
        int month = 0;
        int day = 0;
        int weekday = 0; // Win32 SYSTEMTIME convention: Sunday=0 .. Saturday=6
        int hour = 0;
        int minute = 0;
    };

    struct Result {
        int story = 0;
        std::u16string date;
        std::u16string storyText;
        std::u16string primary; // SaveConfig(slot): date + storyText
        std::u16string sub;     // SaveConfig(slot)_Sub: comment/note
    };

    // RouteProcSDHQ.dll:_GetStory@4 at 0x10008990.
    static int StoryFor(int64_t route, int64_t endClear);

    static bool Format(const LocalTime& time, bool useEnglish, int story,
                       const std::u16string& comment, Result* out,
                       std::string* error = nullptr);

    static bool Build(const KtrfStateStore& state, const LocalTime& time,
                      bool useEnglish, const std::u16string& comment,
                      Result* out, Kotonoha_KtrfError* error);

    // EXE host +0x9C (0x0042A980) reconstructs SaveLoad's first two display
    // buffers by splitting the stored SaveConfig type-8 value: Japanese keeps
    // the final three UTF-16 code units ("第N話"), English the final two
    // ("%02d").  Keep that inverse beside Format so UI code never reimplements
    // the persisted representation.
    static bool SplitStoredPrimary(const std::u16string& primary,
                                   bool useEnglish,
                                   std::u16string* date,
                                   std::u16string* storyText,
                                   std::string* error = nullptr);

    // Exact FILMENGINE.INI value syntax used by the v1.02 parser.  This helper
    // is intentionally text-only; filesystem ownership stays with the app/UI.
    static bool ParseUseEnglish(const std::string& filmEngineIni,
                                bool* useEnglish,
                                std::string* error = nullptr);

private:
    static bool ValidateTime(const LocalTime& time, std::string* error);
};

} // namespace Kotonoha
