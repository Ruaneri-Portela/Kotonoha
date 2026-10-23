#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>

#include <algorithm>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysTopMeterRenderPlan.hpp>

extern "C" {
#include <Kotonoha/renders/ImageRender.h>
}

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
                                      const std::string& assetsRoot,
                                      Variant variant, int feeling001,
                                      int feeling002, std::string* error) {
    Close();
    if (renderer == nullptr) {
        return Fail(error, "School Days MenuBar renderer is null");
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

    base_ = KotonohaImageCreateTexture(renderer, baseAsset.physical.c_str(), -1, -1);
    chip_ = KotonohaImageCreateTexture(renderer, chipAsset.physical.c_str(), -1, -1);
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
    pointerInsideBar_ = false;
    fadeTargetVisible_ = false;
    fadeStartAlpha_ = 0;
    fadeStartMs_ = 0;
    barAlpha_ = 0;
    autoMode_ = false;
    paused_ = false;
    superSkipEnabled_ = false;
    speedControlsEnabled_ = false;
    transportLocked_ = false;
    actionInhibit_ = false;
    fullBarSuppressed_ = false;
    speedIndex_ = 0;
    opened_ = true;
    return true;
}

void SchoolDaysMenuBarPresenter::Close() {
    if (base_ != nullptr) SDL_DestroyTexture(base_);
    if (chip_ != nullptr) SDL_DestroyTexture(chip_);
    base_ = nullptr;
    chip_ = nullptr;
    feelingLatch_ = false;
    selection_ = -1;
    pointerInsideBar_ = false;
    fadeTargetVisible_ = false;
    fadeStartAlpha_ = 0;
    fadeStartMs_ = 0;
    barAlpha_ = 0;
    opened_ = false;
}

bool SchoolDaysMenuBarPresenter::HandleEvent(SDL_Renderer* renderer,
                                             const SDL_Event* event,
                                             Action* action) {
    if (action != nullptr) *action = {};
    if (!opened_ || renderer == nullptr || event == nullptr || action == nullptr) {
        return false;
    }
    if (event->type != SDL_EVENT_MOUSE_MOTION &&
        event->type != SDL_EVENT_MOUSE_BUTTON_DOWN) {
        return false;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }

    const float wx = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.x
                                                           : event->button.x;
    const float wy = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.y
                                                           : event->button.y;
    float rx = 0.0f, ry = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &rx, &ry)) return false;
    int outputWidth = 0, outputHeight = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
        return false;
    }
    const Viewport viewport = FitViewport(outputWidth, outputHeight, layout_);
    if (!(viewport.scale > 0.0f)) return false;
    const float x = (rx - viewport.x) / viewport.scale;
    const float y = (ry - viewport.y) / viewport.scale;
    const int hit = HitTest(x, y, variant_);
    const bool availableNow =
        !fullBarSuppressed_ &&
        ControlAvailable(hit, transportLocked_, actionInhibit_,
                         speedControlsEnabled_, superSkipEnabled_);
    const int available = availableNow ? hit : -1;

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const bool inside = x >= 0.0f && x < layout_.barWidth &&
                            y >= 0.0f && y < layout_.barHeight;
        if (inside != pointerInsideBar_) {
            pointerInsideBar_ = inside;
            fadeStartAlpha_ = barAlpha_;
            fadeStartMs_ = static_cast<uint32_t>(SDL_GetTicks());
            fadeTargetVisible_ = inside;
        }
        selection_ = available;
        return inside;
    }

    // Native coordinate activation recomputes the hit and requires it to equal
    // the stored hover selection. A click never silently changes selection.
    if (available >= 0 && available == selection_) {
        *action = ActionForControl(available);
    }
    return hit >= 0;
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

void SchoolDaysMenuBarPresenter::Update(uint32_t nowMs, int feeling001,
                                        int feeling002) {
    if (!opened_) return;

    const uint32_t elapsed = nowMs - fadeStartMs_;
    barAlpha_ = FadeAlpha(fadeStartAlpha_, fadeTargetVisible_, elapsed);

    if (feelingLatch_) {
        const auto tick = meter_.Tick(nowMs, feeling001, feeling002);
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
    if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    const Viewport viewport = FitViewport(outputWidth, outputHeight, layout_);
    if (!(viewport.scale > 0.0f)) {
        return Fail(error, "School Days MenuBar viewport is invalid");
    }

    // Native 0x100255C0 stops submitting the entire bar once its common alpha
    // reaches zero. Every MenuBar layer shares that same fade.
    if (barAlpha_ == 0 || fullBarSuppressed_) return true;

    SDL_SetTextureAlphaMod(base_, barAlpha_);
    SDL_SetTextureAlphaMod(chip_, barAlpha_);

    // Retail bar is authored at 800x75; WideFull/WideNote scale only the
    // destination. The physical monitor remains completely independent.
    const SDL_FRect baseSource{0.0f, 0.0f, 800.0f, 75.0f};
    const SDL_FRect baseLogical{0.0f, 0.0f,
                                layout_.barWidth, layout_.barHeight};
    const SDL_FRect basePhysical = Transform(baseLogical, viewport);
    if (!SDL_RenderTexture(renderer, base_, &baseSource, &basePhysical)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }

    // Static feeling-meter artwork (record67) carries the authored
    // KOTONOHA/SEKAI labels/frame that are not part of MenuBar.PNG.
    {
        const ChipRecord meterFrame = FixedChipRecord(67, variant_);
        if (!RenderClipped(renderer, chip_, meterFrame.source,
                           meterFrame.destination, viewport, error)) {
            return false;
        }
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
        const ChipRecord skipNormal =
            FixedChipRecord(!transportLocked_ && superSkipEnabled_ ? 46 : 65,
                            variant_);
        if (!RenderClipped(renderer, chip_, skipNormal.source,
                           skipNormal.destination, viewport, error)) return false;
        const ChipRecord pauseNormal = FixedChipRecord(paused_ ? 45 : 44, variant_);
        if (!RenderClipped(renderer, chip_, pauseNormal.source,
                           pauseNormal.destination, viewport, error)) return false;
        const ChipRecord autoNormal = FixedChipRecord(autoMode_ ? 43 : 42, variant_);
        if (!RenderClipped(renderer, chip_, autoNormal.source,
                           autoNormal.destination, viewport, error)) return false;
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
    }

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
