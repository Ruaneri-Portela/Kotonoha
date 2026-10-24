#include <Kotonoha/routing/SchoolDaysSystemUi.hpp>
#include <Kotonoha/routing/SchoolDaysSaveFeelRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysTitleMovieClock.hpp>
#include <Kotonoha/routing/SchoolDaysTitleRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysExitViewRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysReplayRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapRenderPlan.hpp>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

extern "C" {
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Time.h>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>

namespace Kotonoha {

SchoolDaysSystemUi::~SchoolDaysSystemUi() {
    Close();
}

SchoolDaysSystemUi::Layout SchoolDaysSystemUi::LayoutForVariant(
    SchoolDaysSaveFeelModel::Variant variant) {
    const auto planLayout = SchoolDaysSaveFeelRenderPlan::LayoutFor(variant);
    return {variant, planLayout.scale, planLayout.yOffset};
}

void SchoolDaysSystemUi::DestroyTextures() {
    if (titleVideo_ != nullptr) {
        Kotonoha_VideoRenderShutdown(&titleVideo_);
    }
    if (titleVideoClock_ != nullptr) {
        Kotonoha_timeDestroy(titleVideoClock_);
        titleVideoClock_ = nullptr;
    }
    titleVideoStartMs_ = 0u;
    titleBackgroundHeight_ = 450.0f;
    if (baseTexture_ != nullptr) {
        SDL_DestroyTexture(baseTexture_);
        baseTexture_ = nullptr;
    }
    if (chipTexture_ != nullptr) {
        SDL_DestroyTexture(chipTexture_);
        chipTexture_ = nullptr;
    }
    if (titleBackgroundTexture_ != nullptr) {
        SDL_DestroyTexture(titleBackgroundTexture_);
        titleBackgroundTexture_ = nullptr;
    }
    if (saveLoadTextTexture_ != nullptr) {
        SDL_DestroyTexture(saveLoadTextTexture_);
        saveLoadTextTexture_ = nullptr;
    }
    if (routeMapTextTexture_ != nullptr) {
        SDL_DestroyTexture(routeMapTextTexture_);
        routeMapTextTexture_ = nullptr;
    }
    if (replayThumbnailTexture_ != nullptr) {
        SDL_DestroyTexture(replayThumbnailTexture_);
        replayThumbnailTexture_ = nullptr;
    }
    if (replayPopupBaseTexture_ != nullptr) {
        SDL_DestroyTexture(replayPopupBaseTexture_);
        replayPopupBaseTexture_ = nullptr;
    }
    if (replayPopupChipTexture_ != nullptr) {
        SDL_DestroyTexture(replayPopupChipTexture_);
        replayPopupChipTexture_ = nullptr;
    }
    saveLoadFont_.Clear();
    routeMapFont_.Clear();
    saveLoadTextPixels_.clear();
    routeMapTextPixels_.clear();
    saveLoadMetadata_.clear();
    saveLoadCommentAdvances_.clear();
    saveLoadUseEnglish_ = false;
    routeMapUseEnglish_ = false;
    replayAssetsRoot_.clear();
    replayLoadedPage_ = -1;
    replayLoadedSceneCheckCount_ = -1;
    routeMapAssetsRoot_.clear();
    routeMapLoadedMajor_ = -1;
    routeMapLoadedSubpage_ = -1;
}

void SchoolDaysSystemUi::Close() {
    DestroyTextures();
    saveFeel_.Close();
    saveLoad_.Close();
    title_.Close();
    configMenu_.Close();
    somConSet_.Close();
    exitView_.Close();
    replay_.Close();
    routeMap_.Close();
    configVisualState_ = {};
    layout_ = {};
    resultEmitted_ = false;
}

bool SchoolDaysSystemUi::OpenRouteMap(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysRouteMapModel::Variant variant, int initialMajor,
    int initialSubpage, bool alternateHost, int completionPercent,
    bool useEnglish, const std::string& fontDataLogicalPath, std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days RouteMap renderer is null";
        return false;
    }
    if (!routeMap_.Open(assetsRoot, variant, initialMajor, initialSubpage,
                        alternateHost, error)) {
        return false;
    }
    routeMap_.SetCompletionPercent(completionPercent);
    routeMapAssetsRoot_ = assetsRoot;
    routeMapLoadedMajor_ = -1;
    routeMapLoadedSubpage_ = -1;
    if (!EnsureRouteMapTextures(renderer, error)) {
        Close();
        return false;
    }

    namespace fs = std::filesystem;
    fs::path fontLogical(fontDataLogicalPath);
    if (fontLogical.empty() || fontLogical.is_absolute()) {
        if (error != nullptr) *error = "School Days RouteMap FONTDATA logical path is invalid";
        Close();
        return false;
    }
    if (useEnglish) {
        const auto ext = fontLogical.extension().string();
        const auto stem = fontLogical.stem().string();
        fontLogical.replace_filename(stem + "_eng" + ext);
    }
    std::string fontPhysical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), fontLogical.generic_string(), &fontPhysical,
            error, "RouteMap FONTDATA") ||
        !routeMapFont_.LoadFile(fontPhysical, error)) {
        Close();
        return false;
    }
    routeMapTextTexture_ = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 128);
    if (routeMapTextTexture_ == nullptr ||
        !SDL_SetTextureBlendMode(routeMapTextTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr)
            *error = std::string("School Days RouteMap FONTDATA texture setup failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    routeMapUseEnglish_ = useEnglish;
    routeMapTextPixels_.assign(static_cast<size_t>(512) * 128u, 0u);
    if (!UpdateRouteMapCompletionTexture(error)) {
        Close();
        return false;
    }
    resultEmitted_ = false;
    return true;
}

bool SchoolDaysSystemUi::UpdateRouteMapCompletionTexture(std::string* error) {
    if (routeMapTextTexture_ == nullptr || !routeMapFont_.IsLoaded() ||
        routeMapTextPixels_.size() != static_cast<size_t>(512) * 128u) {
        if (error != nullptr) *error = "School Days RouteMap FONTDATA presenter is not ready";
        return false;
    }
    std::fill(routeMapTextPixels_.begin(), routeMapTextPixels_.end(), 0u);
    const auto text = SchoolDaysRouteMapRenderPlan::CompletionTextFor(
        routeMap_, routeMapUseEnglish_);
    int x = 0;
    SchoolDaysFontData::Glyph glyph;
    for (char16_t unit : text.text) {
        const uint16_t code = static_cast<uint16_t>(unit);
        if (!routeMapFont_.DecodeGlyph(code, &glyph, error)) return false;
        if (x < 0 || x + SchoolDaysFontData::kGlyphWidth > 512) {
            if (error != nullptr) *error = "School Days RouteMap FONTDATA text exceeds backing";
            return false;
        }
        for (int gy = 0; gy < SchoolDaysFontData::kGlyphHeight; ++gy) {
            for (int gx = 0; gx < SchoolDaysFontData::kGlyphWidth; ++gx) {
                const size_t source = static_cast<size_t>(gy) *
                                          SchoolDaysFontData::kGlyphWidth +
                                      static_cast<size_t>(gx);
                const uint8_t alpha = glyph.alpha[source];
                const uint8_t gray = glyph.gray[source];
                const uint32_t packed = (static_cast<uint32_t>(alpha) << 24u) |
                                        (static_cast<uint32_t>(gray) << 16u) |
                                        (static_cast<uint32_t>(gray) << 8u) |
                                        static_cast<uint32_t>(gray);
                uint32_t& destination = routeMapTextPixels_[
                    static_cast<size_t>(gy) * 512u + static_cast<size_t>(x + gx)];
                if (destination < packed) destination = packed;
            }
        }
        x += SchoolDaysRouteMapRenderPlan::CompletionGlyphAdvance(code);
    }
    if (!SDL_UpdateTexture(routeMapTextTexture_, nullptr,
                           routeMapTextPixels_.data(),
                           512 * static_cast<int>(sizeof(uint32_t)))) {
        if (error != nullptr)
            *error = std::string("School Days RouteMap FONTDATA upload failed: ") +
                     SDL_GetError();
        return false;
    }
    const uint8_t a = static_cast<uint8_t>(text.color >> 24u);
    const uint8_t r = static_cast<uint8_t>(text.color >> 16u);
    const uint8_t g = static_cast<uint8_t>(text.color >> 8u);
    const uint8_t b = static_cast<uint8_t>(text.color);
    return SDL_SetTextureColorMod(routeMapTextTexture_, r, g, b) &&
           SDL_SetTextureAlphaMod(routeMapTextTexture_, a);
}

bool SchoolDaysSystemUi::EnsureRouteMapTextures(SDL_Renderer* renderer,
                                                std::string* error) {
    if (!routeMap_.IsOpen() || renderer == nullptr) return false;
    const int major = routeMap_.Major();
    const int subpage = routeMap_.Subpage();
    if (routeMapLoadedMajor_ == major && routeMapLoadedSubpage_ == subpage &&
        baseTexture_ != nullptr && chipTexture_ != nullptr) {
        return true;
    }

    if (baseTexture_ != nullptr) {
        SDL_DestroyTexture(baseTexture_);
        baseTexture_ = nullptr;
    }
    if (chipTexture_ != nullptr) {
        SDL_DestroyTexture(chipTexture_);
        chipTexture_ = nullptr;
    }

    const auto base = ResolveSchoolDaysAssetPath(
        routeMapAssetsRoot_.c_str(),
        SchoolDaysRouteMapModel::BaseRelativePath(major, subpage));
    const auto chip = ResolveSchoolDaysAssetPath(
        routeMapAssetsRoot_.c_str(),
        SchoolDaysRouteMapModel::ChipRelativePath(major, subpage));
    if (!base.present || base.collision || !chip.present || chip.collision) {
        if (error != nullptr)
            *error = "School Days RouteMap base/CHIP resources could not be resolved";
        return false;
    }
    baseTexture_ = KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
    chipTexture_ = KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr)
            *error = std::string("School Days RouteMap texture load failed: ") + SDL_GetError();
        return false;
    }
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND);
    routeMapLoadedMajor_ = major;
    routeMapLoadedSubpage_ = subpage;
    return true;
}

