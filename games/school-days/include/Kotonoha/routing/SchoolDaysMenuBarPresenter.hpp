#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include <SDL3/SDL.h>

#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>
#include <Kotonoha/routing/SchoolDaysTopMeterModel.hpp>

namespace Kotonoha {

class AssetProvider;

// Resolution-independent wrapper for FILM::MenuBar's authenticated feeling
// meter. Retail pixel coordinates remain logical authoring coordinates; final
// placement is aspect-fitted into the current SDL render output.
class SchoolDaysMenuBarPresenter {
public:
    using Variant = SchoolDaysSaveFeelModel::Variant;

    struct Layout {
        int canvasWidth = 800;
        int canvasHeight = 600;
        float layoutScale = 1.0f;
        float barWidth = 800.0f;
        float barHeight = 75.0f;
    };

    struct Viewport {
        float x = 0.0f;
        float y = 0.0f;
        float scale = 0.0f;
    };

    struct ChipRecord {
        SDL_FRect destination{};
        SDL_FRect source{};
    };

    struct RenderVisibility {
        bool meter = false;
        bool chrome = false;
    };

    struct PersistentTransport {
        bool selectedSpeed = false;
        bool autoOn = false;
    };

    enum class ActionKind {
        None,
        ToggleAutoMode,
        TogglePause,
        BackOrRestartScene,
        SkipToEnd,
        SkipToNextChoice,
        SetPlaybackSpeed,
        SetReplayIndicatorOpacity,
        OpenSave,
        OpenLoad,
        OpenBackLog,
        OpenConfig,
        OpenMainMenuConfirm,
    };

    struct Action {
        ActionKind kind = ActionKind::None;
        int control = -1;
        int value = -1;
    };

    SchoolDaysMenuBarPresenter() = default;
    ~SchoolDaysMenuBarPresenter();

    SchoolDaysMenuBarPresenter(const SchoolDaysMenuBarPresenter&) = delete;
    SchoolDaysMenuBarPresenter& operator=(const SchoolDaysMenuBarPresenter&) = delete;

    static Layout LayoutForVariant(Variant variant);
    static Viewport FitViewport(int outputWidth, int outputHeight,
                                const Layout& layout);
    static SDL_FRect BackLogControlRect(Variant variant);
    // Raw retail CMAP stores control+1. These helpers expose the authenticated
    // internal 0..24 control geometry and MenuBar_Chip record table.
    static SDL_FRect ControlRect(int control, Variant variant);
    static ChipRecord ControlChip(int control, Variant variant);
    // FILMEngine speed index (0..4) selects native CHIP record 5+index.
    // In retail the default x1 is unlit while x2/x4/x16/x32 keep their
    // orange selection outside pointer hover.
    static ChipRecord SelectedSpeedChip(int speedIndex, bool speedControlsEnabled,
                                        bool transportLocked, Variant variant);
    static ChipRecord NormalSystemControlChip(int control, Variant variant);
    static ChipRecord FixedChipRecord(int record, Variant variant);
    // SysMenuSDHQ.dll FUN_100262E0: the native hover-help sprite selected for
    // each control. Controls5..9 share one help record; controls15..24 share
    // the repeat-indicator transparency help record.
    static ChipRecord HoverHelpChip(int control, Variant variant);
    // SysMenuSDHQ.dll 0x10026ED0 maps controls15..24 to the internal opacity
    // level set {0,2,3,4,5,6,7,8,9,10}.  Alpha is level*25.
    static int ReplayOpacityLevelForControl(int control);
    static uint8_t ReplayOpacityAlphaForControl(int control);
    // Replay Data's cyan status label is composited independently of MenuBar
    // hover fade. Only its own 0..10 opacity selector controls its alpha.
    static uint8_t ReplayIndicatorAlphaForState(
        bool playDataActive, uint8_t selectedAlpha,
        bool fullBarSuppressed, bool meterOnly);
    static uint8_t FadeAlpha(uint8_t startAlpha, bool targetVisible,
                             uint32_t elapsedMs);
    static RenderVisibility VisibilityForState(uint8_t barAlpha,
                                               bool fullBarSuppressed,
                                               bool meterOnly,
                                               bool feelingLatch = false);
    static RenderVisibility VisibilityForFrozenChoiceTail(
        uint8_t barAlpha, bool fullBarSuppressed, bool showMeter);
    // Retail SysMenu 0x10025690 does not apply the surrounding MenuBar fade
    // to meter sprites while host +0x154 reports an active feeling update.
    // During a *changed post-choice* feeling effect, meter-only composition
    // likewise preserves full alpha. Pending SetSELECT never shows Feel.
    static uint8_t MeterAlphaForState(uint8_t barAlpha,
                                      bool chromeVisible,
                                      bool feelingLatch,
                                      bool meterOnly,
                                      bool choiceTailFrozen);
    // The retail x2..x32 selection and AUTO ON remain visible at full opacity
    // after ordinary hover chrome fades completely out.
    static PersistentTransport PersistentTransportForState(
        bool autoMode, int speedIndex, bool speedControlsEnabled,
        bool transportLocked, bool fullBarSuppressed, bool meterOnly);
    // SysMenuSDHQ.dll 0x100252BA..0x10025323: 13 authored CHIP frames 29..41.
    // EXE host+0x11C returns speedIndex, +0x138 returns the AUTO-on tick.
    static int AutoLightRecordForTime(uint32_t nowMs, uint32_t autoOnMs,
                                      int speedIndex);
    static int HitTest(float logicalX, float logicalY, Variant variant);
    // Exact FUN_10023FB0 availability projection for the authenticated host
    // predicates used by controls0..14. Controls15..24 are replay-latched and
    // remain outside the ordinary Gameplay presenter.
    static bool ControlAvailable(int control, bool transportLocked,
                                 bool actionInhibit,
                                 bool speedControlsEnabled,
                                 bool superSkipEnabled,
                                 bool replayIndicatorAvailable = false);
    static bool ControlAvailableInNormalGameplay(int control);
    static Action ActionForControl(int control);

