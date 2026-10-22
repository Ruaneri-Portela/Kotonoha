#pragma once

#include <cstddef>

#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysReplayScriptOracle.hpp>

namespace Kotonoha {

// Temporary frontend-owned replay playback.  This class deliberately owns no
// SchoolDaysStateStore/History/Persistence: live narrative state is only used
// by an injected read-only SceneKey resolver.
class SchoolDaysReplayPlaybackSession final {
public:
    using ResolveSceneFn = bool (*)(const char* sceneKey,
                                    SchoolDaysKtrfScene* scene,
                                    void* userdata,
                                    Kotonoha_KtrfError* error);

    struct Resolver {
        ResolveSceneFn resolve = nullptr;
        void* userdata = nullptr;
    };

    SchoolDaysReplayPlaybackSession() = default;
    ~SchoolDaysReplayPlaybackSession();

    SchoolDaysReplayPlaybackSession(const SchoolDaysReplayPlaybackSession&) = delete;
    SchoolDaysReplayPlaybackSession& operator=(const SchoolDaysReplayPlaybackSession&) = delete;

    bool StartOrdinary(int hSceneIndex, Kotonoha_Game* gameContext,
                       const Resolver& resolver,
                       const SchoolDaysKtrfGameplayBridge::Factory& factory,
                       Kotonoha_KtrfError* error);
    bool StartSpecialChoice(int hSceneIndex, int choice,
                            Kotonoha_Game* gameContext,
                            const Resolver& resolver,
                            const SchoolDaysKtrfGameplayBridge::Factory& factory,
                            Kotonoha_KtrfError* error);
    // Called only after the current temporary Gameplay has reached physical
    // ORS end.  The next Gameplay is created before the outgoing one is
    // destroyed, so failure leaves the current replay scene retryable.
    bool AdvanceAfterSceneEnd(bool* completed, Kotonoha_KtrfError* error);
    bool AdvanceAfterSceneEnd(int priorSelectResult, bool* completed,
                              Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return open_; }
    Gameplay* CurrentGameplay() const { return currentGameplay_; }
    const SchoolDaysKtrfScene& CurrentScene() const { return currentScene_; }
    int HSceneIndex() const { return hSceneIndex_; }
    int SpecialChoice() const { return specialChoice_; }
    bool IsSpecialChoicePlayback() const { return specialChoice_ >= 0; }
    std::size_t SequenceIndex() const { return sequenceIndex_; }
    std::size_t SequenceCount() const {
        return sequence_ != nullptr ? sequence_->count : 0u;
    }

private:
    bool ResolveAndCreate(const char* sceneKey, Gameplay** gameplay,
                          SchoolDaysKtrfScene* scene,
                          Kotonoha_KtrfError* error) const;
    static bool Fail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
                     const char* message);

    const SchoolDaysReplayScriptOracle::SequenceDescriptor* sequence_ = nullptr;
    Resolver resolver_{};
    SchoolDaysKtrfGameplayBridge::Factory factory_{};
    Kotonoha_Game* gameContext_ = nullptr;
    Gameplay* currentGameplay_ = nullptr;
    SchoolDaysKtrfScene currentScene_{};
    int hSceneIndex_ = -1;
    int specialChoice_ = -1;
    std::size_t sequenceIndex_ = 0u;
    bool open_ = false;
};

} // namespace Kotonoha