bool SchoolDaysSystemUi::OpenReplay(SDL_Renderer* renderer,
                                    const std::string& assetsRoot,
                                    SchoolDaysReplayModel::Variant variant,
                                    std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days Replay renderer is null";
        return false;
    }
    if (!replay_.Open(assetsRoot, variant, SchoolDaysReplayModel::View::HScene,
                      error)) {
        return false;
    }
    replayAssetsRoot_ = assetsRoot;
    replayLoadedPage_ = -1;
    replayLoadedSceneCheckCount_ = -1;
    if (!EnsureReplayTextures(renderer, error)) {
        Close();
        return false;
    }
    resultEmitted_ = false;
    return true;
}

bool SchoolDaysSystemUi::EnsureReplayTextures(SDL_Renderer* renderer,
                                              std::string* error) {
    if (!replay_.IsOpen() || renderer == nullptr) return false;
    const bool sceneCheck = replay_.SceneCheckOpen();
    const int sceneCheckCount = sceneCheck ? replay_.SceneCheckChoiceCount() : 0;
    const auto view = replay_.CurrentView();
    const int page = replay_.Page();
    const bool needsThumbnail = view == SchoolDaysReplayModel::View::HScene;
    const bool parentMatches = replayLoadedView_ == view &&
                               replayLoadedPage_ == page &&
                               baseTexture_ != nullptr && chipTexture_ != nullptr &&
                               (!needsThumbnail || replayThumbnailTexture_ != nullptr);

    if (!parentMatches) {
        if (baseTexture_ != nullptr) { SDL_DestroyTexture(baseTexture_); baseTexture_ = nullptr; }
        if (chipTexture_ != nullptr) { SDL_DestroyTexture(chipTexture_); chipTexture_ = nullptr; }
        if (replayThumbnailTexture_ != nullptr) {
            SDL_DestroyTexture(replayThumbnailTexture_);
            replayThumbnailTexture_ = nullptr;
        }

        const std::string baseLogical = SchoolDaysReplayModel::BaseRelativePath(view);
        const std::string chipLogical = SchoolDaysReplayModel::ChipRelativePath(view);
        const auto base = ResolveSchoolDaysAssetPath(replayAssetsRoot_.c_str(),
                                                    baseLogical.c_str());
        const auto chip = ResolveSchoolDaysAssetPath(replayAssetsRoot_.c_str(),
                                                    chipLogical.c_str());
        if (!base.present || base.collision || !chip.present || chip.collision) {
            if (error != nullptr)
                *error = "School Days Replay base/CHIP resources could not be resolved";
            return false;
        }
        baseTexture_ = KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
        chipTexture_ = KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
        if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
            if (error != nullptr)
                *error = std::string("School Days Replay texture load failed: ") + SDL_GetError();
            return false;
        }
        SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_NONE);
        SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND);

        if (needsThumbnail) {
            const std::string thumbLogical =
                SchoolDaysReplayRenderPlan::ThumbnailRelativePath(page);
            const auto thumb = ResolveSchoolDaysAssetPath(replayAssetsRoot_.c_str(),
                                                          thumbLogical.c_str());
            if (!thumb.present || thumb.collision) {
                if (error != nullptr)
                    *error = "School Days Replay thumbnail resource could not be resolved";
                return false;
            }
            replayThumbnailTexture_ =
                KotonohaImageCreateTexture(renderer, thumb.physical.c_str(), -1, -1);
            if (replayThumbnailTexture_ == nullptr) {
                if (error != nullptr)
                    *error = std::string("School Days Replay thumbnail load failed: ") + SDL_GetError();
                return false;
            }
            SDL_SetTextureBlendMode(replayThumbnailTexture_, SDL_BLENDMODE_BLEND);
        }
        replayLoadedView_ = view;
        replayLoadedPage_ = page;
    }

    if (sceneCheck) {
        const bool popupMatches = replayLoadedSceneCheckCount_ == sceneCheckCount &&
                                  replayPopupBaseTexture_ != nullptr &&
                                  replayPopupChipTexture_ != nullptr;
        if (!popupMatches) {
            if (replayPopupBaseTexture_ != nullptr) {
                SDL_DestroyTexture(replayPopupBaseTexture_);
                replayPopupBaseTexture_ = nullptr;
            }
            if (replayPopupChipTexture_ != nullptr) {
                SDL_DestroyTexture(replayPopupChipTexture_);
                replayPopupChipTexture_ = nullptr;
            }
            const std::string popupBaseLogical =
                SchoolDaysReplayModel::SceneCheckBaseRelativePath(sceneCheckCount);
            const std::string popupChipLogical =
                SchoolDaysReplayModel::SceneCheckChipRelativePath(sceneCheckCount);
            const auto popupBase = ResolveSchoolDaysAssetPath(
                replayAssetsRoot_.c_str(), popupBaseLogical.c_str());
            const auto popupChip = ResolveSchoolDaysAssetPath(
                replayAssetsRoot_.c_str(), popupChipLogical.c_str());
            if (!popupBase.present || popupBase.collision ||
                !popupChip.present || popupChip.collision) {
                if (error != nullptr)
                    *error = "School Days SceneCheck resources could not be resolved";
                return false;
            }
            replayPopupBaseTexture_ = KotonohaImageCreateTexture(
                renderer, popupBase.physical.c_str(), -1, -1);
            replayPopupChipTexture_ = KotonohaImageCreateTexture(
                renderer, popupChip.physical.c_str(), -1, -1);
            if (replayPopupBaseTexture_ == nullptr ||
                replayPopupChipTexture_ == nullptr) {
                if (error != nullptr)
                    *error = std::string("School Days SceneCheck texture load failed: ") +
                             SDL_GetError();
                return false;
            }
            // Pop_Replay base artwork is authored translucent.  Native
            // SceneCheck is layered over the retained SceneView parent.
            SDL_SetTextureBlendMode(replayPopupBaseTexture_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureBlendMode(replayPopupChipTexture_, SDL_BLENDMODE_BLEND);
            replayLoadedSceneCheckCount_ = sceneCheckCount;
        }
    } else {
        if (replayPopupBaseTexture_ != nullptr) {
            SDL_DestroyTexture(replayPopupBaseTexture_);
            replayPopupBaseTexture_ = nullptr;
        }
        if (replayPopupChipTexture_ != nullptr) {
            SDL_DestroyTexture(replayPopupChipTexture_);
            replayPopupChipTexture_ = nullptr;
        }
        replayLoadedSceneCheckCount_ = -1;
    }
    return true;
}