    bool Open(SDL_Renderer* renderer, AssetProvider* assets,
              const std::string& assetsRoot,
              Variant variant, int feeling001, int feeling002,
              std::string* error = nullptr);
    void Close();

    void RequestFeelingRefresh() {
        // Once a result tail has finished, a fresh ordinary gameplay writer
        // may again drive standalone Feel. A refresh raised while exiting a
        // choice stays choice-originated, even if its raw values changed.
        if (!choiceTailFrozen_ && !fullBarSuppressed_ && !meterOnly_)
            suppressChoiceOriginFeel_ = false;
        if (!feelingLatch_) feelingDeltaDetected_ = false;
        feelingLatch_ = true;
    }
    // Loading a different save replaces the KTRF feeling state without a
    // normal SET_FLAG writer notification. Keep the MenuBar textures/chrome,
    // but rebase its visual value and clear any old choice animation/cue.
    void RestoreFeelingState(int feeling001, int feeling002);
    void Update(uint32_t nowMs, int feeling001, int feeling002);
    // SysMenuSDHQ.dll 0x10026CA1/0x10026CB8 sends host+0x50 code 3/4
    // only in the meter's Detect phase, when captured feelings have changed.
    // Deliver it once to the game host, never on an ordinary button click.
    int ConsumeFeelingDirectionCode() {
        const int code = pendingFeelingDirectionCode_;
        pendingFeelingDirectionCode_ = 0;
        return code;
    }
    void SetRuntimeState(bool autoMode, bool paused, int speedIndex,
                         bool superSkipEnabled, bool speedControlsEnabled,
                         bool transportLocked, bool actionInhibit,
                         bool fullBarSuppressed);
    // Used only for a detected, changed-feeling post-choice effect, never for
    // the unanswered SETSELECT itself or an unchanged/omitted choice.
    void SetMeterOnly(bool enabled) {
        if (enabled && !meterOnly_) ResetHoverChrome();
        meterOnly_ = enabled;
        if (!enabled) ResumePhysicalHoverAfterChoice();
    }
    // Post-choice: controls remain frozen whether or not any feeling changed;
    // the separately gated meter may appear on actual detected change only.
    void SetChoiceTailFrozen(bool enabled) {
        if (enabled && !choiceTailFrozen_) {
            ResetHoverChrome();
            suppressedPointerKnown_ = false;
            // Omissions may still execute RouteProc SetFeeling. The result's
            // raw delta must not emerge as a generic standalone Feel after
            // the frozen visual phase ends.
            suppressChoiceOriginFeel_ = true;
        }
        choiceTailFrozen_ = enabled;
        if (enabled) selection_ = -1;
        else ResumePhysicalHoverAfterChoice();
    }
    // Confirmation freezes transport regardless of whether feelings change.
    // Only an actual post-choice SeUp/SeDown detector event may reveal the
    // otherwise hidden standalone feeling bar.
    void SetChoiceTailFeelingVisible(bool visible) {
        choiceTailFeelingVisible_ = visible;
    }
    bool ChoiceTailFrozen() const { return choiceTailFrozen_; }
    void SetReplayIndicatorState(bool available, int control = -1) {
        replayIndicatorAvailable_ = available;
        if (control >= 15 && control <= 24)
            replayIndicatorControl_ = control;
    }
    bool Render(SDL_Renderer* renderer, std::string* error = nullptr) const;
    bool HandleEvent(SDL_Renderer* renderer, const SDL_Event* event,
                     Action* action);
    // Authenticated MenuBar internal control 12 (CMAP value 13) opens BackLog.
    // Internal control 11 / CMAP value 12 dispatches a different ReMenu mode.
    // Only
    // mouse hover + left-click are bound; activation requires hit==selection.
    bool HandleBackLogEvent(SDL_Renderer* renderer, const SDL_Event* event,
                            bool* openRequested);

