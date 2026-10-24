#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>
#include <Kotonoha/routing/SchoolDaysFontData.hpp>
#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysTitleModel.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysSomConSetModel.hpp>
#include <Kotonoha/routing/SchoolDaysExitViewModel.hpp>
#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>
#include <Kotonoha/routing/SchoolDaysReplayRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapRenderPlan.hpp>

struct Kotonoha_Time;
struct Kotonoha_videoData;

namespace Kotonoha {

// Presentation-only School Days system UI.  Semantic outer flow remains in
// SchoolDaysFlowController; this object owns only extracted UI resources,
// native coordinate transforms, pointer interaction and the pure SaveFeel
// model.  SaveLoad/Title presenters can be added here without moving KTRF or
// persistence authority into the UI layer.
class SchoolDaysSystemUi {
public:
    enum class UiSfxCue : int {
        None = -1,
        Cancel = 0,
        Select = 1,
        Click = 2,
        Up = 3,
        Down = 4,
        View = 5,
        Open = 6,
    };

    struct Output {
        bool hasSaveFeelResult = false;
        SchoolDaysSaveFeelModel::Result saveFeelResult =
            SchoolDaysSaveFeelModel::Result::Skip;
        bool hasSaveLoadOutput = false;
        SchoolDaysSaveLoadModel::Output saveLoadOutput{};
        bool hasTitleActivation = false;
        SchoolDaysTitleModel::Activation titleActivation{};
        bool hasConfigAction = false;
        SchoolDaysConfigMenuModel::Action configAction{};
        bool hasSomConSetReturn = false;
        bool hasExitActivation = false;
        SchoolDaysExitViewModel::Activation exitActivation{};
        bool hasReplayAction = false;
        SchoolDaysReplayModel::Action replayAction{};
        bool hasRouteMapAction = false;
        SchoolDaysRouteMapModel::Action routeMapAction{};
        UiSfxCue uiSfx = UiSfxCue::None;
    };

    struct Layout {
        SchoolDaysSaveFeelModel::Variant variant =
            SchoolDaysSaveFeelModel::Variant::Standard;
        float nativeScale = 1.0f;
        float nativeYOffset = 75.0f;
    };

    SchoolDaysSystemUi() = default;
    ~SchoolDaysSystemUi();

    SchoolDaysSystemUi(const SchoolDaysSystemUi&) = delete;
    SchoolDaysSystemUi& operator=(const SchoolDaysSystemUi&) = delete;

    static Layout LayoutForVariant(SchoolDaysSaveFeelModel::Variant variant);

    bool OpenSaveFeel(SDL_Renderer* renderer, const std::string& assetsRoot,
                      SchoolDaysSaveFeelModel::Variant variant,
                      std::string* error = nullptr);
    bool OpenSaveLoad(SDL_Renderer* renderer, const std::string& assetsRoot,
                      SchoolDaysSaveLoadModel::Variant variant,
                      SchoolDaysSaveLoadModel::Mode mode,
                      bool textInputEnabled, bool useEnglish,
                      const std::string& fontDataLogicalPath,
                      std::string* error = nullptr);
    bool OpenSaveLoad(SDL_Renderer* renderer, const std::string& assetsRoot,
                      SchoolDaysSaveLoadModel::Variant variant,
                      bool textInputEnabled, bool useEnglish,
                      const std::string& fontDataLogicalPath,
                      std::string* error = nullptr) {
        return OpenSaveLoad(renderer, assetsRoot, variant,
                            SchoolDaysSaveLoadModel::Mode::Save,
                            textInputEnabled, useEnglish,
                            fontDataLogicalPath, error);
    }
    bool SetSaveLoadMetadata(
        const std::vector<SchoolDaysSaveService::SlotMetadata>& rows,
        std::string* error = nullptr);
    bool OpenTitle(
        SDL_Renderer* renderer, const std::string& assetsRoot,
        SchoolDaysTitleModel::Variant variant,
        const SchoolDaysTitleModel::HostState& hostState,
        const SchoolDaysEndBackgroundResolver::Result& background,
        const std::string& baseBackgroundAsset,
        std::string* error = nullptr);
    bool OpenConfig(
        SDL_Renderer* renderer, const std::string& assetsRoot,
        SchoolDaysConfigMenuModel::Variant variant,
        SchoolDaysConfigMenuModel::Page page,
        const SchoolDaysConfigMenuRenderPlan::VisualState& visualState,
        std::string* error = nullptr);
    bool OpenSomConSet(SDL_Renderer* renderer, const std::string& assetsRoot,
                       SchoolDaysSomConSetModel::Variant variant,
                       std::string* error = nullptr);
    bool SetConfigVisualState(
        SDL_Renderer* renderer, const std::string& assetsRoot,
        const SchoolDaysConfigMenuRenderPlan::VisualState& visualState,
        std::string* error = nullptr);
    bool OpenExit(SDL_Renderer* renderer, const std::string& assetsRoot,
                  SchoolDaysExitViewModel::Variant variant,
                  SchoolDaysExitViewModel::BaseArtwork artwork =
                      SchoolDaysExitViewModel::BaseArtwork::Exit,
                  std::string* error = nullptr);
    bool OpenReplay(SDL_Renderer* renderer, const std::string& assetsRoot,
                    SchoolDaysReplayModel::Variant variant,
                    std::string* error = nullptr);
    bool OpenRouteMap(SDL_Renderer* renderer, const std::string& assetsRoot,
                      SchoolDaysRouteMapModel::Variant variant,
                      int initialMajor, int initialSubpage,
                      bool alternateHost, int completionPercent,
                      bool useEnglish, const std::string& fontDataLogicalPath,
                      std::string* error = nullptr);
    void SetReplayHSceneAvailable(int index, bool available) {
        replay_.SetHSceneAvailable(index, available);
    }
    void SetReplayPlayDataAvailable(int index, bool available) {
        replay_.SetPlayDataAvailable(index, available);
    }
    void SetReplaySceneCheckChoiceAvailable(int choice, bool available) {
        replay_.SetSceneCheckChoiceAvailable(choice, available);
    }
    void SetRouteMapNodeAvailable(int localIndex, bool available) {
        routeMap_.SetNodeAvailable(localIndex, available);
    }
    void SetRouteMapNodeHistory(int localIndex, bool seen) {
        routeMap_.SetNodeHistory(localIndex, seen);
    }
    void SetRouteMapCurrentMarker(int marker) {
        routeMap_.SetCurrentMarker(marker);
    }
    void Close();