bool SchoolDaysSystemUi::OpenConfig(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysConfigMenuModel::Variant variant,
    SchoolDaysConfigMenuModel::Page page,
    const SchoolDaysConfigMenuRenderPlan::VisualState& visualState,
    std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days ConfigMenu renderer is null";
        return false;
    }
    if (!configMenu_.Open(assetsRoot, variant, page, error)) return false;
    configVisualState_ = visualState;

    const auto base = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(),
        SchoolDaysConfigMenuRenderPlan::BaseRelativePath(configMenu_,
                                                         configVisualState_));
    const auto chip = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysConfigMenuModel::ChipRelativePath(page));
    if (!base.present || base.collision || !chip.present || chip.collision) {
        if (error != nullptr)
            *error = "School Days ConfigMenu base/CHIP resources could not be resolved";
        Close();
        return false;
    }

    baseTexture_ =
        KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
    chipTexture_ =
        KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr)
            *error = std::string("School Days ConfigMenu texture load failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_NONE);
    if (!SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr)
            *error = std::string("School Days ConfigMenu CHIP blend setup failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    resultEmitted_ = false;
    return true;
}

bool SchoolDaysSystemUi::SetConfigVisualState(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    const SchoolDaysConfigMenuRenderPlan::VisualState& visualState,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (!configMenu_.IsOpen() || renderer == nullptr || baseTexture_ == nullptr) {
        if (error != nullptr)
            *error = "School Days ConfigMenu visual presenter is not ready";
        return false;
    }

    const std::string oldBase = SchoolDaysConfigMenuRenderPlan::BaseRelativePath(
        configMenu_, configVisualState_);
    const std::string newBase = SchoolDaysConfigMenuRenderPlan::BaseRelativePath(
        configMenu_, visualState);
    if (oldBase != newBase) {
        const auto resolved = ResolveSchoolDaysAssetPath(assetsRoot.c_str(), newBase);
        if (!resolved.present || resolved.collision) {
            if (error != nullptr)
                *error = "School Days ConfigMenu updated base resource could not be resolved";
            return false;
        }
        SDL_Texture* replacement =
            KotonohaImageCreateTexture(renderer, resolved.physical.c_str(), -1, -1);
        if (replacement == nullptr ||
            !SDL_SetTextureBlendMode(replacement, SDL_BLENDMODE_NONE)) {
            if (error != nullptr)
                *error = std::string("School Days ConfigMenu updated base texture load failed: ") +
                         SDL_GetError();
            if (replacement != nullptr) SDL_DestroyTexture(replacement);
            return false;
        }
        SDL_DestroyTexture(baseTexture_);
        baseTexture_ = replacement;
    }
    configVisualState_ = visualState;
    return true;
}

bool SchoolDaysSystemUi::OpenSomConSet(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysSomConSetModel::Variant variant, std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days SomconSet renderer is null";
        return false;
    }
    if (!somConSet_.Open(assetsRoot, variant, error)) return false;

    const auto base = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysSomConSetModel::BaseRelativePath());
    const auto chip = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysSomConSetModel::ChipRelativePath());
    if (!base.present || base.collision || !chip.present || chip.collision) {
        if (error != nullptr)
            *error = "School Days SomconSet base/CHIP resources could not be resolved";
        Close();
        return false;
    }

    baseTexture_ =
        KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
    chipTexture_ =
        KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr)
            *error = std::string("School Days SomconSet texture load failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_BLEND);
    if (!SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr)
            *error = std::string("School Days SomconSet CHIP blend setup failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    resultEmitted_ = false;
    return true;
}

bool SchoolDaysSystemUi::OpenExit(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysExitViewModel::Variant variant,
    SchoolDaysExitViewModel::BaseArtwork artwork, std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days ExitView renderer is null";
        return false;
    }
    if (!exitView_.Open(assetsRoot, variant, error)) return false;

    const auto base = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysExitViewModel::BaseRelativePath(artwork));
    const auto chip = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysExitViewModel::ChipRelativePath());
    if (!base.present || base.collision || !chip.present || chip.collision) {
        if (error != nullptr)
            *error = "School Days ExitView base/CHIP resources could not be resolved";
        Close();
        return false;
    }

    baseTexture_ =
        KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
    chipTexture_ =
        KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr)
            *error = std::string("School Days ExitView texture load failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_NONE);
    if (!SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr)
            *error = std::string("School Days ExitView CHIP blend setup failed: ") +
                     SDL_GetError();
        Close();
        return false;
    }
    resultEmitted_ = false;
    return true;
}

bool SchoolDaysSystemUi::OpenSaveLoad(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysSaveLoadModel::Variant variant, SchoolDaysSaveLoadModel::Mode mode,
    bool textInputEnabled,
    bool useEnglish, const std::string& fontDataLogicalPath,
    std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days SaveLoad renderer is null";
        return false;
    }

    namespace fs = std::filesystem;
    std::string saveLoadCmap;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), SchoolDaysSaveLoadModel::CmapRelativePath(variant),
            &saveLoadCmap, error, "SaveLoad CMAP")) {
        return false;
    }
    if (!saveLoad_.Open(saveLoadCmap, variant, mode, textInputEnabled, error)) {
        return false;
    }

    const auto base = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), mode == SchoolDaysSaveLoadModel::Mode::Load
                                ? "System/SaveLoad/Load"
                                : "System/SaveLoad/Save");
    const auto chip = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), "System/SaveLoad/SaveLoad_Chip");
    if (!base.present || base.collision || !chip.present || chip.collision) {
        if (error != nullptr) *error = "School Days SaveLoad base/CHIP resources could not be resolved";
        Close();
        return false;
    }

    fs::path fontLogical(fontDataLogicalPath);
    if (fontLogical.empty() || fontLogical.is_absolute()) {
        if (error != nullptr) *error = "School Days FONTDATA logical path is invalid";
        Close();
        return false;
    }
    if (useEnglish) {
        const auto ext = fontLogical.extension().string();
        const auto stem = fontLogical.stem().string();
        fontLogical.replace_filename(stem + "_eng" + ext);
    }
    std::string fontPhysical;
    if (!ResolveSchoolDaysRequiredAssetPath(
            assetsRoot.c_str(), fontLogical.generic_string(), &fontPhysical, error,
            "FONTDATA")) {
        Close();
        return false;
    }
    if (!saveLoadFont_.LoadFile(fontPhysical, error)) {
        Close();
        return false;
    }

    baseTexture_ = KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
    chipTexture_ = KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr) *error = std::string("School Days SaveLoad texture load failed: ") + SDL_GetError();
        Close();
        return false;
    }
    saveLoadTextTexture_ = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 2048, 1024);
    if (saveLoadTextTexture_ == nullptr ||
        !SDL_SetTextureBlendMode(saveLoadTextTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr) *error = std::string("School Days SaveLoad FONTDATA texture setup failed: ") + SDL_GetError();
        Close();
        return false;
    }
    saveLoadTextPixels_.assign(static_cast<size_t>(2048) * 1024u, 0u);
    saveLoadMetadata_.assign(10u, SchoolDaysSaveService::SlotMetadata{});
    saveLoadCommentAdvances_.assign(10u, 0);
    saveLoadUseEnglish_ = useEnglish;
    if (!SDL_UpdateTexture(saveLoadTextTexture_, nullptr, saveLoadTextPixels_.data(),
                           2048 * static_cast<int>(sizeof(uint32_t)))) {
        if (error != nullptr) *error = std::string("School Days SaveLoad FONTDATA texture clear failed: ") + SDL_GetError();
        Close();
        return false;
    }
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_NONE);
    if (!SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr) *error = std::string("School Days SaveLoad CHIP blend setup failed: ") + SDL_GetError();
        Close();
        return false;
    }
    layout_ = LayoutForVariant(variant);
    resultEmitted_ = false;
    return true;
}

