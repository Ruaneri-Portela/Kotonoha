#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

struct Kotonoha_Game;

namespace Kotonoha {

class SchoolDaysScenePresentation {
public:
    virtual ~SchoolDaysScenePresentation() = default;
    virtual void RestoreChoice(int64_t value) = 0;
    virtual void SetPaused(bool paused) = 0;
};
using Gameplay = SchoolDaysScenePresentation;

class SchoolDaysKtrfGameplayBridge {
public:
    using CreateGameplayFn = Gameplay* (*)(const char* orsPath,
                                            Kotonoha_Game* gameContext,
                                            void* userdata);
    using DestroyGameplayFn = void (*)(Gameplay* gameplay, void* userdata);

    struct Factory {
        CreateGameplayFn create = nullptr;
        DestroyGameplayFn destroy = nullptr;
        void* userdata = nullptr;
    };

    enum class StepKind {
        Unresolved,
        BlockedChoice,
        SwappedScene,
        BackBoundary,
        BackTrialPv,
        BackNoScript,
        PendingHandoff,
        Terminal,
    };

    struct StepResult {
        StepKind kind = StepKind::Unresolved;
        uint32_t transitionIndex = KOTONOHA_KTRF_NULL_INDEX;
        uint32_t hopCount = 0;
        SchoolDaysKtrfScene scene;
        std::vector<std::string> hooks;
        std::vector<int64_t> endings;
    };

    SchoolDaysKtrfGameplayBridge() = default;
    ~SchoolDaysKtrfGameplayBridge();

    SchoolDaysKtrfGameplayBridge(const SchoolDaysKtrfGameplayBridge&) = delete;
    SchoolDaysKtrfGameplayBridge& operator=(const SchoolDaysKtrfGameplayBridge&) = delete;

    bool Open(const char* ktrfPath, const char* assetsRoot,
              Kotonoha_Game* gameContext, const Factory& factory,
              Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return opened; }
    Gameplay* CurrentGameplay() const { return currentGameplay; }
    const SchoolDaysKtrfScene& CurrentScene() const { return currentScene; }
    const SchoolDaysKtrfSession& Session() const { return session; }
    SchoolDaysKtrfSession& Session() { return session; }

    bool CommitChoice(int64_t value, int* accepted, Kotonoha_KtrfError* error);
    bool Advance(const char* trigger, StepResult* out, Kotonoha_KtrfError* error);
    bool AdvanceToNextChoice(const char* trigger, bool allowUnreadScenes,
                             StepResult* out,
                             Kotonoha_KtrfError* error);
    bool Back(StepResult* out, Kotonoha_KtrfError* error);
    bool LoadSave(const std::vector<uint8_t>& bytes, StepResult* out,
                  Kotonoha_KtrfError* error);
    bool LoadRouteMapSelector(int selector, StepResult* out,
                              Kotonoha_KtrfError* error);
    // Start a fresh normal School Days route from the authenticated new-game
    // entry while preserving GlobalFlag. The physical Gameplay swap is
    // transactional: the old Title-owned/frozen scene remains live on failure.
    bool ResetNewGame(StepResult* out, Kotonoha_KtrfError* error);

    // Native MenuBar -> MainMenu confirmation destroys the active script/media
    // before Title re-entry but keeps the global owner/GlobalFlag alive.  This
    // is deliberately narrower than Close(), which tears down the KTRF session.
    bool ReleaseCurrentGameplayForFrontend(Kotonoha_KtrfError* error);

    // Developer navigation: load a physical KTRF scene directly and activate
    // its NODE in the native router. Runtime variables are preserved.
    bool DebugJumpToNode(uint32_t nodeIndex, StepResult* out,
                         Kotonoha_KtrfError* error);

    bool HasPendingHandoff() const { return pendingHandoff; }
    bool PendingHandoffIsTerminal() const {
        return pendingHandoff &&
               pendingStep.kind == SchoolDaysKtrfSession::StepKind::Terminal;
    }
    bool ContinueHandoff(StepResult* out, Kotonoha_KtrfError* error);

private:
    bool CreateAndSwap(const SchoolDaysKtrfScene& scene,
                       Kotonoha_KtrfError* error);
    bool BuildCoordinate(const SchoolDaysKtrfScene& scene,
                         SchoolDaysCoordinate* coordinate,
                         Kotonoha_KtrfError* error) const;
    static bool IsCallback2C(const std::vector<std::string>& hooks);
    static bool IsCallback38(const std::vector<std::string>& hooks);
    static void SetError(Kotonoha_KtrfError* error,
                         Kotonoha_KtrfErrorCode code,
                         const char* message);

    SchoolDaysKtrfSession session;
    Factory factory{};
    Kotonoha_Game* gameContext = nullptr;
    Gameplay* currentGameplay = nullptr;
    SchoolDaysKtrfScene currentScene;
    SchoolDaysKtrfSession::StepResult pendingStep;
    SchoolDaysHistory::Entry pendingHistoryEntry;
    bool pendingHistoryEntryValid = false;
    bool pendingHandoff = false;
    bool opened = false;
};

} // namespace Kotonoha
