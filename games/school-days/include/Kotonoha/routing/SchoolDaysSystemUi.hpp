#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadModel.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>
#include <Kotonoha/routing/SchoolDaysFontData.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuModel.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysExitViewModel.hpp>
#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>
#include <Kotonoha/routing/SchoolDaysReplayRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapRenderPlan.hpp>

namespace Kotonoha {

class AssetProvider;

// Presentation-only School Days system UI (SaveFeel, SaveLoad, Config, Exit,
// Replay and RouteMap panels drawn from the retail System artwork). The runtime
// keeps the semantic flow; this object owns the UI textures, the native
// coordinate transform and pointer interaction. Coordinates are those of the
// logical area set with SetArea() (the game viewport). The title screen is
// drawn by the runtime itself.
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
        bool hasConfigAction = false;
        SchoolDaysConfigMenuModel::Action configAction{};
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

    explicit SchoolDaysSystemUi(const AssetProvider* assets = nullptr)
        : assets_(assets) {}
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
    bool OpenConfig(
        SDL_Renderer* renderer, const std::string& assetsRoot,
        SchoolDaysConfigMenuModel::Variant variant,
        SchoolDaysConfigMenuModel::Page page,
        const SchoolDaysConfigMenuRenderPlan::VisualState& visualState,
        std::string* error = nullptr);
    bool SetConfigVisualState(
        const SchoolDaysConfigMenuRenderPlan::VisualState& visualState,
        std::string* error = nullptr);
    bool OpenExit(SDL_Renderer* renderer, const std::string& assetsRoot,
                  SchoolDaysExitViewModel::Variant variant,
                  SchoolDaysExitViewModel::BaseArtwork artwork =
                      SchoolDaysExitViewModel::BaseArtwork::Exit,
                  std::string* error = nullptr);
    bool OpenReplay(SDL_Renderer* renderer, const std::string& assetsRoot,
                    SchoolDaysReplayModel::Variant variant,
                    bool textInputEnabled, bool useEnglish,
                    const std::string& fontDataLogicalPath,
                    std::string* error = nullptr);
    bool SetReplayPlayDataMetadata(
        const std::vector<SchoolDaysSaveService::SlotMetadata>& slots,
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
#if defined(__ANDROID__) || defined(ANDROID)
    bool MobileSwipePage(int delta) {
        if (saveLoad_.IsOpen()) return saveLoad_.MobileSwipePage(delta);
        if (replay_.IsOpen()) return replay_.MobileSwipePage(delta);
        return false;
    }
#endif
    bool SubmitSaveLoadComment(const std::u16string& comment);
    bool CancelSaveLoadComment();
    bool Activate(Output* output);
    void Previous();
    void Next();
    void Update(uint32_t nowMs, int64_t feeling001, int64_t feeling002,
                Output* output);
    bool Render(SDL_Renderer* renderer, std::string* error = nullptr);
    // Logical size events and rendering use (defaults to 1280x720).
    void SetArea(SDL_Point area) { area_ = area; }
    // In FULL+STANDARD, stretch the 800x600 authored host canvas across the
    // physical presentation: its opaque 800x450 image still occupies only the
    // centered 75% height, leaving black bars like the original screenshot.
    // This affects only Config, not the engine-wide SettingsMenu or other
    // School Days panels; CMAP hit-testing uses the identical transform.
    void SetConfigStandardFullscreenStretch(bool enabled) {
        configStandardFullscreenStretch_ = enabled;
    }
    void SetAssetProvider(const AssetProvider* assets) { assets_ = assets; }
    bool IsOpen() const {
        return saveFeel_.IsOpen() || saveLoad_.IsOpen() || configMenu_.IsOpen() ||
               exitView_.IsOpen() || replay_.IsOpen() || routeMap_.IsOpen();
    }

    bool IsSaveFeelOpen() const { return saveFeel_.IsOpen(); }
    bool IsSaveLoadOpen() const { return saveLoad_.IsOpen(); }
    bool IsConfigOpen() const { return configMenu_.IsOpen(); }
    bool IsExitOpen() const { return exitView_.IsOpen(); }
    bool IsReplayOpen() const { return replay_.IsOpen(); }
    bool IsRouteMapOpen() const { return routeMap_.IsOpen(); }
    bool IsSaveLoadCommentEditorOpen() const { return saveLoadCommentEditorOpen_; }
    const std::u16string& SaveLoadCommentEditorText() const {
        return saveLoadCommentText_;
    }
    const SchoolDaysSaveFeelModel& SaveFeel() const { return saveFeel_; }
    const SchoolDaysSaveLoadModel& SaveLoad() const { return saveLoad_; }
    void RestoreSaveLoadSelection(int selection) {
        saveLoad_.RestoreSelection(selection);
    }
    const SchoolDaysConfigMenuModel& ConfigMenu() const { return configMenu_; }
    const SchoolDaysExitViewModel& ExitView() const { return exitView_; }
    const SchoolDaysReplayModel& Replay() const { return replay_; }
    const SchoolDaysRouteMapModel& RouteMap() const { return routeMap_; }

private:
    struct Viewport {
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
        float scaleY = 1.0f;
    };

    static Viewport ComputeViewport(int outputWidth, int outputHeight,
                                    int virtualWidth, int virtualHeight);
    Viewport ComputeConfigViewport(int outputWidth, int outputHeight,
                                   int virtualWidth, int virtualHeight) const;
    bool PointerToVirtual(SDL_Renderer* renderer, float x, float y,
                          int* virtualX, int* virtualY) const;
    static SDL_FRect TransformRect(const SDL_FRect& native,
                                   const Viewport& viewport);
    bool EmitModelResult(SchoolDaysSaveFeelModel::Result result, Output* output);
    bool BeginSaveLoadCommentEditor(SDL_Renderer* renderer, int slot);
    void EndSaveLoadCommentEditorTextInput();
    bool HandleSaveLoadCommentEditorEvent(const SDL_Event* event);
    bool UpdateSaveLoadCommentEditorTexture(std::string* error);
    bool RenderSaveLoadCommentEditor(SDL_Renderer* renderer,
                                     std::string* error);
    void DestroyTextures();
    SDL_Texture* LoadTexture(SDL_Renderer* renderer,
                             const std::string& physical) const;
    bool EnsureReplayTextures(SDL_Renderer* renderer, std::string* error);
    bool UpdateReplayMetadataTexture(std::string* error);
    bool EnsureRouteMapTextures(SDL_Renderer* renderer, std::string* error);
    bool UpdateRouteMapCompletionTexture(std::string* error);

    SchoolDaysSaveFeelModel saveFeel_;
    SchoolDaysSaveLoadModel saveLoad_;
    SchoolDaysConfigMenuModel configMenu_;
    SchoolDaysExitViewModel exitView_;
    SchoolDaysReplayModel replay_;
    SchoolDaysRouteMapModel routeMap_;
    SchoolDaysConfigMenuRenderPlan::VisualState configVisualState_{};
    Layout layout_{};
    SDL_Texture* baseTexture_ = nullptr;
    SDL_Texture* chipTexture_ = nullptr;
    const AssetProvider* assets_ = nullptr;
    SDL_Point area_{1280, 720};
    bool configStandardFullscreenStretch_ = false;
    SDL_Texture* saveLoadTextTexture_ = nullptr;
    SDL_Texture* saveLoadCommentTextTexture_ = nullptr;
    SDL_Texture* routeMapTextTexture_ = nullptr;
    SDL_Texture* replayTextTexture_ = nullptr;
    SDL_Texture* replayThumbnailTexture_ = nullptr;
    SDL_Texture* replayPopupBaseTexture_ = nullptr;
    SDL_Texture* replayPopupChipTexture_ = nullptr;
    std::string replayAssetsRoot_;
    SchoolDaysReplayModel::View replayLoadedView_ =
        SchoolDaysReplayModel::View::HScene;
    int replayLoadedPage_ = -1;
    int replayTextLoadedPage_ = -1;
    int replayLoadedSceneCheckCount_ = -1;
    std::string routeMapAssetsRoot_;
    int routeMapLoadedMajor_ = -1;
    int routeMapLoadedSubpage_ = -1;
    SchoolDaysFontData saveLoadFont_;
    SchoolDaysFontData routeMapFont_;
    SchoolDaysFontData replayFont_;
    std::vector<uint32_t> saveLoadTextPixels_;
    std::vector<uint32_t> saveLoadCommentTextPixels_;
    std::vector<uint32_t> routeMapTextPixels_;
    std::vector<uint32_t> replayTextPixels_;
    std::vector<SchoolDaysSaveService::SlotMetadata> saveLoadMetadata_;
    std::vector<SchoolDaysSaveService::SlotMetadata> replayMetadata_;
    std::vector<int> saveLoadCommentAdvances_;
    bool saveLoadCommentEditorOpen_ = false;
    int saveLoadCommentSlot_ = -1;
    std::u16string saveLoadCommentText_;
    SDL_Window* saveLoadCommentWindow_ = nullptr;
    bool saveLoadCommentTextDirty_ = false;
    uint32_t saveLoadCommentCursorPhase_ = UINT32_MAX;
    bool saveLoadUseEnglish_ = false;
    bool routeMapUseEnglish_ = false;
    bool replayTextInputEnabled_ = false;
    bool replayUseEnglish_ = false;
    bool resultEmitted_ = false;
};

} // namespace Kotonoha