bool SchoolDaysSystemUi::SetSaveLoadMetadata(
    const std::vector<SchoolDaysSaveService::SlotMetadata>& rows,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (!saveLoad_.IsOpen() || saveLoadTextTexture_ == nullptr ||
        !saveLoadFont_.IsLoaded() || rows.size() != 10u) {
        if (error != nullptr) *error = "School Days SaveLoad metadata presenter is not ready";
        return false;
    }
    std::fill(saveLoadTextPixels_.begin(), saveLoadTextPixels_.end(), 0u);
    saveLoadCommentAdvances_.assign(10u, 0);

    auto drawText = [&](const std::u16string& text, int limit, int startX,
                        int startY, int* renderedAdvance) -> bool {
        int x = startX;
        const size_t count = std::min(text.size(), static_cast<size_t>(limit));
        SchoolDaysFontData::Glyph glyph;
        for (size_t i = 0u; i < count; ++i) {
            const uint16_t code = static_cast<uint16_t>(text[i]);
            if (!saveLoadFont_.DecodeGlyph(code, &glyph, error)) return false;
            if (x < 0 || startY < 0 || x + SchoolDaysFontData::kGlyphWidth > 2048 ||
                startY + SchoolDaysFontData::kGlyphHeight > 1024) {
                if (error != nullptr) *error = "School Days FONTDATA glyph exceeds SaveLoad atlas";
                return false;
            }
            for (int gy = 0; gy < SchoolDaysFontData::kGlyphHeight; ++gy) {
                for (int gx = 0; gx < SchoolDaysFontData::kGlyphWidth; ++gx) {
                    const size_t source = static_cast<size_t>(gy) *
                                              SchoolDaysFontData::kGlyphWidth +
                                          static_cast<size_t>(gx);
                    const uint8_t alpha = glyph.alpha[source];
                    const uint8_t gray = glyph.gray[source];
                    const uint32_t packed = (static_cast<uint32_t>(alpha) << 24u) |
                                            (static_cast<uint32_t>(gray) << 16u) |
                                            (static_cast<uint32_t>(gray) << 8u) |
                                            static_cast<uint32_t>(gray);
                    uint32_t& destination = saveLoadTextPixels_[
                        static_cast<size_t>(startY + gy) * 2048u +
                        static_cast<size_t>(x + gx)];
                    if (destination < packed) destination = packed;
                }
            }
            x += SchoolDaysSaveLoadRenderPlan::GlyphAdvance(code, saveLoadUseEnglish_);
        }
        if (renderedAdvance != nullptr) *renderedAdvance = x - startX;
        return true;
    };

    for (int row = 0; row < 10; ++row) {
        const auto& metadata = rows[static_cast<size_t>(row)];
        if (!metadata.occupied) continue;
        const int y = 2 + 48 * row;
        if (!drawText(metadata.date,
                      SchoolDaysSaveLoadRenderPlan::PrimaryCharacterLimit(saveLoadUseEnglish_),
                      0, y, nullptr) ||
            !drawText(metadata.story,
                      SchoolDaysSaveLoadRenderPlan::SecondaryCharacterLimit(),
                      1024, y, nullptr)) {
            return false;
        }
        if (saveLoad_.TextInputEnabled() &&
            !drawText(metadata.comment,
                      SchoolDaysSaveLoadRenderPlan::CommentCharacterLimit(saveLoadUseEnglish_),
                      0, 514 + 48 * row,
                      &saveLoadCommentAdvances_[static_cast<size_t>(row)])) {
            return false;
        }
    }
    if (!SDL_UpdateTexture(saveLoadTextTexture_, nullptr, saveLoadTextPixels_.data(),
                           2048 * static_cast<int>(sizeof(uint32_t)))) {
        if (error != nullptr) *error = std::string("School Days SaveLoad FONTDATA atlas upload failed: ") + SDL_GetError();
        return false;
    }
    saveLoadMetadata_ = rows;
    std::array<bool, 10> occupancy{};
    for (size_t i = 0; i < occupancy.size(); ++i) {
        occupancy[i] = rows[i].occupied;
    }
    saveLoad_.SetRowOccupancy(occupancy);
    return true;
}

bool SchoolDaysSystemUi::OpenTitle(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysTitleModel::Variant variant,
    const SchoolDaysTitleModel::HostState& hostState,
    const SchoolDaysEndBackgroundResolver::Result& background,
    const std::string& baseBackgroundAsset,
    std::string* error) {
    Close();
    if (renderer == nullptr || background.asset.empty()) {
        if (error != nullptr) *error = "School Days Title presenter arguments are invalid";
        return false;
    }

    if (!title_.Open(assetsRoot, variant, hostState, error)) return false;

    const auto overlay = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), SchoolDaysTitleModel::ResourceStem(title_.CurrentFamily()));
    const auto chip = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), SchoolDaysTitleModel::ChipStem(title_.CurrentFamily()));
    if (!overlay.present || overlay.collision || !chip.present || chip.collision) {
        if (error != nullptr) *error = "School Days Title overlay/CHIP resources could not be resolved";
        Close();
        return false;
    }

    baseTexture_ =
        KotonohaImageCreateTexture(renderer, overlay.physical.c_str(), -1, -1);
    chipTexture_ =
        KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr) *error = std::string("School Days Title overlay texture load failed: ") + SDL_GetError();
        Close();
        return false;
    }
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND);

    std::filesystem::path selectedPath(background.asset);
    std::string extension = selectedPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    auto loadStill = [&](const std::string& logical, float height,
                         bool required) -> bool {
        const auto resolved = ResolveSchoolDaysAssetPath(assetsRoot.c_str(), logical);
        if (!resolved.present || resolved.collision) {
            if (required && error != nullptr)
                *error = "School Days Title background asset could not be resolved";
            return !required;
        }
        SDL_Texture* texture =
            KotonohaImageCreateTexture(renderer, resolved.physical.c_str(), -1, -1);
        if (texture == nullptr) {
            if (required && error != nullptr)
                *error = std::string("School Days Title background texture load failed: ") + SDL_GetError();
            return !required;
        }
        if (titleBackgroundTexture_ != nullptr)
            SDL_DestroyTexture(titleBackgroundTexture_);
        titleBackgroundTexture_ = texture;
        SDL_SetTextureBlendMode(titleBackgroundTexture_, SDL_BLENDMODE_NONE);
        titleBackgroundHeight_ = height;
        return true;
    };

    if (extension == ".png") {
        const float height =
            background.kind == SchoolDaysEndBackgroundResolver::Kind::Base
                ? 450.0f
                : 452.0f;
        if (!loadStill(background.asset, height, true)) {
            Close();
            return false;
        }
    } else {
        // Keep BaseFile ready solely as the authenticated movie-open/decode
        // fallback. It is not drawn while a healthy WMV owns the background.
        (void)loadStill(baseBackgroundAsset, 450.0f, false);
        const auto movie = ResolveSchoolDaysAssetPath(assetsRoot.c_str(), background.asset);
        if (movie.status == SchoolDaysAssetResolutionStatus::Collision ||
            movie.status == SchoolDaysAssetResolutionStatus::Invalid) {
            if (error != nullptr) {
                *error = movie.status == SchoolDaysAssetResolutionStatus::Collision
                    ? "School Days Title movie path has a case-insensitive collision"
                    : "School Days Title movie path is invalid";
            }
            Close();
            return false;
        }
        if (movie.status == SchoolDaysAssetResolutionStatus::Present) {
            titleVideoClock_ = Kotonoha_timeNew(false);
            if (titleVideoClock_ != nullptr) {
                Kotonoha_timePause(titleVideoClock_);
                Kotonoha_timeSet(titleVideoClock_, 0u);
                titleVideo_ = Kotonoha_VideoRenderInit(
                    movie.physical.c_str(), titleVideoClock_, 0u,
                    std::numeric_limits<Uint64>::max(),
                    std::numeric_limits<Kotonoha_SceneTick>::max());
            }
        }
        if (titleVideo_ != nullptr) {
            titleVideoStartMs_ = SDL_GetTicks();
            titleBackgroundHeight_ = 452.0f;
        } else {
            if (titleVideoClock_ != nullptr) {
                Kotonoha_timeDestroy(titleVideoClock_);
                titleVideoClock_ = nullptr;
            }
            if (titleBackgroundTexture_ == nullptr &&
                !loadStill(baseBackgroundAsset, 450.0f, true)) {
                Close();
                return false;
            }
            titleBackgroundHeight_ = 450.0f;
        }
    }
    layout_ = LayoutForVariant(variant);
    return true;
}

