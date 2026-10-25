#include <Kotonoha/routing/SchoolDaysPlayDataSession.hpp>

#include <cstdio>

namespace Kotonoha {

SchoolDaysPlayDataSession::~SchoolDaysPlayDataSession() { Close(); }

bool SchoolDaysPlayDataSession::Fail(Kotonoha_KtrfError* error,
                                     Kotonoha_KtrfErrorCode code,
                                     const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message
                                         : "School Days PlayData session error");
    }
    return false;
}

bool SchoolDaysPlayDataSession::CreateAndSwap(
    const SchoolDaysKtrfScene& scene, Kotonoha_KtrfError* error) {
    if (factory_.create == nullptr || factory_.destroy == nullptr ||
        gameContext_ == nullptr || scene.orsPath.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData factory/scene is invalid");
    }
    Gameplay* next = factory_.create(scene.orsPath.c_str(), gameContext_,
                                     factory_.userdata);
    if (next == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_IO,
                    "School Days PlayData failed to create Gameplay");
    }

    Gameplay* previous = currentGameplay_;
    currentGameplay_ = next;
    currentScene_ = scene;
    choiceCommittedForScene_ = false;
    if (previous != nullptr) factory_.destroy(previous, factory_.userdata);
    return true;
}

bool SchoolDaysPlayDataSession::PrimeRecordedChoice(
    int* promptResult, bool* committed, Kotonoha_KtrfError* error) {
    if (currentGameplay_ == nullptr || promptResult == nullptr || committed == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData recorded-choice target is invalid");
    }
    if (!choiceReplayLatchArmed_) return true;

    int32_t saved = -1;
    const auto savedIt = recordedChoices_.find(currentScene_.sceneKey);
    const bool present = savedIt != recordedChoices_.end();
    if (present) saved = savedIt->second;
    // Native 0x4330F0 returns -1 when the tag4 key is absent. Any result other
    // than -2 consumes host +0x98, after which normal selection processing
    // records the accepted value into the temporary loaded SLog.
    const int choice = present ? saved : -1;
    *promptResult = choice;
    choiceReplayLatchArmed_ = false;
    return UpdateChoice(true, promptResult, committed, error);
}

bool SchoolDaysPlayDataSession::ParseRecordedChoices(
    const std::vector<uint8_t>& saveBytes,
    std::map<std::string, int32_t>* out,
    Kotonoha_KtrfError* error) const {
    if (out == nullptr || saveBytes.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData choice-oracle input is invalid");
    }
    SchoolDaysPersistence::SaveImage replayImage;
    if (!session_.Persistence().ParseSaveImage(saveBytes, &replayImage, error)) {
        return false;
    }

    std::map<std::string, int32_t> parsed;
    for (const auto& record : replayImage.document.records) {
        if (record.tag != 4) continue;
        std::string sceneKey;
        sceneKey.reserve(record.name.size());
        for (const char16_t unit : record.name) {
            if (unit > 0x7fu) {
                return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                            "School Days PlayData tag4 SceneKey is not ASCII");
            }
            sceneKey.push_back(static_cast<char>(unit));
        }
        if (sceneKey.empty() ||
            !parsed.emplace(sceneKey, record.integer).second) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days PlayData contains duplicate/empty tag4 SceneKey");
        }
    }
    *out = std::move(parsed);
    return true;
}

bool SchoolDaysPlayDataSession::ReplaceRecordedChoiceOracle(
    const std::vector<uint8_t>& saveBytes, Kotonoha_KtrfError* error) {
    if (!opened_ || !choiceReplayLatchArmed_) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData tag4-only LOAD requires the armed replay latch");
    }
    std::map<std::string, int32_t> next;
    if (!ParseRecordedChoices(saveBytes, &next, error)) return false;
    recordedChoices_ = std::move(next);
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

