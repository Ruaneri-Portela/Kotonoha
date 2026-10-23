#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <Kotonoha/routing/SchoolDaysFlowController.hpp>
#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>

struct Kotonoha_Game;

namespace Kotonoha {

class Gameplay;
class Kotonoha;
class SchoolDaysPlayDataSession;
class SchoolDaysReplayPlaybackSession;

class SchoolDaysKtrfAppController {
public:
    struct DebugSceneEntry {
        uint32_t nodeIndex = KOTONOHA_KTRF_NULL_INDEX;
        std::string sceneKey;
    };

    // Narrow, read-only projection for MENU::RouteMap.  The presenter does not
    // own ROUTE/SCENE/SP state and never receives a StateStore reference.
    struct RouteMapProjection {
        int major = 0;
        int subpage = 0;
        int currentMarker = -1;
        bool alternateHost = false;
        std::array<bool, 19> available{};
    };

    SchoolDaysKtrfAppController() = default;
    ~SchoolDaysKtrfAppController();

    SchoolDaysKtrfAppController(const SchoolDaysKtrfAppController&) = delete;
    SchoolDaysKtrfAppController& operator=(const SchoolDaysKtrfAppController&) = delete;

    bool Open(const char* ktrfPath, const char* orsRoot,
              Kotonoha* engine, Kotonoha_Game* gameContext,
              Kotonoha_KtrfError* error);
    void Close();

    bool IsOpen() const { return opened; }
    bool HasPendingHandoff() const { return bridge.HasPendingHandoff(); }
    bool PendingHandoffIsTerminal() const {
        return bridge.PendingHandoffIsTerminal();
    }
    SchoolDaysFlowController::State FlowState() const {
        return flow.CurrentState();
    }
    bool FlowPromptVisible() const { return flow.IsPromptVisible(); }

    Gameplay* CurrentGameplay() const { return bridge.CurrentGameplay(); }
    const SchoolDaysKtrfScene& CurrentScene() const { return bridge.CurrentScene(); }
    const std::string& AssetsRoot() const { return bridge.Session().AssetsRoot(); }
    bool ReadSessionInt(const std::string& symbol, int64_t* value,
                        Kotonoha_KtrfError* error) const {
        return bridge.Session().State().ReadSessionInt(symbol, value, error);
    }
    bool ReadGlobalInt(const std::string& symbol, int64_t* value,
                       Kotonoha_KtrfError* error) const {
        return bridge.Session().State().ReadGlobalInt(symbol, value, error);
    }
    bool ConsumeTopMeterRefreshRequest() {
        return bridge.Session().ConsumeTopMeterRefreshRequest();
    }

    std::size_t DebugSceneCount() const { return debugScenes.size(); }
    const DebugSceneEntry* DebugSceneAt(std::size_t ordinal) const;
    int DebugCurrentSceneOrdinal() const;
    bool DebugJumpToScene(std::size_t ordinal, Kotonoha_KtrfError* error);
    bool DebugJumpRelative(int delta, Kotonoha_KtrfError* error);

    // Development inspection only. This exposes the already-bound KTRF adapter
    // so the debug UI can observe typed variable metadata and live runtime
    // values without changing routing semantics.
    const Kotonoha_KtrfAdapter* DebugAdapter() const {
        return bridge.Session().Adapter();
    }

    bool SubmitSaveFeelResult(SchoolDaysFlowController::SaveFeelResult result,
                              Kotonoha_KtrfError* error);
    bool SubmitSaveLoadResult(SchoolDaysFlowController::SaveLoadResult result,
                              Kotonoha_KtrfError* error);
    bool EnterGameplaySave(Kotonoha_KtrfError* error);
    bool EnterGameplayLoad(Kotonoha_KtrfError* error);
    bool EnterGameplayConfig(Kotonoha_KtrfError* error);
    bool EnterGameplayExit(Kotonoha_KtrfError* error);
    bool SkipToNextChoice(Kotonoha_KtrfError* error);
    bool EnterGameplayRouteMap(Kotonoha_KtrfError* error);
    bool ReturnGameplayRouteMapToLoad(Kotonoha_KtrfError* error);
    bool CloseGameplayModal(Kotonoha_KtrfError* error);
    bool SubmitGameplayExit(bool confirm, bool* returnToTitle,
                            Kotonoha_KtrfError* error);
    bool EnterTitleLoad(Kotonoha_KtrfError* error);
    bool CancelTitleLoad(Kotonoha_KtrfError* error);
    bool EnterTitleRouteMap(Kotonoha_KtrfError* error);
    bool ReturnTitleRouteMapToLoad(Kotonoha_KtrfError* error);
    bool LoadRouteMapSelector(int selector, Kotonoha_KtrfError* error);
    bool StartNewGame(int selector, Kotonoha_KtrfError* error);
    bool EnterStartupTitle(Kotonoha_KtrfError* error);
    bool EnterTitleConfig(Kotonoha_KtrfError* error);
    bool CloseTitleConfig(Kotonoha_KtrfError* error);
    bool EnterTitleExit(Kotonoha_KtrfError* error);
    bool SubmitTitleExit(bool confirm, bool* exitApplication,
                         Kotonoha_KtrfError* error);