bool SchoolDaysSystemUi::OpenSaveFeel(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysSaveFeelModel::Variant variant, std::string* error) {
    Close();
    if (renderer == nullptr) {
        if (error != nullptr) *error = "School Days SystemUi renderer is null";
        return false;
    }

    if (!saveFeel_.Open(assetsRoot, variant, error)) return false;
    layout_ = LayoutForVariant(variant);

    const auto base = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), "System/SaveLoad/Pop_Save_Feel");
    const auto chip = ResolveSchoolDaysImageAsset(
        assetsRoot.c_str(), "System/SaveLoad/Pop_Save_Feel_Chip");
    if (!base.present || base.collision || !chip.present || chip.collision) {
        if (error != nullptr) {
            *error = "School Days SaveFeel PNG/CHIP asset could not be resolved";
        }
        Close();
        return false;
    }

    baseTexture_ = KotonohaImageCreateTexture(renderer, base.physical.c_str(), -1, -1);
    chipTexture_ = KotonohaImageCreateTexture(renderer, chip.physical.c_str(), -1, -1);
    if (baseTexture_ == nullptr || chipTexture_ == nullptr) {
        if (error != nullptr) {
            *error = std::string("School Days SaveFeel texture load failed: ") +
                     SDL_GetError();
        }
        Close();
        return false;
    }

    // Base is intentionally opaque in the shipped asset. CHIP carries alpha.
    SDL_SetTextureBlendMode(baseTexture_, SDL_BLENDMODE_NONE);
    if (!SDL_SetTextureBlendMode(chipTexture_, SDL_BLENDMODE_BLEND)) {
        if (error != nullptr) {
            *error = std::string("School Days SaveFeel CHIP blend setup failed: ") +
                     SDL_GetError();
        }
        Close();
        return false;
    }
    resultEmitted_ = false;
    return true;
}

SchoolDaysSystemUi::Viewport SchoolDaysSystemUi::ComputeViewport(
    int outputWidth, int outputHeight, int virtualWidth, int virtualHeight) {
    Viewport viewport;
    if (outputWidth <= 0 || outputHeight <= 0 || virtualWidth <= 0 ||
        virtualHeight <= 0) {
        viewport.scale = 0.0f;
        return viewport;
    }
    const float sx = static_cast<float>(outputWidth) /
                     static_cast<float>(virtualWidth);
    const float sy = static_cast<float>(outputHeight) /
                     static_cast<float>(virtualHeight);
    viewport.scale = std::min(sx, sy);
    viewport.x = (static_cast<float>(outputWidth) -
                  static_cast<float>(virtualWidth) * viewport.scale) *
                 0.5f;
    viewport.y = (static_cast<float>(outputHeight) -
                  static_cast<float>(virtualHeight) * viewport.scale) *
                 0.5f;
    return viewport;
}

SDL_FRect SchoolDaysSystemUi::TransformRect(const SDL_FRect& native,
                                            const Viewport& viewport) {
    return {viewport.x + native.x * viewport.scale,
            viewport.y + native.y * viewport.scale,
            native.w * viewport.scale, native.h * viewport.scale};
}

bool SchoolDaysSystemUi::PointerToVirtual(SDL_Renderer* renderer, float x, float y,
                                          int* virtualX, int* virtualY) const {
    if (renderer == nullptr || virtualX == nullptr || virtualY == nullptr ||
        (!saveFeel_.IsOpen() && !saveLoad_.IsOpen() && !title_.IsOpen() &&
         !configMenu_.IsOpen() && !somConSet_.IsOpen() && !exitView_.IsOpen() && !replay_.IsOpen() &&
         !routeMap_.IsOpen())) {
        return false;
    }

    float renderX = 0.0f;
    float renderY = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(renderer, x, y, &renderX, &renderY)) {
        return false;
    }
    int width = 0;
    int height = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) return false;
    const SchoolDaysCmap& cmap = saveFeel_.IsOpen()
        ? saveFeel_.Cmap()
        : (saveLoad_.IsOpen()
               ? saveLoad_.Cmap()
               : (title_.IsOpen()
                      ? title_.Cmap()
                      : (configMenu_.IsOpen() ? configMenu_.Cmap()
                                              : (somConSet_.IsOpen()
                                                     ? somConSet_.Cmap()
                                                     : (exitView_.IsOpen()
                                                            ? exitView_.Cmap()
                                                            : (replay_.IsOpen()
                                                                   ? replay_.Cmap()
                                                                   : routeMap_.Cmap()))))));
    const auto viewport = ComputeViewport(
        width, height, static_cast<int>(cmap.Width()), static_cast<int>(cmap.Height()));
    if (viewport.scale <= 0.0f) return false;
    const float vx = (renderX - viewport.x) / viewport.scale;
    const float vy = (renderY - viewport.y) / viewport.scale;
    if (vx < 0.0f || vy < 0.0f ||
        vx >= static_cast<float>(cmap.Width()) ||
        vy >= static_cast<float>(cmap.Height())) {
        // A valid window->render conversion outside the logical CMAP is still
        // meaningful input: native hover clears selection to -1.  Reserve a
        // false return for actual transform/query failures so HandleEvent can
        // distinguish letterbox/background from an SDL error.
        *virtualX = -1;
        *virtualY = -1;
        return true;
    }
    *virtualX = static_cast<int>(std::floor(vx));
    *virtualY = static_cast<int>(std::floor(vy));
    return true;
}

bool SchoolDaysSystemUi::EmitModelResult(SchoolDaysSaveFeelModel::Result result,
                                         Output* output) {
    if (output == nullptr || resultEmitted_) return false;
    output->hasSaveFeelResult = true;
    output->saveFeelResult = result;
    resultEmitted_ = true;
    return true;
}