    bool IsOpen() const { return opened_; }
    bool FeelingLatch() const { return feelingLatch_; }
    int Selection() const { return selection_; }
    uint8_t BarAlpha() const { return barAlpha_; }
    bool PointerInsideBar() const { return pointerInsideBar_; }
    // The new upstream Ctrl+A / Ctrl+R controls never override the
    // pending-choice or frozen-result render/input gate.
    bool AutoHideLocked() const { return autoHideLocked_; }
    bool TopMeterHidden() const { return topMeterHidden_; }
    void ToggleAutoHideLock() {
        autoHideLocked_ = !autoHideLocked_;
        if (autoHideLocked_) {
            // An explicit pin is not permission to reveal chrome while the
            // choice owns input; physical hover can resume after handoff.
            if (fullBarSuppressed_ || meterOnly_ || choiceTailFrozen_) return;
            fadeStartAlpha_ = barAlpha_;
            fadeStartMs_ = static_cast<uint32_t>(SDL_GetTicks());
            fadeTargetVisible_ = true;
            barAlpha_ = 255;
        } else if (!pointerInsideBar_) {
            fadeStartAlpha_ = barAlpha_;
            fadeStartMs_ = static_cast<uint32_t>(SDL_GetTicks());
            fadeTargetVisible_ = false;
        }
    }
    void ToggleTopMeter() { topMeterHidden_ = !topMeterHidden_; }
    const SchoolDaysTopMeterModel& Meter() const { return meter_; }

private:
    // The native input gate prevents mouse hover from generating chrome.
    // Discard any pre-choice fade/selection when the game takes ownership.
    // A pointer actually moved into the bar while the result is frozen may
    // reveal chrome *after* release; an old hover alpha may never do so.
    void ResetHoverChrome();
    void ResumePhysicalHoverAfterChoice();
    static SDL_FRect Transform(const SDL_FRect& logical,
                               const Viewport& viewport);
    static bool RenderClipped(SDL_Renderer* renderer, SDL_Texture* texture,
                              SDL_FRect source, SDL_FRect destination,
                              const Viewport& viewport,
                              std::string* error);

    Variant variant_ = Variant::Standard;
    Layout layout_{};
    AssetProvider* assets_ = nullptr;
    SDL_Texture* base_ = nullptr;
    SDL_Texture* chip_ = nullptr;
    SchoolDaysTopMeterModel meter_;
    int pendingFeelingDirectionCode_ = 0;
    bool feelingLatch_ = false;
    // A refresh request is not proof of changed raw 001/002. Only the meter's
    // Detect phase may reveal a standalone feeling animation.
    bool feelingDeltaDetected_ = false;
    bool suppressChoiceOriginFeel_ = false;
    bool opened_ = false;
    int selection_ = -1;
    bool pointerInsideBar_ = false;
    // Last real pointer motion during a suppressed choice result, not a stale
    // pre-choice hover or a synthetic visibility latch.
    bool suppressedPointerKnown_ = false;
    bool suppressedPointerInsideBar_ = false;
    bool autoMode_ = false;
    uint32_t autoOnAtMs_ = 0;
    int autoLightRecord_ = 43;
    bool paused_ = false;
    bool superSkipEnabled_ = false;
    bool speedControlsEnabled_ = false;
    bool transportLocked_ = false;
    bool actionInhibit_ = false;
    bool fullBarSuppressed_ = false;
    bool meterOnly_ = false;
    bool choiceTailFrozen_ = false;
    bool choiceTailFeelingVisible_ = false;
    int speedIndex_ = 0;
    bool replayIndicatorAvailable_ = false;
    int replayIndicatorControl_ = 24;
    uint8_t replayIndicatorAlpha_ = 255;
    uint8_t barAlpha_ = 0;
    uint8_t fadeStartAlpha_ = 0;
    uint32_t fadeStartMs_ = 0;
    bool fadeTargetVisible_ = false;
    bool autoHideLocked_ = false;
    bool topMeterHidden_ = false;
};

} // namespace Kotonoha