    bool HandleEvent(SDL_Renderer* renderer, const SDL_Event* event,
                     Output* output);
    bool SubmitSaveLoadComment(const std::u16string& comment);
    bool CancelSaveLoadComment();
    bool Activate(Output* output);
    void Previous();
    void Next();
    void Update(uint32_t nowMs, int64_t feeling001, int64_t feeling002,
                Output* output);
    bool Render(SDL_Renderer* renderer, std::string* error = nullptr);

    bool IsSaveFeelOpen() const { return saveFeel_.IsOpen(); }
    bool IsSaveLoadOpen() const { return saveLoad_.IsOpen(); }
    bool IsTitleOpen() const { return title_.IsOpen(); }
    bool IsConfigOpen() const { return configMenu_.IsOpen(); }
    bool IsSomConSetOpen() const { return somConSet_.IsOpen(); }
    bool IsExitOpen() const { return exitView_.IsOpen(); }
    bool IsReplayOpen() const { return replay_.IsOpen(); }
    bool IsRouteMapOpen() const { return routeMap_.IsOpen(); }
    const SchoolDaysSaveFeelModel& SaveFeel() const { return saveFeel_; }
    const SchoolDaysSaveLoadModel& SaveLoad() const { return saveLoad_; }
    const SchoolDaysTitleModel& Title() const { return title_; }
    const SchoolDaysConfigMenuModel& ConfigMenu() const { return configMenu_; }
    const SchoolDaysSomConSetModel& SomConSet() const { return somConSet_; }
    const SchoolDaysExitViewModel& ExitView() const { return exitView_; }
    const SchoolDaysReplayModel& Replay() const { return replay_; }
    const SchoolDaysRouteMapModel& RouteMap() const { return routeMap_; }

private:
    struct Viewport {
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
    };

    static Viewport ComputeViewport(int outputWidth, int outputHeight,
                                    int virtualWidth, int virtualHeight);
    bool PointerToVirtual(SDL_Renderer* renderer, float x, float y,
                          int* virtualX, int* virtualY) const;
    static SDL_FRect TransformRect(const SDL_FRect& native,
                                   const Viewport& viewport);
    bool EmitModelResult(SchoolDaysSaveFeelModel::Result result, Output* output);
    void DestroyTextures();
    bool DrawTitleBackground(SDL_Renderer* renderer,
                             const Viewport& viewport,
                             std::string* error);
    bool EnsureReplayTextures(SDL_Renderer* renderer, std::string* error);
    bool EnsureRouteMapTextures(SDL_Renderer* renderer, std::string* error);
    bool UpdateRouteMapCompletionTexture(std::string* error);

    SchoolDaysSaveFeelModel saveFeel_;
    SchoolDaysSaveLoadModel saveLoad_;
    SchoolDaysTitleModel title_;
    SchoolDaysConfigMenuModel configMenu_;
    SchoolDaysSomConSetModel somConSet_;
    SchoolDaysExitViewModel exitView_;
    SchoolDaysReplayModel replay_;
    SchoolDaysRouteMapModel routeMap_;
    SchoolDaysConfigMenuRenderPlan::VisualState configVisualState_{};
    Layout layout_{};
    SDL_Texture* baseTexture_ = nullptr;
    SDL_Texture* chipTexture_ = nullptr;
    SDL_Texture* titleBackgroundTexture_ = nullptr;
    Kotonoha_Time* titleVideoClock_ = nullptr;
    Kotonoha_videoData* titleVideo_ = nullptr;
    uint64_t titleVideoStartMs_ = 0u;
    float titleBackgroundHeight_ = 450.0f;
    SDL_Texture* saveLoadTextTexture_ = nullptr;
    SDL_Texture* routeMapTextTexture_ = nullptr;
    SDL_Texture* replayThumbnailTexture_ = nullptr;
    SDL_Texture* replayPopupBaseTexture_ = nullptr;
    SDL_Texture* replayPopupChipTexture_ = nullptr;
    std::string replayAssetsRoot_;
    SchoolDaysReplayModel::View replayLoadedView_ =
        SchoolDaysReplayModel::View::HScene;
    int replayLoadedPage_ = -1;
    int replayLoadedSceneCheckCount_ = -1;
    std::string routeMapAssetsRoot_;
    int routeMapLoadedMajor_ = -1;
    int routeMapLoadedSubpage_ = -1;
    SchoolDaysFontData saveLoadFont_;
    SchoolDaysFontData routeMapFont_;
    std::vector<uint32_t> saveLoadTextPixels_;
    std::vector<uint32_t> routeMapTextPixels_;
    std::vector<SchoolDaysSaveService::SlotMetadata> saveLoadMetadata_;
    std::vector<int> saveLoadCommentAdvances_;
    bool saveLoadUseEnglish_ = false;
    bool routeMapUseEnglish_ = false;
    bool resultEmitted_ = false;
};

} // namespace Kotonoha
