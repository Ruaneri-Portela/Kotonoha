#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>

#include <algorithm>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/routing/SchoolDaysTopMeterRenderPlan.hpp>

namespace Kotonoha {
namespace {

bool Fail(std::string* error, const char* text) {
    if (error != nullptr) {
        *error = text != nullptr ? text : "School Days MenuBar error";
    }
    return false;
}

} // namespace

SchoolDaysMenuBarPresenter::~SchoolDaysMenuBarPresenter() { Close(); }

bool SchoolDaysMenuBarPresenter::Open(SDL_Renderer* renderer,
                                      AssetProvider* assets,
                                      const std::string& assetsRoot,
                                      Variant variant, int feeling001,
                                      int feeling002, std::string* error) {
    Close();
    if (renderer == nullptr || assets == nullptr) {
        return Fail(error, "School Days MenuBar renderer/assets are null");
    }

    const auto baseAsset = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), "System/MenuBar/MenuBar");
    const auto chipAsset = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), "System/MenuBar/MenuBar_Chip");
    if (!baseAsset.present || baseAsset.collision || !chipAsset.present ||
        chipAsset.collision) {
        return Fail(error,
                    "School Days MenuBar PNG/CHIP asset could not be resolved");
    }

    assets_ = assets;
    try {
        const SurfacePtr baseSurface = assets_->loadSurface(baseAsset.physical);
        const SurfacePtr chipSurface = assets_->loadSurface(chipAsset.physical);
        base_ = SDL_CreateTextureFromSurface(renderer, baseSurface.get());
        chip_ = SDL_CreateTextureFromSurface(renderer, chipSurface.get());
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        Close();
        return false;
    }
    if (base_ == nullptr || chip_ == nullptr) {
        Close();
        return Fail(error, "School Days MenuBar texture load failed");
    }
    // MenuBar.PNG is an RGBA overlay over the narrative frame, not an opaque
    // replacement surface. Preserve its authored alpha at every output size.
    SDL_SetTextureBlendMode(base_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(chip_, SDL_BLENDMODE_BLEND);

    variant_ = variant;
    layout_ = LayoutForVariant(variant);
    (void)meter_.Initialize(feeling001, feeling002);
    // Initialize() mirrors native callback_30(0): no update cycle is active
    // until a feeling writer raises the host latch again.
    feelingLatch_ = false;
    pendingFeelingDirectionCode_ = 0;
    pointerInsideBar_ = false;
    fadeTargetVisible_ = false;
    fadeStartAlpha_ = 0;
    fadeStartMs_ = 0;
    barAlpha_ = 0;
    autoMode_ = false;
    autoOnAtMs_ = 0;
    autoLightRecord_ = 43;
    paused_ = false;
    superSkipEnabled_ = false;
    speedControlsEnabled_ = false;
    transportLocked_ = false;
    actionInhibit_ = false;
    fullBarSuppressed_ = false;
    meterOnly_ = false;
    speedIndex_ = 0;
    replayIndicatorAvailable_ = false;
    replayIndicatorControl_ = 24;
    replayIndicatorAlpha_ = 255;
    opened_ = true;
    return true;
}

void SchoolDaysMenuBarPresenter::Close() {
    if (base_ != nullptr) SDL_DestroyTexture(base_);
    if (chip_ != nullptr) SDL_DestroyTexture(chip_);
    base_ = nullptr;
    chip_ = nullptr;
    assets_ = nullptr;
    feelingLatch_ = false;
    pendingFeelingDirectionCode_ = 0;
    selection_ = -1;
    pointerInsideBar_ = false;
    fadeTargetVisible_ = false;
    fadeStartAlpha_ = 0;
    fadeStartMs_ = 0;
    barAlpha_ = 0;
    autoMode_ = false;
    autoOnAtMs_ = 0;
    autoLightRecord_ = 43;
    opened_ = false;
    meterOnly_ = false;
    replayIndicatorAvailable_ = false;
    replayIndicatorControl_ = 24;
    replayIndicatorAlpha_ = 255;
}

