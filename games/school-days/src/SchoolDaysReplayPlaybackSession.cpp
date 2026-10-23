#include <Kotonoha/routing/SchoolDaysReplayPlaybackSession.hpp>

#include <cstdio>

namespace Kotonoha {

SchoolDaysReplayPlaybackSession::~SchoolDaysReplayPlaybackSession() { Close(); }

bool SchoolDaysReplayPlaybackSession::Fail(Kotonoha_KtrfError* error,
                                           Kotonoha_KtrfErrorCode code,
                                           const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s",
                      message != nullptr ? message : "School Days replay playback error");
    }
    return false;
}

bool SchoolDaysReplayPlaybackSession::ResolveAndCreate(
    const char* sceneKey, Gameplay** gameplay, SchoolDaysKtrfScene* scene,
    Kotonoha_KtrfError* error) const {
    if (sceneKey == nullptr || *sceneKey == '\0' || gameplay == nullptr ||
        scene == nullptr || resolver_.resolve == nullptr || factory_.create == nullptr ||
        gameContext_ == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay resolve/create argument is invalid");
    }

    *gameplay = nullptr;
    *scene = {};
    if (!resolver_.resolve(sceneKey, scene, resolver_.userdata, error)) return false;
    if (scene->sceneKey != sceneKey || scene->orsPath.empty()) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days replay resolver returned a mismatched physical scene");
    }

    Gameplay* created = factory_.create(scene->orsPath.c_str(), gameContext_,
                                        factory_.userdata);
    if (created == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_IO,
                    "School Days replay Gameplay creation failed");
    }
    *gameplay = created;
    return true;
}

bool SchoolDaysReplayPlaybackSession::StartOrdinary(
    int hSceneIndex, Kotonoha_Game* gameContext, const Resolver& resolver,
    const SchoolDaysKtrfGameplayBridge::Factory& factory,
    Kotonoha_KtrfError* error) {
    if (open_) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay playback is already open");
    }
    if (gameContext == nullptr || resolver.resolve == nullptr ||
        factory.create == nullptr || factory.destroy == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay playback dependencies are incomplete");
    }

    std::string oracleError;
    const auto* sequence =
        SchoolDaysReplayScriptOracle::OrdinarySequence(hSceneIndex, nullptr,
                                                       &oracleError);
    if (sequence == nullptr || sequence->count == 0u || sequence->sceneKeys == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay HScene is not an ordinary static sequence");
    }

    resolver_ = resolver;
    factory_ = factory;
    gameContext_ = gameContext;
    sequence_ = sequence;
    hSceneIndex_ = hSceneIndex;
    specialChoice_ = -1;
    sequenceIndex_ = 0u;

    Gameplay* gameplay = nullptr;
    SchoolDaysKtrfScene scene;
    if (!ResolveAndCreate(sequence_->sceneKeys[0], &gameplay, &scene, error)) {
        sequence_ = nullptr;
        resolver_ = {};
        factory_ = {};
        gameContext_ = nullptr;
        hSceneIndex_ = -1;
        return false;
    }

    currentGameplay_ = gameplay;
    currentScene_ = std::move(scene);
    open_ = true;
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

bool SchoolDaysReplayPlaybackSession::StartSpecialChoice(
    int hSceneIndex, int choice, Kotonoha_Game* gameContext,
    const Resolver& resolver,
    const SchoolDaysKtrfGameplayBridge::Factory& factory,
    Kotonoha_KtrfError* error) {
    if (open_) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay playback is already open");
    }
    if (gameContext == nullptr || resolver.resolve == nullptr ||
        factory.create == nullptr || factory.destroy == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay playback dependencies are incomplete");
    }

    std::string oracleError;
    const auto* sequence = SchoolDaysReplayScriptOracle::SpecialChoiceSequence(
        hSceneIndex, choice, nullptr, &oracleError);
    if (sequence == nullptr || sequence->count == 0u || sequence->sceneKeys == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay HScene has no authenticated special choice table");
    }

    resolver_ = resolver;
    factory_ = factory;
    gameContext_ = gameContext;
    sequence_ = sequence;
    hSceneIndex_ = hSceneIndex;
    specialChoice_ = choice;
    sequenceIndex_ = 0u;

    Gameplay* gameplay = nullptr;
    SchoolDaysKtrfScene scene;
    if (!ResolveAndCreate(sequence_->sceneKeys[0], &gameplay, &scene, error)) {
        sequence_ = nullptr;
        resolver_ = {};
        factory_ = {};
        gameContext_ = nullptr;
        hSceneIndex_ = -1;
        specialChoice_ = -1;
        return false;
    }

    currentGameplay_ = gameplay;
    currentScene_ = std::move(scene);
    open_ = true;
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

bool SchoolDaysReplayPlaybackSession::AdvanceAfterSceneEnd(
    bool* completed, Kotonoha_KtrfError* error) {
    return AdvanceAfterSceneEnd(-2, completed, error);
}

bool SchoolDaysReplayPlaybackSession::AdvanceAfterSceneEnd(
    int priorSelectResult, bool* completed, Kotonoha_KtrfError* error) {
    if (!open_ || currentGameplay_ == nullptr || sequence_ == nullptr ||
        completed == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days replay advance was requested outside playback");
    }
    *completed = false;

    std::size_t nextIndex = sequenceIndex_ + 1u;
    bool terminal = nextIndex >= sequence_->count;
    if (specialChoice_ >= 0) {
        int specialNext = -1;
        if (!SchoolDaysReplayScriptOracle::ResolveSpecialNextCursor(
                hSceneIndex_, specialChoice_, static_cast<int>(sequenceIndex_),
                priorSelectResult, &specialNext, &terminal, nullptr)) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days replay native special cursor resolution failed");
        }
        if (!terminal) nextIndex = static_cast<std::size_t>(specialNext);
    }

    if (terminal) {
        factory_.destroy(currentGameplay_, factory_.userdata);
        currentGameplay_ = nullptr;
        currentScene_ = {};
        sequence_ = nullptr;
        sequenceIndex_ = 0u;
        hSceneIndex_ = -1;
        specialChoice_ = -1;
        open_ = false;
        *completed = true;
        if (error != nullptr) {
            error->code = KOTONOHA_KTRF_OK;
            error->message[0] = '\0';
        }
        return true;
    }

    Gameplay* nextGameplay = nullptr;
    SchoolDaysKtrfScene nextScene;
    if (!ResolveAndCreate(sequence_->sceneKeys[nextIndex], &nextGameplay,
                          &nextScene, error)) {
        return false;
    }

    Gameplay* previous = currentGameplay_;
    currentGameplay_ = nextGameplay;
    currentScene_ = std::move(nextScene);
    sequenceIndex_ = nextIndex;
    factory_.destroy(previous, factory_.userdata);
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_OK;
        error->message[0] = '\0';
    }
    return true;
}

void SchoolDaysReplayPlaybackSession::Close() {
    if (currentGameplay_ != nullptr && factory_.destroy != nullptr) {
        factory_.destroy(currentGameplay_, factory_.userdata);
    }
    currentGameplay_ = nullptr;
    currentScene_ = {};
    sequence_ = nullptr;
    resolver_ = {};
    factory_ = {};
    gameContext_ = nullptr;
    hSceneIndex_ = -1;
    specialChoice_ = -1;
    sequenceIndex_ = 0u;
    open_ = false;
}

} // namespace Kotonoha
