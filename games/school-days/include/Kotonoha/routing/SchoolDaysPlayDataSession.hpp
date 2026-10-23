#pragma once

#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

struct Kotonoha_Game;

namespace Kotonoha {

// Isolated implementation of SceneView PlayData.  It owns a private KTRF
// session/state/persistence image seeded from one SaveFile plus a read-only
// snapshot of the live GlobalFlag.  Nothing in this object aliases the live
// SchoolDaysKtrfGameplayBridge.
class SchoolDaysPlayDataSession {
public:
    SchoolDaysPlayDataSession() = default;
    ~SchoolDaysPlayDataSession();

    SchoolDaysPlayDataSession(const SchoolDaysPlayDataSession&) = delete;
    SchoolDaysPlayDataSession& operator=(const SchoolDaysPlayDataSession&) = delete;

    bool Start(const char* ktrfPath, const char* orsRoot,
               const std::vector<uint8_t>& saveBytes,
               const std::vector<uint8_t>* globalFlagBytes,
               Kotonoha_Game* gameContext,
               const SchoolDaysKtrfGameplayBridge::Factory& factory,
               Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return opened_; }
    Gameplay* CurrentGameplay() const { return currentGameplay_; }
    const SchoolDaysKtrfScene& CurrentScene() const { return currentScene_; }
    bool ChoiceReplayLatchArmed() const { return choiceReplayLatchArmed_; }

    // Commits a resolved prompt into the private router/SLog. While the native
    // +0x98 latch is armed, the first SetSELECT that has actually reached its
    // ORS start tick is answered from loaded tag4 (or -1 when absent) and
    // consumes the latch; later prompts are interactive.
    // promptResult is the Gameplay::promptId storage in production, but keeping
    // this seam scalar makes the routing owner independent from Prompt/SDL UI.
    bool UpdateChoice(bool setSelectReached, int* promptResult, bool* committed,
                      Kotonoha_KtrfError* error);

    // Called once the current ORS has physically ended.  Routing is advanced
    // only in this private session.  terminal=true destroys the temporary
    // Gameplay and returns ownership to SceneView.
    bool AdvanceAfterSceneEnd(bool* terminal, Kotonoha_KtrfError* error);

private:
    static bool Fail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
                     const char* message);
    bool CreateAndSwap(const SchoolDaysKtrfScene& scene,
                       Kotonoha_KtrfError* error);
    bool PrimeRecordedChoice(int* promptResult, bool* committed,
                             Kotonoha_KtrfError* error);

    SchoolDaysKtrfSession session_;
    SchoolDaysKtrfGameplayBridge::Factory factory_{};
    Kotonoha_Game* gameContext_ = nullptr;
    Gameplay* currentGameplay_ = nullptr;
    SchoolDaysKtrfScene currentScene_;
    bool choiceReplayLatchArmed_ = false;
    bool choiceCommittedForScene_ = false;
    bool opened_ = false;
};

} // namespace Kotonoha