bool SchoolDaysMenuBarPresenter::HandleEvent(SDL_Renderer* renderer,
                                             const SDL_Event* event,
                                             Action* action) {
    if (action != nullptr) *action = {};
    if (!opened_ || meterOnly_ || renderer == nullptr ||
        event == nullptr || action == nullptr) {
        return false;
    }
    if (event->type != SDL_EVENT_MOUSE_MOTION &&
        event->type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->type != SDL_EVENT_MOUSE_BUTTON_UP &&
        event->type != SDL_EVENT_MOUSE_WHEEL) {
        return false;
    }
    if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }

    const float wx = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.x
                   : event->type == SDL_EVENT_MOUSE_WHEEL ? event->wheel.mouse_x
                                                          : event->button.x;
    const float wy = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.y
                   : event->type == SDL_EVENT_MOUSE_WHEEL ? event->wheel.mouse_y
                                                          : event->button.y;
    int outputWidth = 0, outputHeight = 0;
    SDL_RendererLogicalPresentation presentation{};
    if (!SDL_GetRenderLogicalPresentation(renderer, &outputWidth, &outputHeight,
                                          &presentation)) {
        return false;
    }
    if (presentation == SDL_LOGICAL_PRESENTATION_DISABLED &&
        !SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight))
        return false;
    const Viewport viewport = FitViewport(outputWidth, outputHeight, layout_);
    if (!(viewport.scale > 0.0f)) return false;
    // Engine::Event has already converted this event into Screen coordinates.
    const float x = (wx - viewport.x) / viewport.scale;
    const float y = (wy - viewport.y) / viewport.scale;
    const int hit = HitTest(x, y, variant_);
    const bool availableNow =
        !fullBarSuppressed_ &&
        ControlAvailable(hit, transportLocked_, actionInhibit_,
                         speedControlsEnabled_, superSkipEnabled_,
                         replayIndicatorAvailable_);
    const int available = availableNow ? hit : -1;

    const bool inside = x >= 0.0f && x < layout_.barWidth &&
                        y >= 0.0f && y < layout_.barHeight;
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (inside != pointerInsideBar_) {
            pointerInsideBar_ = inside;
            fadeStartAlpha_ = barAlpha_;
            fadeStartMs_ = static_cast<uint32_t>(SDL_GetTicks());
            fadeTargetVisible_ = inside;
        }
        selection_ = available;
        return inside;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event->type == SDL_EVENT_MOUSE_WHEEL) return inside;

    // Native coordinate activation recomputes the hit and requires it to equal
    // the stored hover selection. A click never silently changes selection.
    if (available >= 0 && available == selection_) {
        *action = ActionForControl(available);
        if (action->kind == ActionKind::SetReplayIndicatorOpacity) {
            replayIndicatorControl_ = available;
            replayIndicatorAlpha_ = ReplayOpacityAlphaForControl(available);
        }
    }
    return inside;
}

bool SchoolDaysMenuBarPresenter::HandleBackLogEvent(
    SDL_Renderer* renderer, const SDL_Event* event, bool* openRequested) {
    if (openRequested != nullptr) *openRequested = false;
    if (openRequested == nullptr) return false;
    Action action;
    const bool consumed = HandleEvent(renderer, event, &action);
    *openRequested = action.kind == ActionKind::OpenBackLog;
    return consumed;
}

void SchoolDaysMenuBarPresenter::RestoreFeelingState(int feeling001,
                                                    int feeling002) {
    if (!opened_) return;
    (void)meter_.Initialize(feeling001, feeling002);
    feelingLatch_ = false;
    pendingFeelingDirectionCode_ = 0;
}

void SchoolDaysMenuBarPresenter::Update(uint32_t nowMs, int feeling001,
                                        int feeling002) {
    if (!opened_) return;

    // EXE host+0x108 returns the pause latch (+0x200). SysMenu branches past
    // the animation update while paused, retaining its last submitted frame.
    // The epoch is still wall-clock time, so resuming jumps to the current
    // authored phase, just as GetTickCount() does in the retail DLL.
    if (autoMode_ && !paused_) {
        autoLightRecord_ = AutoLightRecordForTime(
            nowMs, autoOnAtMs_, speedIndex_);
    }

    const uint32_t elapsed = nowMs - fadeStartMs_;
    barAlpha_ = FadeAlpha(fadeStartAlpha_, fadeTargetVisible_, elapsed);

    if (feelingLatch_) {
        const auto tick = meter_.Tick(nowMs, feeling001, feeling002);
        if (tick.hostDirectionCode == 3 || tick.hostDirectionCode == 4)
            pendingFeelingDirectionCode_ = tick.hostDirectionCode;
        if (tick.clearFeelingLatch) feelingLatch_ = false;
    }
}

