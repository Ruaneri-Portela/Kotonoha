#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>

#include <cstdio>

namespace Kotonoha {

bool SchoolDaysEndBackgroundResolver::Fail(Kotonoha_KtrfError* error,
                                            Kotonoha_KtrfErrorCode code,
                                            const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message :
                                           "School Days end-background error");
    }
    return false;
}

bool SchoolDaysEndBackgroundResolver::Resolve(
    const Profile& profile, SchoolDaysStateStore* state, Result* out,
    Kotonoha_KtrfError* error) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    if (state == nullptr || out == nullptr || profile.baseAsset.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days end-background profile/state/result is invalid");
    }
    *out = Result{};
    out->kind = Kind::Base;
    out->asset = profile.baseAsset;

    // EXE 0x0041EB20: EndBGView==0 bypasses 0x0041FEE0 entirely and loads
    // BaseFile.  No ending/global state is consulted in that branch.
    if (!profile.endBgView) return true;

    // 0x0041F600 does not load EndingList at all when EndBGView is disabled.
    // Validate the ending table only on the enabled branch, matching that
    // native dependency boundary instead of rejecting an irrelevant profile.
    if (profile.endingMax < 0 ||
        static_cast<size_t>(profile.endingMax) > profile.endingAssets.size()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days EndingMax exceeds configured ending assets");
    }

    int64_t allClear = 0;
    if (!state->ReadGlobalInt("AllClear", &allClear, error)) return false;
    if (allClear != 0) {
        if (profile.allClearAsset.empty()) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days AllClear asset is empty");
        }
        out->kind = Kind::AllClear;
        out->asset = profile.allClearAsset;
        return true;
    }

    int64_t endClear = 0;
    if (!state->ReadGlobalInt("EndClear", &endClear, error)) return false;
    if (endClear == 0) return true;

    int completed = 0;
    for (int index = 0; index < profile.endingMax; ++index) {
        char key[32];
        std::snprintf(key, sizeof(key), "[End%02d]=\"", index);
        int64_t value = 0;
        if (!state->ReadGlobalInt(key, &value, error)) return false;
        if (value != 0) ++completed;
    }

    // EXE 0x0041FEE0 sets AllClear through the global bool setter when the
    // counted End00..End(N-1) total equals EndingMax, then selects AllClear.
    if (completed == profile.endingMax) {
        if (profile.allClearAsset.empty()) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days AllClear asset is empty");
        }
        if (!state->WriteGlobalInt("AllClear", 1, error)) return false;
        out->kind = Kind::AllClear;
        out->asset = profile.allClearAsset;
        out->promotedAllClear = true;
        return true;
    }

    int64_t endNo = 0;
    if (!state->ReadGlobalInt("EndNo", &endNo, error)) return false;
    if (endNo < 0 || endNo >= profile.endingMax ||
        static_cast<size_t>(endNo) >= profile.endingAssets.size()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_RANGE,
                    "School Days EndNo is outside the configured ending list");
    }

    // 0x00420310 indexes the vector as base + EndNo*0x1c: EndNo is zero-based
    // even though ENDLIST.INI keys are Ending01..Ending22.
    out->kind = Kind::Ending;
    out->endingIndex = static_cast<int>(endNo);
    out->asset = profile.endingAssets[static_cast<size_t>(endNo)];
    return true;
}

} // namespace Kotonoha
