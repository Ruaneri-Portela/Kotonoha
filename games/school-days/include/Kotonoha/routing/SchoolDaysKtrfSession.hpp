#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/routing/SchoolDaysKtrfAdapter.h>
#include <Kotonoha/routing/SchoolDaysKtrfAssetResolver.h>
}

#include <Kotonoha/routing/SchoolDaysHistory.hpp>
#include <Kotonoha/routing/SchoolDaysPersistence.hpp>

namespace Kotonoha {

struct SchoolDaysKtrfScene {
    uint32_t nodeIndex = KOTONOHA_KTRF_NULL_INDEX;
    uint32_t resourceIndex = KOTONOHA_KTRF_NULL_INDEX;
    std::string sceneKey;
    std::string orsPath;
};

class SchoolDaysKtrfSession {
public:
    enum class StepKind {
        Unresolved,
        BlockedChoice,
        Scene,
        Terminal,
    };

    struct StepResult {
        StepKind kind = StepKind::Unresolved;
        uint32_t transitionIndex = KOTONOHA_KTRF_NULL_INDEX;
        uint32_t hopCount = 0;
        SchoolDaysKtrfScene scene;
        std::vector<std::string> hooks;
        std::vector<int64_t> endings;

        bool HasHook(const std::string& symbol) const;
    };

    enum class BackKind {
        Unresolved,
        Scene,
        Boundary,
        TrialPv,
        NoScript,
    };

    struct BackPlan {
        BackKind kind = BackKind::Unresolved;
        SchoolDaysCoordinate source;
        // Exact RouteProc predecessor. This can be a KTRF routing-only node
        // with no ORS asset; destination is always the physical scene that
        // will actually be presented after collapsing any such dispatcher.
        SchoolDaysCoordinate semanticPredecessor;
        SchoolDaysCoordinate destination;
        SchoolDaysKtrfScene scene;
        SchoolDaysHistory::Entry historyEntry;
        std::vector<std::string> hooks;
    };

    struct LoadPlan {
        SchoolDaysPersistence::SaveImage image;
        SchoolDaysCoordinate destination;
        SchoolDaysKtrfScene scene;
    };

    struct RouteMapCheckpointPlan {
        int selector = -1;
        SchoolDaysCoordinate destination;
        SchoolDaysKtrfScene scene;
    };

    SchoolDaysKtrfSession();
    ~SchoolDaysKtrfSession();

    SchoolDaysKtrfSession(const SchoolDaysKtrfSession&) = delete;
    SchoolDaysKtrfSession& operator=(const SchoolDaysKtrfSession&) = delete;

    bool Open(const char* ktrfPath, const char* assetsRoot,
              Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return opened; }
    bool ResetNewGame(SchoolDaysKtrfScene* scene, Kotonoha_KtrfError* error);
    bool CurrentScene(SchoolDaysKtrfScene* scene, Kotonoha_KtrfError* error) const;
    bool CommitChoice(int64_t value, int* accepted, Kotonoha_KtrfError* error);
    bool Advance(const char* trigger, StepResult* out, Kotonoha_KtrfError* error);
    // Complete the RouteProc terminal helper after callback_38 has fired:
    // FLAG_LOGO=false (global bool), callback_2C/reset, ROUTE=-1 (local int),
    // and an empty outgoing script name.  This is intentionally separate from
    // generic KTRF terminal handling because the frozen route IR does not carry
    // these host-side effects.
    bool CommitTerminalRoutingReset(StepResult* terminal,
                                    Kotonoha_KtrfError* error);
    bool PlanBack(BackPlan* plan, Kotonoha_KtrfError* error);
    bool CommitBack(BackPlan* plan, Kotonoha_KtrfError* error);

    bool LoadGlobalFlag(const std::vector<uint8_t>& bytes,
                        Kotonoha_KtrfError* error);
    bool SerializeGlobalFlag(std::vector<uint8_t>* bytes,
                             Kotonoha_KtrfError* error) const;
    bool PlanLoadSave(const std::vector<uint8_t>& bytes, LoadPlan* plan,
                      Kotonoha_KtrfError* error) const;
    bool CommitLoadSave(const LoadPlan& plan, Kotonoha_KtrfError* error);
    bool PlanRouteMapCheckpoint(int selector, RouteMapCheckpointPlan* plan,
                                Kotonoha_KtrfError* error) const;
    bool CommitRouteMapCheckpoint(const RouteMapCheckpointPlan& plan,
                                  Kotonoha_KtrfError* error);

    // Debug-only scene support. ResolveSceneNode validates and probes the
    // physical ORS without changing router state. DebugActivateNode then moves
    // the router to that physical node while preserving runtime variables.
    bool ResolveSceneNode(uint32_t nodeIndex, SchoolDaysKtrfScene* scene,
                          Kotonoha_KtrfError* error) const;
    // Read-only SceneKey lookup used by isolated frontend/replay playback.
    // This resolves and probes a physical ORS without activating the router or
    // mutating StateStore/History/Persistence.
    bool ResolveSceneKey(const std::string& sceneKey, SchoolDaysKtrfScene* scene,
                         Kotonoha_KtrfError* error) const;
    bool DebugActivateNode(uint32_t nodeIndex, Kotonoha_KtrfError* error);

    const std::string& AssetsRoot() const { return assetsRoot; }
    const Kotonoha_SchoolDaysKtrfAdapter* Adapter() const { return &adapter; }
    Kotonoha_SchoolDaysKtrfAdapter* MutableAdapter() { return &adapter; }
    const SchoolDaysStateStore& State() const { return state; }
    SchoolDaysStateStore& State() { return state; }
    const SchoolDaysHistory& History() const { return history; }
    SchoolDaysHistory& History() { return history; }
    const SchoolDaysPersistence& Persistence() const { return persistence; }
    SchoolDaysPersistence& Persistence() { return persistence; }
    bool ConsumeTopMeterRefreshRequest() {
        const bool requested = topMeterRefreshRequested;
        topMeterRefreshRequested = false;
        return requested;
    }

private:
    static int OnEnding(int64_t endingCode, void* userdata,
                        Kotonoha_KtrfError* error);
    static int OnHook(const char* symbol, size_t symbolLength, void* userdata,
                      Kotonoha_KtrfError* error);
    static int OnEffectApplied(uint32_t effectIndex, void* userdata,
                               Kotonoha_KtrfError* error);

    bool ResolveOrsPhysicalPath(Kotonoha_SchoolDaysKtrfOrsAsset* asset,
                                Kotonoha_KtrfError* error) const;
    bool FillScene(const Kotonoha_SchoolDaysKtrfOrsAsset& asset,
                   SchoolDaysKtrfScene* out) const;
    bool ResolveSemanticCoordinate(int route, int scene,
                                   SchoolDaysCoordinate* coordinate,
                                   SchoolDaysKtrfScene* resolvedScene,
                                   bool* routingOnly,
                                   Kotonoha_KtrfError* error) const;

    Kotonoha_KtrfDocument document{};
    Kotonoha_SchoolDaysKtrfAdapter adapter{};
    SchoolDaysStateStore state;
    SchoolDaysHistory history;
    SchoolDaysPersistence persistence;
    std::string assetsRoot;
    std::vector<std::string> pendingHooks;
    std::vector<int64_t> pendingEndings;
    bool topMeterRefreshRequested = false;
    bool opened = false;
};

} // namespace Kotonoha