void SchoolDaysMenuBarPresenter::SetRuntimeState(bool autoMode, bool paused,
                                                 int speedIndex,
                                                 bool superSkipEnabled,
                                                 bool speedControlsEnabled,
                                                 bool transportLocked,
                                                 bool actionInhibit,
                                                 bool fullBarSuppressed) {
    if (autoMode && !autoMode_) {
        // EXE 0x00425295..0x0042529E writes GetTickCount to host+0x224 when
        // the AUTO host latch transitions OFF -> ON, never on pointer hover.
        autoOnAtMs_ = static_cast<uint32_t>(SDL_GetTicks());
        autoLightRecord_ = 43; // retail persistent ON art before first update
    } else if (!autoMode) {
        autoLightRecord_ = 43;
    }
    autoMode_ = autoMode;
    paused_ = paused;
    speedIndex_ = std::clamp(speedIndex, 0, 4);
    superSkipEnabled_ = superSkipEnabled;
    speedControlsEnabled_ = speedControlsEnabled;
    transportLocked_ = transportLocked;
    actionInhibit_ = actionInhibit;
    fullBarSuppressed_ = fullBarSuppressed;
}

SDL_FRect SchoolDaysMenuBarPresenter::Transform(const SDL_FRect& logical,
                                                const Viewport& viewport) {
    return {viewport.x + logical.x * viewport.scale,
            viewport.y + logical.y * viewport.scale,
            logical.w * viewport.scale,
            logical.h * viewport.scale};
}

