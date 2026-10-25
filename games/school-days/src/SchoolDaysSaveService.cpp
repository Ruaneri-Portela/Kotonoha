#include <Kotonoha/routing/SchoolDaysSaveService.hpp>

#include <algorithm>
#include <cstdio>

namespace Kotonoha {

bool SchoolDaysSaveService::Fail(Kotonoha_KtrfError* error,
                                 Kotonoha_KtrfErrorCode code,
                                 const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message :
                                           "School Days save-service error");
    }
    return false;
}

bool SchoolDaysSaveService::PlanSlot(
    int slot, const std::u16string& primaryMetadata,
    const std::u16string& subMetadata,
    const SchoolDaysPersistence& persistence, const KtrfStateStore& state,
    Plan* out, Kotonoha_KtrfError* error) {
    SchoolDaysSaveProfile profile;
    profile.saveFolder = "Save";
    profile.saveFileName = "Save/SaveFile00%d.DAT";
    profile.flagFileName = "Save/GlobalFlag.DAT";
    profile.saveConfig = "FILMEngine/SaveFile00%d";
    return PlanSlot(profile, slot, primaryMetadata, subMetadata, persistence,
                    state, out, error);
}

bool SchoolDaysSaveService::PlanSlot(
    const SchoolDaysSaveProfile& profile, int slot,
    const std::u16string& primaryMetadata,
    const std::u16string& subMetadata,
    const SchoolDaysPersistence& persistence, const KtrfStateStore& state,
    Plan* out, Kotonoha_KtrfError* error) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    if (out == nullptr || slot < 0 || slot >= 100) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days SaveFile slot must be in range 0..99");
    }
    if (!persistence.HasSaveLog() || !persistence.HasGlobalFlag()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days save plan requires live SLog and GlobalFlag state");
    }

    Plan planned;
    planned.slot = slot;
    std::string profileError;
    if (!SchoolDaysSaveProfile::FormatIndexed(profile.saveFileName, slot,
                                              &planned.saveFilePath,
                                              &profileError) ||
        !SchoolDaysSaveProfile::FormatIndexed(profile.saveConfig, slot,
                                              &planned.saveConfigKey,
                                              &profileError) ||
        profile.flagFileName.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    profileError.empty() ?
                        "School Days save profile path/key is invalid" :
                        profileError.c_str());
    }
    planned.globalFlagPath = profile.flagFileName;
    planned.saveConfigSubKey = planned.saveConfigKey + "_Sub";

    // SAVE is legal during the post-choice visual tail.  At that point tag4
    // already records the accepted choice while the tag1 FlgH snapshot may
    // still describe the pre-choice scene state.  Refresh tag1 on a staged
    // copy from the live KTRF state before serializing; never mutate the live
    // persistence object until the external writes have succeeded and Commit
    // is called.
    planned.stagedPersistence = persistence;
    KtrfStateStore::Snapshot liveState;
    if (!state.Capture(&liveState, error)) return false;
    // Router activation clears every choice latch, so a committed bit in this
    // snapshot can only belong to the active node/visit. Persist that fact
    // separately from tag4, whose SceneKey record intentionally survives Back
    // for PlayData history.
    const bool liveChoiceCommitted = std::any_of(
        liveState.choiceCommitted.begin(), liveState.choiceCommitted.end(),
        [](uint8_t value) { return value != 0u; });
    if (!planned.stagedPersistence.RefreshCurrentScriptSnapshot(state, error) ||
        !planned.stagedPersistence.SerializeSaveLogWithLiveChoiceCommitMarker(
            liveChoiceCommitted, &planned.saveFileBytes, error)) {
        return false;
    }

    // 0x0042AEA0 writes SLog before mutating SaveConfig metadata.  The staged
    // object preserves that high-level transaction ordering for the caller.
    if (!planned.stagedPersistence.UpsertGlobalString(
            planned.saveConfigKey, primaryMetadata, error) ||
        !planned.stagedPersistence.UpsertGlobalString(
            planned.saveConfigSubKey, subMetadata, error) ||
        !planned.stagedPersistence.SerializeGlobalFlag(
            state, &planned.globalFlagBytes, error)) {
        return false;
    }

    *out = std::move(planned);
    return true;
}

bool SchoolDaysSaveService::Commit(const Plan& plan,
                                   SchoolDaysPersistence* persistence,
                                   Kotonoha_KtrfError* error) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    if (persistence == nullptr || plan.slot < 0 || plan.slot >= 100 ||
        plan.saveFileBytes.empty() || plan.globalFlagBytes.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days save commit plan is invalid");
    }
    *persistence = plan.stagedPersistence;
    return true;
}

} // namespace Kotonoha
