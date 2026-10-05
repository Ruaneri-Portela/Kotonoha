#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

struct Kotonoha_Game;

namespace Kotonoha {

class Gameplay;

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

    bool HasPendingHandoff() const { return pendingHandoff; }
    bool ContinueHandoff(StepResult* out, Kotonoha_KtrfError* error);

private:
    bool CreateAndSwap(const SchoolDaysKtrfScene& scene,
                       Kotonoha_KtrfError* error);
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
    bool pendingHandoff = false;
    bool opened = false;
};

SchoolDaysKtrfGameplayBridge::Factory MakeSchoolDaysRealGameplayFactory();

} // namespace Kotonoha