bool SchoolDaysMenuBarPresenter::RenderClipped(
    SDL_Renderer* renderer, SDL_Texture* texture, SDL_FRect source,
    SDL_FRect destination, const Viewport& viewport, std::string* error) {
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    if (renderer == nullptr || texture == nullptr || source.w <= 0.0f ||
        source.h <= 0.0f || destination.w <= 0.0f || destination.h <= 0.0f ||
        !SDL_GetTextureSize(texture, &textureWidth, &textureHeight)) {
        return Fail(error, "School Days MenuBar render source is invalid");
    }

    const float xScale = destination.w / source.w;
    const float yScale = destination.h / source.h;

    const float clipLeft = std::max(0.0f, -source.x);
    const float clipTop = std::max(0.0f, -source.y);
    source.x += clipLeft;
    source.y += clipTop;
    source.w -= clipLeft;
    source.h -= clipTop;
    destination.x += clipLeft * xScale;
    destination.y += clipTop * yScale;
    destination.w -= clipLeft * xScale;
    destination.h -= clipTop * yScale;

    const float clipRight =
        std::max(0.0f, source.x + source.w - textureWidth);
    const float clipBottom =
        std::max(0.0f, source.y + source.h - textureHeight);
    source.w -= clipRight;
    source.h -= clipBottom;
    destination.w -= clipRight * xScale;
    destination.h -= clipBottom * yScale;

    if (source.w <= 0.0f || source.h <= 0.0f || destination.w <= 0.0f ||
        destination.h <= 0.0f) {
        return true;
    }

    const SDL_FRect physical = Transform(destination, viewport);
    if (!SDL_RenderTexture(renderer, texture, &source, &physical)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    return true;
}

bool SchoolDaysMenuBarPresenter::Render(SDL_Renderer* renderer,
                                        std::string* error) const {
    if (!opened_ || renderer == nullptr || base_ == nullptr || chip_ == nullptr) {
        return Fail(error, "School Days MenuBar presenter is not open");
    }

    int outputWidth = 0;
    int outputHeight = 0;
    SDL_RendererLogicalPresentation presentation{};
    if (!SDL_GetRenderLogicalPresentation(renderer, &outputWidth, &outputHeight,
                                          &presentation)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    if (presentation == SDL_LOGICAL_PRESENTATION_DISABLED &&
        !SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    const Viewport viewport = FitViewport(outputWidth, outputHeight, layout_);
    if (!(viewport.scale > 0.0f)) {
        return Fail(error, "School Days MenuBar viewport is invalid");
    }

    // Native persistent AUTO ON, selected x2..x32 and the Replay Data status
    // label survive the ordinary hover-bar fade. A replay-opacity selector
    // controls that label independently; only chrome obeys barAlpha_.
    const RenderVisibility visibility =
        VisibilityForState(barAlpha_, fullBarSuppressed_, meterOnly_);
    const PersistentTransport persistent = PersistentTransportForState(
        autoMode_, speedIndex_, speedControlsEnabled_, transportLocked_,
        fullBarSuppressed_, meterOnly_);
    const uint8_t replayAlpha = ReplayIndicatorAlphaForState(
        replayIndicatorAvailable_, replayIndicatorAlpha_, fullBarSuppressed_,
        meterOnly_);
    if (!visibility.meter && !persistent.selectedSpeed && !persistent.autoOn &&
        replayAlpha == 0)
        return true;

    SDL_SetTextureAlphaMod(base_, barAlpha_);
    SDL_SetTextureAlphaMod(chip_, visibility.chrome ? barAlpha_ : 255);

    // The persistent artwork has an independent full-opacity composition;
    // restoring chrome alpha matters for the subsequent hover/help sprites.
    const auto drawPersistentChip = [&](const ChipRecord& record) {
        SDL_SetTextureAlphaMod(chip_, 255);
        const bool ok = RenderClipped(renderer, chip_, record.source,
                                      record.destination, viewport, error);
        SDL_SetTextureAlphaMod(chip_, visibility.chrome ? barAlpha_ : 255);
        return ok;
    };
    const auto drawPersistentSpeed = [&]() {
        return !persistent.selectedSpeed ||
            drawPersistentChip(SelectedSpeedChip(
                speedIndex_, speedControlsEnabled_, transportLocked_, variant_));
    };
    const auto drawPersistentAuto = [&]() {
        if (!persistent.autoOn) return true;
        // Retail records29..41 are complete blue AUTO sprites, not merely a
        // translucent light mask. Replace rec43 with the chosen full frame
        // instead of double-compositing their partly transparent boundaries.
        const int record =
            autoLightRecord_ >= 29 && autoLightRecord_ <= 41
                ? autoLightRecord_ : 43;
        return drawPersistentChip(FixedChipRecord(record, variant_));
    };
    const auto drawReplayLabel = [&]() {
        if (replayAlpha == 0) return true;
        // The retail transparency selector is meaningful even with no hover:
        // record68 is a separate authored cyan "REPLAY MODE" label. Never
        // multiply its selected alpha by the disappearing MenuBar chrome.
        SDL_SetTextureAlphaMod(chip_, replayAlpha);
        const ChipRecord label = FixedChipRecord(68, variant_);
        const bool ok = RenderClipped(renderer, chip_, label.source,
                                      label.destination, viewport, error);
        SDL_SetTextureAlphaMod(chip_, visibility.chrome ? barAlpha_ : 255);
        return ok;
    };

    // Retail bar is authored at 800x75; WideFull/WideNote scale only the
    // destination. The physical monitor remains completely independent.
    if (visibility.chrome) {
        const SDL_FRect baseSource{0.0f, 0.0f, 800.0f, 75.0f};
        const SDL_FRect baseLogical{0.0f, 0.0f,
                                    layout_.barWidth, layout_.barHeight};
        const SDL_FRect basePhysical = Transform(baseLogical, viewport);
        if (!SDL_RenderTexture(renderer, base_, &baseSource, &basePhysical)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
    }

    // Static feeling-meter artwork (record67) carries the authored
    // KOTONOHA/SEKAI labels/frame that are not part of MenuBar.PNG.
    if (visibility.meter) {
        const ChipRecord meterFrame = FixedChipRecord(67, variant_);
        if (!RenderClipped(renderer, chip_, meterFrame.source,
                           meterFrame.destination, viewport, error)) {
            return false;
        }
    }

    if (visibility.chrome) {
    // Repeat-mode strip. Retail 0x10021C20 always submits this family: record69
    // is the ordinary gray strip, record70 is the cyan replay-active strip,
    // and record71 is the movable 12x17 selector. The active-state predicate
    // is FILMEngine secondary-vtable +0x98 (main+0x1E0), not elapsed story time.
    {
        const ChipRecord repeatStrip =
            FixedChipRecord(replayIndicatorAvailable_ ? 70 : 69, variant_);
        if (!RenderClipped(renderer, chip_, repeatStrip.source,
                           repeatStrip.destination, viewport, error)) {
            return false;
        }

        ChipRecord selector = FixedChipRecord(71, variant_);
        const SDL_FRect selectorControl =
            ControlRect(replayIndicatorControl_, variant_);
        selector.destination = selectorControl;
        if (!RenderClipped(renderer, chip_, selector.source,
                           selector.destination, viewport, error)) {
            return false;
        }

        // Render the Replay Data label over its live selector while hovered.
        if (!drawReplayLabel()) return false;
    }

    // Persistent transport state. Native ordinary draw order after MenuBar.PNG
    // is speed strip (rec47 enabled / rec66 disabled), next-choice/SKIP
    // (rec46 enabled / rec65 disabled), pause (44/45), then auto (42/43).
    // Controls2/3 are authored directly into MenuBar.PNG.
    {
        const ChipRecord speedsNormal =
            FixedChipRecord(!transportLocked_ && speedControlsEnabled_ ? 47 : 66,
                            variant_);
        if (!RenderClipped(renderer, chip_, speedsNormal.source,
                           speedsNormal.destination, viewport, error)) return false;
        if (!drawPersistentSpeed()) return false;
        const ChipRecord skipNormal =
            FixedChipRecord(!transportLocked_ && superSkipEnabled_ ? 46 : 65,
                            variant_);
        if (!RenderClipped(renderer, chip_, skipNormal.source,
                           skipNormal.destination, viewport, error)) return false;
        const ChipRecord pauseNormal = FixedChipRecord(paused_ ? 45 : 44, variant_);
        if (!RenderClipped(renderer, chip_, pauseNormal.source,
                           pauseNormal.destination, viewport, error)) return false;
        if (persistent.autoOn) {
            if (!drawPersistentAuto()) return false;
        } else {
            const ChipRecord autoNormal = FixedChipRecord(42, variant_);
            if (!RenderClipped(renderer, chip_, autoNormal.source,
                               autoNormal.destination, viewport, error)) return false;
        }
    }

    // The five system labels are separate CHIP sprites in retail.
    for (int control = 10; control <= 14; ++control) {
        const ChipRecord normal = NormalSystemControlChip(control, variant_);
        if (!RenderClipped(renderer, chip_, normal.source, normal.destination,
                           viewport, error)) {
            return false;
        }
    }

    if (selection_ >= 0) {
        // Native substitutes state-specific hover records for Auto/Pause only.
        const ChipRecord hover =
            selection_ == 0 && autoMode_
                ? FixedChipRecord(25, variant_)
                : (selection_ == 1 && paused_
                       ? FixedChipRecord(26, variant_)
                       : ControlChip(selection_, variant_));
        if (hover.source.w > 0.0f && hover.source.h > 0.0f &&
            !RenderClipped(renderer, chip_, hover.source, hover.destination,
                           viewport, error)) {
            return false;
        }

        // FUN_100262E0 renders one of fixed records53..64 below the controls
        // whenever a valid control is selected.  These are the retail-authored
        // help strings (e.g. pause/play, save, load, and the shared repeat
        // transparency description for controls15..24).
        const ChipRecord help = HoverHelpChip(selection_, variant_);
        if (help.source.w > 0.0f && help.source.h > 0.0f &&
            !RenderClipped(renderer, chip_, help.source, help.destination,
                           viewport, error)) {
            return false;
        }
    }

    } else {
        // Chrome faded to zero: keep the active status sprites, not the idle
        // controls/selector strip. Replay Data label still obeys transparency.
        if (!drawPersistentSpeed() || !drawPersistentAuto() ||
            !drawReplayLabel()) return false;
    } // MenuBar controls, help, and repeat strip are absent in meter-only mode.

    if (!visibility.meter) return true;

    const auto plan = SchoolDaysTopMeterRenderPlan::Build(
        meter_.CurrentVisual(), layout_.layoutScale);
    if (!plan.valid) {
        return Fail(error, "School Days top-meter render plan is invalid");
    }
    const SDL_FRect source{plan.source.x, plan.source.y,
                           plan.source.w, plan.source.h};
    const SDL_FRect destination{plan.destination.x, plan.destination.y,
                                plan.destination.w, plan.destination.h};
    return RenderClipped(renderer, chip_, source, destination, viewport, error);
}

} // namespace Kotonoha