bool SchoolDaysPlayDataSession::UpdateChoice(
    bool setSelectReached, int* promptResult, bool* committed,
    Kotonoha_KtrfError* error) {
    if (committed == nullptr || promptResult == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData choice output/result is null");
    }
    *committed = false;
    if (!setSelectReached || choiceCommittedForScene_) {
        return true;
    }

    if (choiceReplayLatchArmed_) {
        return PrimeRecordedChoice(promptResult, committed, error);
    }

    const int result = *promptResult;
    if (result == -2) return true;
    int accepted = 0;
    if (!session_.CommitChoice(static_cast<int64_t>(result), &accepted, error) ||
        accepted == 0) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days PlayData choice was rejected by private routing");
    }
    choiceCommittedForScene_ = true;
    *committed = true;
    return true;
}

bool SchoolDaysPlayDataSession::Start(
    const char* ktrfPath, const char* orsRoot,
    const std::vector<uint8_t>& saveBytes,
    const std::vector<uint8_t>* globalFlagBytes,
    Kotonoha_Game* gameContext,
    const SchoolDaysKtrfGameplayBridge::Factory& factory,
    Kotonoha_KtrfError* error) {
    Close();
    if (ktrfPath == nullptr || *ktrfPath == '\0' || orsRoot == nullptr ||
        *orsRoot == '\0' || saveBytes.empty() || gameContext == nullptr ||
        factory.create == nullptr || factory.destroy == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData start arguments are invalid");
    }

    factory_ = factory;
    gameContext_ = gameContext;
    if (!session_.Open(ktrfPath, orsRoot, error)) {
        Close();
        return false;
    }
    if (globalFlagBytes != nullptr && !globalFlagBytes->empty() &&
        !session_.LoadGlobalFlag(*globalFlagBytes, error)) {
        Close();
        return false;
    }

    // Snapshot tag4 independently because SceneView may replace the replay
    // oracle while this private playback owner is alive.  The selected SaveFile
    // itself is nevertheless a real private LOAD checkpoint: restore its
    // State/History/Persistence/SceneKey exactly as the authenticated PlayData
    // flow does, then create only the private Gameplay.
    if (!ParseRecordedChoices(saveBytes, &recordedChoices_, error)) {
        Close();
        return false;
    }

    SchoolDaysKtrfSession::LoadPlan plan;
    choiceReplayLatchArmed_ = true;
    if (!session_.PlanLoadSave(saveBytes, &plan, error) ||
        !session_.CommitLoadSave(plan, error) ||
        !CreateAndSwap(plan.scene, error)) {
        Close();
        return false;
    }
    opened_ = true;
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

bool SchoolDaysPlayDataSession::AdvanceAfterSceneEnd(
    bool* terminal, Kotonoha_KtrfError* error) {
    if (!opened_ || currentGameplay_ == nullptr || terminal == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days PlayData advance argument/state is invalid");
    }
    *terminal = false;

    SchoolDaysKtrfSession::StepResult step;
    if (!session_.Advance("ktrf:next", &step, error)) return false;
    if (step.kind == SchoolDaysKtrfSession::StepKind::Terminal) {
        factory_.destroy(currentGameplay_, factory_.userdata);
        currentGameplay_ = nullptr;
        currentScene_ = {};
        opened_ = false;
        *terminal = true;
        return true;
    }
    if (step.kind != SchoolDaysKtrfSession::StepKind::Scene) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days PlayData private routing did not resolve a scene");
    }
    if (CreateAndSwap(step.scene, error)) return true;

    // The semantic route has already advanced inside the private KTRF session.
    // If physical Gameplay construction fails, discard the complete temporary
    // PlayData owner so no caller can observe an old Gameplay paired with the
    // newly advanced private State/History/Persistence image. The retained live
    // controller is never referenced by this cleanup path.
    Close();
    return false;
}

void SchoolDaysPlayDataSession::Close() {
    if (currentGameplay_ != nullptr && factory_.destroy != nullptr) {
        factory_.destroy(currentGameplay_, factory_.userdata);
    }
    currentGameplay_ = nullptr;
    currentScene_ = {};
    recordedChoices_.clear();
    session_.Close();
    factory_ = {};
    gameContext_ = nullptr;
    choiceReplayLatchArmed_ = false;
    choiceCommittedForScene_ = false;
    opened_ = false;
}

} // namespace Kotonoha