bool SchoolDaysSystemUi::DrawTitleBackground(SDL_Renderer* renderer,
                                             const Viewport& viewport,
                                             std::string* error) {
    if (renderer == nullptr) return false;

    if (titleVideo_ != nullptr && titleVideoClock_ != nullptr) {
        const uint64_t elapsed = SDL_GetTicks() - titleVideoStartMs_;
        const uint64_t frame = SchoolDaysTitleMovieClock::FrameIndex(elapsed);
        const uint64_t targetMs =
            SchoolDaysTitleMovieClock::TargetMilliseconds(frame);
        Kotonoha_timeSet(titleVideoClock_, targetMs);

        bool failed = false;
        for (;;) {
            const Uint64 oldVideoTime = titleVideo_->videoTime;
            SDL_Texture* const oldTexture = titleVideo_->texture;
            const auto status =
                Kotonoha_VideoRenderProcess(titleVideo_, renderer);
            if (status == KOTONOHA_SCENE_FATAL_ERROR ||
                status == KOTONOHA_SCENE_COMPLETE ||
                status == KOTONOHA_SCENE_NULL) {
                failed = true;
                break;
            }
            if (titleVideo_->decoderEof) break;
            if (titleVideo_->videoTime >= targetMs &&
                titleVideo_->texture != nullptr) {
                break;
            }
            if (titleVideo_->currentFrame != nullptr &&
                titleVideo_->currentPtsMs > targetMs) {
                break;
            }
            const bool progressed = titleVideo_->videoTime != oldVideoTime ||
                                    titleVideo_->texture != oldTexture;
            if (!progressed) break;
        }

        if (failed) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                        "School Days Title EndBG movie failed; using BaseFile fallback");
            Kotonoha_VideoRenderShutdown(&titleVideo_);
            Kotonoha_timeDestroy(titleVideoClock_);
            titleVideoClock_ = nullptr;
            titleVideoStartMs_ = 0u;
            titleBackgroundHeight_ = 450.0f;
            if (titleBackgroundTexture_ == nullptr) {
                if (error != nullptr)
                    *error = "School Days Title movie failed without BaseFile fallback";
                return false;
            }
        }
    }

    const auto nativeLayout =
        SchoolDaysTitleRenderPlan::LayoutFor(title_.CurrentVariant());
    const SDL_FRect bgNative = {
        0.0f, nativeLayout.yOffset * nativeLayout.scale,
        800.0f * nativeLayout.scale,
        titleBackgroundHeight_ * nativeLayout.scale};
    const SDL_FRect bgDst = TransformRect(bgNative, viewport);

    if (titleVideo_ != nullptr) {
        if (titleVideo_->texture == nullptr) {
            // Retail keeps the movie object active while waiting for its first
            // sample. Do not substitute BaseFile unless open/decode failed.
            return true;
        }
        if (!SDL_RenderTexture(renderer, titleVideo_->texture, nullptr, &bgDst)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        return true;
    }

    if (titleBackgroundTexture_ == nullptr) {
        if (error != nullptr) *error = "School Days Title background texture is unavailable";
        return false;
    }
    if (!SDL_RenderTexture(renderer, titleBackgroundTexture_, nullptr, &bgDst)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    return true;
}

bool SchoolDaysSystemUi::SubmitSaveLoadComment(const std::u16string& comment) {
    return saveLoad_.SubmitComment(comment);
}

bool SchoolDaysSystemUi::CancelSaveLoadComment() {
    return saveLoad_.CancelComment();
}

bool SchoolDaysSystemUi::Activate(Output* output) {
    SchoolDaysSaveFeelModel::Result result{};
    if (!saveFeel_.Activate(&result)) return false;
    return EmitModelResult(result, output);
}

void SchoolDaysSystemUi::Previous() {
    saveFeel_.Previous();
}

void SchoolDaysSystemUi::Next() {
    saveFeel_.Next();
}

bool SchoolDaysSystemUi::HandleEvent(SDL_Renderer* renderer,
                                     const SDL_Event* event, Output* output) {
    if (output != nullptr) *output = {};
    if ((!saveFeel_.IsOpen() && !saveLoad_.IsOpen() && !title_.IsOpen() &&
         !configMenu_.IsOpen() && !somConSet_.IsOpen() && !exitView_.IsOpen() && !replay_.IsOpen() &&
         !routeMap_.IsOpen()) ||
        event == nullptr || (resultEmitted_ && !saveLoad_.IsOpen())) return false;

    float x = 0.0f;
    float y = 0.0f;
    bool pointerMoved = false;
    bool pointerActivated = false;
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        x = event->motion.x;
        y = event->motion.y;
        pointerMoved = true;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        x = event->button.x;
        y = event->button.y;
        pointerMoved = true;
        pointerActivated = true;
    } else {
        return false;
    }

    int virtualX = 0;
    int virtualY = 0;
    if (!PointerToVirtual(renderer, x, y, &virtualX, &virtualY)) return false;
    // Activation can cross the presentation/semantic boundary (SaveFeel result,
    // SaveLoad close/comment/save request).  A null output sink must never make
    // those transitions disappear while still mutating the model.  Hover is
    // safe without a sink, so only fail closed for activation.
    if (pointerActivated && output == nullptr) return false;
    if (replay_.IsOpen()) {
        if (pointerActivated) {
            const int hit = replay_.HitControl(virtualX, virtualY);
            const int selected = replay_.SceneCheckOpen()
                ? replay_.SceneCheckSelection() : replay_.Selection();
            if (hit < 0 || hit != selected) return false;
            SchoolDaysReplayModel::Action action;
            if (!replay_.Activate(&action)) return false;
            if (action.kind != SchoolDaysReplayModel::ActionKind::None) {
                output->hasReplayAction = true;
                output->replayAction = action;
            }
            // SceneView ordinary activation calls id2; its explicit close path
            // calls the shared cancel preset id0.
            output->uiSfx =
                action.kind == SchoolDaysReplayModel::ActionKind::Close
                    ? UiSfxCue::Cancel : UiSfxCue::Click;
            return true;
        }
        if (!pointerMoved) return false;
        const bool changed = replay_.SetSelectionFromPointer(virtualX, virtualY);
        if (changed && output != nullptr && !replay_.SceneCheckOpen() &&
            replay_.CurrentView() == SchoolDaysReplayModel::View::PlayData &&
            replay_.Selection() >= 23 && replay_.Selection() <= 32) {
            // 0x1001EB16..0x1001EB62: secondary PlayData row-hit transition.
            output->uiSfx = UiSfxCue::Open;
        }
        return changed;
    }
    if (routeMap_.IsOpen()) {
        if (pointerActivated) {
            const int hit = routeMap_.HitControl(virtualX, virtualY);
            if (hit < 0 || hit != routeMap_.Selection()) return false;
            const int oldMajor = routeMap_.Major();
            const int oldSubpage = routeMap_.Subpage();
            SchoolDaysRouteMapModel::Action action;
            std::string routeError;
            if (!routeMap_.Activate(&action, &routeError)) return false;
            if ((routeMap_.Major() != oldMajor || routeMap_.Subpage() != oldSubpage) &&
                !EnsureRouteMapTextures(renderer, &routeError)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "School Days RouteMap page resource reload failed: %s",
                             routeError.c_str());
                return false;
            }
            if (action.kind != SchoolDaysRouteMapModel::ActionKind::None) {
                output->hasRouteMapAction = true;
                output->routeMapAction = action;
            }
            return true;
        }
        return pointerMoved && routeMap_.SetSelectionFromPointer(virtualX, virtualY);
    }
    if (somConSet_.IsOpen()) {
        if (pointerActivated) {
            const int hit = somConSet_.HitControl(virtualX, virtualY);
            if (hit < 0 || hit != somConSet_.Selection()) return false;
            int transition = -1;
            if (!somConSet_.Activate(&transition) ||
                transition != SchoolDaysSomConSetModel::kReturnToConfigHostTransition) {
                return false;
            }
            output->hasSomConSetReturn = true;
            return true;
        }
        return pointerMoved && somConSet_.SetSelectionFromPointer(virtualX, virtualY);
    }
    if (configMenu_.IsOpen()) {
        if (pointerActivated) {
            const int hit = configMenu_.HitControl(virtualX, virtualY);
            if (hit < 0 || hit != configMenu_.Selection()) return false;
            SchoolDaysConfigMenuModel::Action action;
            if (!configMenu_.Activate(&action)) return false;
            if (action.kind == SchoolDaysConfigMenuModel::ActionKind::SelectPage &&
                action.page == SchoolDaysConfigMenuModel::Page::SomCon &&
                !configVisualState_.somConAvailable) {
                return false;
            }
            if (configMenu_.CurrentPage() == SchoolDaysConfigMenuModel::Page::SomCon &&
                hit >= 6 && hit <= 17 &&
                (!configVisualState_.somConSetActive ||
                 !configVisualState_.somConInteractionActive)) {
                return false;
            }
            output->hasConfigAction = true;
            output->configAction = action;
            // ConfigMenu ordinary activation uses id2; close/back uses id0.
            output->uiSfx =
                action.kind == SchoolDaysConfigMenuModel::ActionKind::Close
                    ? UiSfxCue::Cancel : UiSfxCue::Click;
            return true;
        }
        return pointerMoved &&
               configMenu_.SetSelectionFromPointer(virtualX, virtualY);
    }
    if (title_.IsOpen()) {
        if (pointerActivated) {
            const int hit = title_.HitItem(virtualX, virtualY);
            if (hit < 0 || hit != title_.Selection() ||
                !SchoolDaysTitleModel::ItemAvailable(
                    hit, title_.CurrentHostState())) {
                return false;
            }
            SchoolDaysTitleModel::Activation activation;
            if (!title_.ActivateSelection(&activation)) return false;
            output->hasTitleActivation = true;
            output->titleActivation = activation;
            // TitleView 0x10020BFC/0x10020C53 -> host SFX id2.
            output->uiSfx = UiSfxCue::Click;
            return true;
        }
        return pointerMoved && title_.SetSelectionFromPointer(virtualX, virtualY);
    }
    if (exitView_.IsOpen()) {
        if (pointerActivated) {
            const int hit = exitView_.HitChoice(virtualX, virtualY);
            if (hit < 0 || hit != exitView_.Selection()) return false;
            SchoolDaysExitViewModel::Activation activation;
            if (!exitView_.Activate(&activation)) return false;
            output->hasExitActivation = true;
            output->exitActivation = activation;
            return true;
        }
        return pointerMoved && exitView_.SetSelectionFromPointer(virtualX, virtualY);
    }
    if (saveLoad_.IsOpen()) {
        if (pointerActivated) {
            const int hit = saveLoad_.HitControl(virtualX, virtualY);
            if (hit < 0 || hit != saveLoad_.Selection() || saveLoad_.SavePending()) {
                return false;
            }
            SchoolDaysSaveLoadModel::Output modelOutput;
            if (!saveLoad_.Activate(&modelOutput)) return false;
            if (modelOutput.action != SchoolDaysSaveLoadModel::Action::None &&
                output != nullptr) {
                output->hasSaveLoadOutput = true;
                output->saveLoadOutput = modelOutput;
            }
            // SaveLoad ordinary activation uses id2; its explicit close/back
            // path uses the shared cancel preset id0.
            output->uiSfx =
                modelOutput.action == SchoolDaysSaveLoadModel::Action::Close
                    ? UiSfxCue::Cancel : UiSfxCue::Click;
            return true;
        }
        if (!pointerMoved) return false;
        const bool changed = saveLoad_.SetSelectionFromPointer(virtualX, virtualY);
        if (changed && output != nullptr && saveLoad_.Selection() >= 22 &&
            saveLoad_.Selection() <= 31) {
            // 0x10015061..0x10015095: secondary SaveLoad row-hit transition.
            output->uiSfx = UiSfxCue::Open;
        }
        return changed;
    }
    if (pointerActivated) {
        // Native coordinate activation recomputes the CMAP hit and accepts only
        // when it already matches the current selection.  Do not turn a click
        // on a different control into an implicit hover+activate operation.
        const int hit = saveFeel_.HitChoice(virtualX, virtualY);
        if (saveFeel_.Interactive() && hit >= 0 &&
            hit == saveFeel_.Selection()) {
            return Activate(output);
        }
        return false;
    }
    if (pointerMoved) {
        return saveFeel_.SetSelectionFromPointer(virtualX, virtualY);
    }
    return false;
}