    // Filesystem/UI-neutral persistence boundary.  The future SchoolDaysSystemUi
    // owns path/slot selection and external I/O; this controller owns applying
    // loaded state to the live bridge and committing a save plan only after the
    // caller reports that the planned files were written successfully.
    bool LoadGlobalFlag(const std::vector<uint8_t>& bytes,
                        Kotonoha_KtrfError* error);
    bool HasGlobalFlag() const;
    bool SerializeGlobalFlag(std::vector<uint8_t>* bytes,
                             Kotonoha_KtrfError* error) const;
    bool LoadSaveFile(const std::vector<uint8_t>& bytes,
                      Kotonoha_KtrfError* error);
    bool PlanSaveSlot(int slot, const std::u16string& primaryMetadata,
                      const std::u16string& subMetadata,
                      SchoolDaysSaveService::Plan* plan,
                      Kotonoha_KtrfError* error) const;
    bool PlanSaveSlot(const SchoolDaysSaveProfile& profile, int slot,
                      const std::u16string& primaryMetadata,
                      const std::u16string& subMetadata,
                      SchoolDaysSaveService::Plan* plan,
                      Kotonoha_KtrfError* error) const;
    bool CommitSavePlan(const SchoolDaysSaveService::Plan& plan,
                        Kotonoha_KtrfError* error);
    bool ReadSaveSlotMetadata(
        const SchoolDaysSaveProfile& profile, int slot, bool fileOpenable,
        SchoolDaysSaveService::SlotMetadata* metadata,
        Kotonoha_KtrfError* error) const;
    bool BuildSaveMetadata(const SchoolDaysSaveMetadata::LocalTime& time,
                           bool useEnglish,
                           const std::u16string& comment,
                           SchoolDaysSaveMetadata::Result* result,
                           Kotonoha_KtrfError* error) const;

    // Narrow TitleView state boundary.  Keep the StateStore private while
    // allowing the system-UI layer to reproduce the authenticated C4
    // end-background decision and the independently-proved EndClear replay
    // availability gate.
    bool ResolveTitleBackground(
        const SchoolDaysEndBackgroundResolver::Profile& profile,
        SchoolDaysEndBackgroundResolver::Result* result,
        Kotonoha_KtrfError* error);
    bool TitleEndClearAvailable(bool* available,
                                Kotonoha_KtrfError* error) const;
    bool BuildInitialRouteMapProjection(RouteMapProjection* projection,
                                        Kotonoha_KtrfError* error) const;
    bool BuildRouteMapProjection(int major, int subpage,
                                 RouteMapProjection* projection,
                                 Kotonoha_KtrfError* error) const;
    bool StartOrdinaryReplayPlayback(int hSceneIndex,
                                     SchoolDaysReplayPlaybackSession* playback,
                                     Kotonoha_KtrfError* error);
    bool StartSpecialReplayPlayback(int hSceneIndex, int choice,
                                    SchoolDaysReplayPlaybackSession* playback,
                                    Kotonoha_KtrfError* error);
    bool StartPlayDataPlayback(const std::vector<uint8_t>& saveBytes,
                               SchoolDaysPlayDataSession* playback,
                               Kotonoha_KtrfError* error);

    // Development convenience: equivalent to explicitly choosing the proven
    // SaveFeel result 0 (skip save) and then executing the resulting action.
    bool ContinueHandoff(Kotonoha_KtrfError* error);
    SDL_AppResult Main(Gameplay** out, bool sceneAdvancePrepared);

private:
    static Gameplay* CreateGameplay(const char* orsPath,
                                    Kotonoha_Game* gameContext,
                                    void* userdata);
    static void DestroyGameplay(Gameplay* gameplay, void* userdata);
    static bool ResolveReplayScene(const char* sceneKey,
                                   SchoolDaysKtrfScene* scene,
                                   void* userdata,
                                   Kotonoha_KtrfError* error);

    bool BuildDebugSceneCatalog(Kotonoha_KtrfError* error);
    bool ExecuteFlowAction(SchoolDaysFlowController::Action action,
                           Kotonoha_KtrfError* error);

    Kotonoha* engine = nullptr;
    Kotonoha_Game* gameContext = nullptr;
    SchoolDaysKtrfGameplayBridge bridge;
    SchoolDaysFlowController flow;
    std::vector<DebugSceneEntry> debugScenes;
    std::string ktrfPath;
    std::string orsRoot;
    bool opened = false;
};

} // namespace Kotonoha
