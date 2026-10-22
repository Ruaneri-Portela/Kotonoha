#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>

#include <cstdio>

namespace Kotonoha {

bool SchoolDaysSaveMetadata::Build(const SchoolDaysStateStore& state,
                                   const LocalTime& time, bool useEnglish,
                                   const std::u16string& comment, Result* out,
                                   Kotonoha_KtrfError* error) {
    int64_t route = 0;
    int64_t endClear = 0;
    if (!state.ReadSessionInt("ROUTE", &route, error)) return false;
    if (!state.ReadGlobalInt("EndClear", &endClear, error)) return false;

    std::string formatError;
    if (!Format(time, useEnglish, StoryFor(route, endClear), comment, out,
                &formatError)) {
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_ERROR_FORMAT;
            std::snprintf(error->message, sizeof(error->message), "%s",
                          formatError.c_str());
        }
        return false;
    }
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

} // namespace Kotonoha