void SchoolDaysSystemUi::Update(uint32_t nowMs, int64_t feeling001,
                                int64_t feeling002, Output* output) {
    if (output != nullptr) *output = {};
    if (saveLoad_.IsOpen()) {
        // Tick consumes the deferred native SAVE request.  Do not dequeue it
        // unless the caller supplied somewhere to publish the resulting slot.
        if (output == nullptr) return;
        SchoolDaysSaveLoadModel::Output modelOutput;
        if (saveLoad_.Tick(&modelOutput) &&
            modelOutput.action != SchoolDaysSaveLoadModel::Action::None &&
            output != nullptr) {
            output->hasSaveLoadOutput = true;
            output->saveLoadOutput = modelOutput;
        }
        return;
    }
    if (!saveFeel_.IsOpen() || resultEmitted_) return;
    saveFeel_.SetFeelings(feeling001, feeling002);
    saveFeel_.Tick(nowMs);

    SchoolDaysSaveFeelModel::Result result{};
    if (saveFeel_.CountdownResult(&result)) {
        EmitModelResult(result, output);
    }
}

bool SchoolDaysSystemUi::Render(SDL_Renderer* renderer, std::string* error) {
    if (routeMap_.IsOpen()) {
        if (renderer == nullptr || baseTexture_ == nullptr || chipTexture_ == nullptr) {
            if (error != nullptr) *error = "School Days RouteMap presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight, static_cast<int>(routeMap_.Cmap().Width()),
            static_cast<int>(routeMap_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days RouteMap viewport is invalid";
            return false;
        }
        const auto plan = SchoolDaysRouteMapRenderPlan::Build(routeMap_);
        for (const auto& sprite : plan) {
            SDL_Texture* texture =
                sprite.texture == SchoolDaysRouteMapRenderPlan::Texture::Base
                    ? baseTexture_ : chipTexture_;
            const SDL_FRect nativeDst = {sprite.destination.x, sprite.destination.y,
                                         sprite.destination.w, sprite.destination.h};
            const SDL_FRect dst = TransformRect(nativeDst, viewport);
            const SDL_FRect source = {sprite.source.x, sprite.source.y,
                                      sprite.source.w, sprite.source.h};
            const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
            if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
        if (routeMapTextTexture_ == nullptr) {
            if (error != nullptr) *error = "School Days RouteMap FONTDATA texture is unavailable";
            return false;
        }
        const auto text = SchoolDaysRouteMapRenderPlan::CompletionTextFor(
            routeMap_, routeMapUseEnglish_);
        const SDL_FRect nativeTextDst = {text.destination.x, text.destination.y,
                                         text.destination.w, text.destination.h};
        const SDL_FRect textDst = TransformRect(nativeTextDst, viewport);
        const SDL_FRect textSource = {0.0f, 0.0f, 512.0f, 48.0f};
        if (!SDL_RenderTexture(renderer, routeMapTextTexture_, &textSource,
                               &textDst)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        return true;
    }
    if (replay_.IsOpen()) {
        if (renderer == nullptr || !EnsureReplayTextures(renderer, error)) {
            if (error != nullptr && error->empty())
                *error = "School Days Replay presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight, static_cast<int>(replay_.Cmap().Width()),
            static_cast<int>(replay_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days Replay viewport is invalid";
            return false;
        }

        auto drawReplayPlan = [&](
            const std::vector<SchoolDaysReplayRenderPlan::Sprite>& plan,
            bool popupTextures) -> bool {
            for (const auto& sprite : plan) {
                SDL_Texture* texture = nullptr;
                switch (sprite.texture) {
                case SchoolDaysReplayRenderPlan::Texture::Base:
                    texture = popupTextures ? replayPopupBaseTexture_ : baseTexture_;
                    break;
                case SchoolDaysReplayRenderPlan::Texture::Chip:
                    texture = popupTextures ? replayPopupChipTexture_ : chipTexture_;
                    break;
                case SchoolDaysReplayRenderPlan::Texture::Thumbnail:
                    texture = replayThumbnailTexture_;
                    break;
                }
                if (texture == nullptr) {
                    if (error != nullptr) *error = "School Days Replay texture is unavailable";
                    return false;
                }
                const SDL_FRect nativeDst = {
                    sprite.destination.x, sprite.destination.y,
                    sprite.destination.w, sprite.destination.h};
                const SDL_FRect dst = TransformRect(nativeDst, viewport);
                const SDL_FRect source = {
                    sprite.source.x, sprite.source.y,
                    sprite.source.w, sprite.source.h};
                const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
                if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
                    if (error != nullptr) *error = SDL_GetError();
                    return false;
                }
            }
            return true;
        };

        // SceneCheck is a nested modal: the retained SceneView stays visible
        // underneath the translucent Pop_Replay artwork.
        const auto parentPlan = SchoolDaysReplayRenderPlan::Build(replay_);
        if (!drawReplayPlan(parentPlan, false)) return false;

        if (replay_.SceneCheckOpen()) {
            SchoolDaysReplayRenderPlan::SceneCheckVisualState visual;
            visual.previewRecord = replay_.SceneCheckPreviewRecord();
            for (int i = 0; i < 4; ++i)
                visual.enabled[static_cast<size_t>(i)] =
                    replay_.IsSceneCheckChoiceAvailable(i);
            const auto popupPlan =
                SchoolDaysReplayRenderPlan::BuildSceneCheck(replay_, visual);
            if (!drawReplayPlan(popupPlan, true)) return false;
        }
        return true;
    }
    if (somConSet_.IsOpen()) {
        if (renderer == nullptr || baseTexture_ == nullptr || chipTexture_ == nullptr) {
            if (error != nullptr) *error = "School Days SomconSet presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight, static_cast<int>(somConSet_.Cmap().Width()),
            static_cast<int>(somConSet_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days SomconSet viewport is invalid";
            return false;
        }
        const auto nativeLayout =
            SchoolDaysConfigMenuRenderPlan::LayoutFor(somConSet_.CurrentVariant());
        const SDL_FRect nativeDst = {
            -0.5f * nativeLayout.scale,
            nativeLayout.yOffset * nativeLayout.scale,
            800.0f * nativeLayout.scale,
            450.0f * nativeLayout.scale};
        const SDL_FRect dst = TransformRect(nativeDst, viewport);
        if (!SDL_RenderTexture(renderer, baseTexture_, nullptr, &dst)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        // Pop_Som_Chip is the popup's single transparent hover artwork.  With
        // one native control there is no atlas-selection ambiguity.
        if (somConSet_.Selection() == 0 &&
            !SDL_RenderTexture(renderer, chipTexture_, nullptr, &dst)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        return true;
    }
    if (configMenu_.IsOpen()) {
        if (renderer == nullptr || baseTexture_ == nullptr || chipTexture_ == nullptr) {
            if (error != nullptr) *error = "School Days ConfigMenu presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight,
            static_cast<int>(configMenu_.Cmap().Width()),
            static_cast<int>(configMenu_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days ConfigMenu viewport is invalid";
            return false;
        }
        const auto plan =
            SchoolDaysConfigMenuRenderPlan::Build(configMenu_, configVisualState_);
        for (const auto& sprite : plan) {
            SDL_Texture* texture =
                sprite.texture == SchoolDaysConfigMenuRenderPlan::Texture::Chip
                    ? chipTexture_
                    : baseTexture_;
            const SDL_FRect nativeDst = {sprite.destination.x, sprite.destination.y,
                                         sprite.destination.w, sprite.destination.h};
            const SDL_FRect dst = TransformRect(nativeDst, viewport);
            const SDL_FRect source = {sprite.source.x, sprite.source.y,
                                      sprite.source.w, sprite.source.h};
            const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
            if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
        return true;
    }
    if (exitView_.IsOpen()) {
        if (renderer == nullptr || baseTexture_ == nullptr || chipTexture_ == nullptr) {
            if (error != nullptr) *error = "School Days ExitView presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight, static_cast<int>(exitView_.Cmap().Width()),
            static_cast<int>(exitView_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days ExitView viewport is invalid";
            return false;
        }
        const auto plan = SchoolDaysExitViewRenderPlan::Build(exitView_);
        for (const auto& sprite : plan) {
            SDL_Texture* texture =
                sprite.texture == SchoolDaysExitViewRenderPlan::Texture::Base
                    ? baseTexture_ : chipTexture_;
            const SDL_FRect nativeDst = {sprite.destination.x, sprite.destination.y,
                                         sprite.destination.w, sprite.destination.h};
            const SDL_FRect dst = TransformRect(nativeDst, viewport);
            const SDL_FRect source = {sprite.source.x, sprite.source.y,
                                      sprite.source.w, sprite.source.h};
            const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
            if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
        return true;
    }
    if (title_.IsOpen()) {
        if (renderer == nullptr || baseTexture_ == nullptr || chipTexture_ == nullptr ||
            (titleBackgroundTexture_ == nullptr && titleVideo_ == nullptr)) {
            if (error != nullptr) *error = "School Days Title presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight, static_cast<int>(title_.Cmap().Width()),
            static_cast<int>(title_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days Title viewport is invalid";
            return false;
        }
        const auto plan = SchoolDaysTitleRenderPlan::Build(title_);

        if (!DrawTitleBackground(renderer, viewport, error)) return false;

        for (const auto& sprite : plan) {
            SDL_Texture* texture =
                sprite.texture == SchoolDaysTitleRenderPlan::Texture::Overlay
                    ? baseTexture_ : chipTexture_;
            const SDL_FRect nativeDst = {sprite.destination.x, sprite.destination.y,
                                         sprite.destination.w, sprite.destination.h};
            const SDL_FRect dst = TransformRect(nativeDst, viewport);
            const SDL_FRect source = {sprite.source.x, sprite.source.y,
                                      sprite.source.w, sprite.source.h};
            const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
            if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
        return true;
    }

    if (saveLoad_.IsOpen()) {
        if (renderer == nullptr || baseTexture_ == nullptr || chipTexture_ == nullptr) {
            if (error != nullptr) *error = "School Days SaveLoad presenter is not open";
            return false;
        }
        int outputWidth = 0;
        int outputHeight = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
        const auto viewport = ComputeViewport(
            outputWidth, outputHeight, static_cast<int>(saveLoad_.Cmap().Width()),
            static_cast<int>(saveLoad_.Cmap().Height()));
        if (viewport.scale <= 0.0f) {
            if (error != nullptr) *error = "School Days SaveLoad viewport is invalid";
            return false;
        }
        const auto plan = SchoolDaysSaveLoadRenderPlan::Build(saveLoad_);
        for (const auto& sprite : plan) {
            SDL_Texture* texture =
                sprite.texture == SchoolDaysSaveLoadRenderPlan::Texture::Base
                    ? baseTexture_ : chipTexture_;
            const SDL_FRect nativeDst = {sprite.destination.x, sprite.destination.y,
                                         sprite.destination.w, sprite.destination.h};
            const SDL_FRect dst = TransformRect(nativeDst, viewport);
            const SDL_FRect source = {sprite.source.x, sprite.source.y,
                                      sprite.source.w, sprite.source.h};
            const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
            if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
        if (saveLoadTextTexture_ == nullptr || saveLoadMetadata_.size() != 10u) {
            if (error != nullptr) *error = "School Days SaveLoad FONTDATA atlas is unavailable";
            return false;
        }
        const auto metadataLayout = SchoolDaysSaveLoadRenderPlan::MetadataLayout(
            saveLoad_.CurrentVariant(), saveLoadUseEnglish_,
            &saveLoadCommentAdvances_);
        for (size_t row = 0u; row < metadataLayout.size(); ++row) {
            if (!saveLoadMetadata_[row].occupied) continue;
            const auto drawMetadata = [&] (
                const SchoolDaysSaveLoadRenderPlan::Rect& sourceRect,
                const SchoolDaysSaveLoadRenderPlan::Rect& destinationRect) {
                const SDL_FRect source = {sourceRect.x, sourceRect.y,
                                          sourceRect.w, sourceRect.h};
                const SDL_FRect nativeDestination = {
                    destinationRect.x, destinationRect.y,
                    destinationRect.w, destinationRect.h};
                const SDL_FRect destination =
                    TransformRect(nativeDestination, viewport);
                return SDL_RenderTexture(renderer, saveLoadTextTexture_, &source,
                                         &destination);
            };
            const auto& metadata = metadataLayout[row];
            if (!drawMetadata(metadata.primarySource, metadata.primary) ||
                !drawMetadata(metadata.secondarySource, metadata.secondary) ||
                (saveLoad_.TextInputEnabled() &&
                 !drawMetadata(metadata.optionalThirdSource,
                               metadata.optionalThird))) {
                if (error != nullptr) *error = SDL_GetError();
                return false;
            }
        }
        return true;
    }

    if (!saveFeel_.IsOpen() || renderer == nullptr || baseTexture_ == nullptr ||
        chipTexture_ == nullptr) {
        if (error != nullptr) *error = "School Days SaveFeel presenter is not open";
        return false;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    const auto viewport = ComputeViewport(
        outputWidth, outputHeight, static_cast<int>(saveFeel_.Cmap().Width()),
        static_cast<int>(saveFeel_.Cmap().Height()));
    if (viewport.scale <= 0.0f) {
        if (error != nullptr) *error = "School Days SaveFeel viewport is invalid";
        return false;
    }

    const auto plan = SchoolDaysSaveFeelRenderPlan::Build(saveFeel_);
    for (const auto& sprite : plan) {
        SDL_Texture* texture =
            sprite.texture == SchoolDaysSaveFeelRenderPlan::Texture::Base
                ? baseTexture_
                : chipTexture_;
        const SDL_FRect nativeDst = {sprite.destination.x, sprite.destination.y,
                                     sprite.destination.w, sprite.destination.h};
        const SDL_FRect dst = TransformRect(nativeDst, viewport);
        if (dst.w <= 0.0f || dst.h <= 0.0f) continue;

        const SDL_FRect source = {sprite.source.x, sprite.source.y,
                                  sprite.source.w, sprite.source.h};
        const SDL_FRect* sourcePtr = sprite.hasSource ? &source : nullptr;
        if (!SDL_RenderTexture(renderer, texture, sourcePtr, &dst)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
    }
    return true;
}

} // namespace Kotonoha
