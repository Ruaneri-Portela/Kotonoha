#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>

#include <algorithm>
#include <filesystem>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

extern "C" {
#include <Kotonoha/renders/ImageRender.h>
}

namespace Kotonoha {

SchoolDaysBackLogPresenter::~SchoolDaysBackLogPresenter() { Close(); }

bool SchoolDaysBackLogPresenter::Fail(std::string* error, const char* text) {
    if (error != nullptr) {
        *error = text != nullptr ? text : "School Days BackLog presenter error";
    }
    return false;
}

bool SchoolDaysBackLogPresenter::Open(
    SDL_Renderer* renderer, const std::string& assetsRoot,
    SchoolDaysBackLogModel::Orientation orientation, bool useEnglish,
    const std::string& fontDataLogicalPath, std::string* error) {
    Close();
    if (renderer == nullptr) return Fail(error, "School Days BackLog renderer is null");

    const auto baseAsset = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysBackLogModel::BaseRelativePath(orientation));
    const auto chipAsset = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), SchoolDaysBackLogModel::ChipRelativePath(orientation));
    if (!baseAsset.present || baseAsset.collision || !chipAsset.present ||
        chipAsset.collision) {
        return Fail(error, "School Days BackLog PNG/CHIP asset could not be resolved");
    }

    base_ = KotonohaImageCreateTexture(renderer, baseAsset.physical.c_str(), -1, -1);
    chip_ = KotonohaImageCreateTexture(renderer, chipAsset.physical.c_str(), -1, -1);
    if (base_ == nullptr || chip_ == nullptr) {
        Close();
        return Fail(error, "School Days BackLog texture load failed");
    }
    namespace fs = std::filesystem;
    fs::path fontLogical(fontDataLogicalPath);
    if (fontLogical.empty() || fontLogical.is_absolute()) {
        Close();
        return Fail(error, "School Days BackLog FONTDATA logical path is invalid");
    }
    if (useEnglish) {
        const std::string extension = fontLogical.extension().string();
        const std::string stem = fontLogical.stem().string();
        fontLogical.replace_filename(stem + "_eng" + extension);
    }
    const auto fontAsset = ResolveSchoolDaysAssetPath(
        assetsRoot.c_str(), fontLogical.generic_string());
    if (!fontAsset.present || fontAsset.collision ||
        !font_.LoadFile(fontAsset.physical, error)) {
        Close();
        return Fail(error, "School Days BackLog FONTDATA asset could not be loaded");
    }
    text_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, 2048, 1024);
    if (text_ == nullptr || !SDL_SetTextureBlendMode(text_, SDL_BLENDMODE_BLEND)) {
        Close();
        return Fail(error, "School Days BackLog FONTDATA texture setup failed");
    }
    textPixels_.assign(static_cast<size_t>(2048) * 1024u, 0u);
    SDL_SetTextureBlendMode(base_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(chip_, SDL_BLENDMODE_BLEND);
    orientation_ = orientation;
    useEnglish_ = useEnglish;
    opened_ = true;
    return true;
}

void SchoolDaysBackLogPresenter::Close() {
    if (base_ != nullptr) SDL_DestroyTexture(base_);
    if (chip_ != nullptr) SDL_DestroyTexture(chip_);
    if (text_ != nullptr) SDL_DestroyTexture(text_);
    base_ = nullptr;
    chip_ = nullptr;
    text_ = nullptr;
    font_.Clear();
    textPixels_.clear();
    useEnglish_ = false;
    opened_ = false;
}

bool SchoolDaysBackLogPresenter::Render(
    SDL_Renderer* renderer, const SchoolDaysBackLogModel& model,
    std::string* error) {
    if (!opened_ || renderer == nullptr || base_ == nullptr || chip_ == nullptr ||
        text_ == nullptr || !font_.IsLoaded() ||
        !model.IsOpen() || model.CurrentOrientation() != orientation_) {
        return Fail(error, "School Days BackLog presenter is not open for this model");
    }

    int outputWidth = 0;
    int outputHeight = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    const auto layout = SchoolDaysBackLogRenderPlan::LayoutFor(model.CurrentVariant());
    if (outputWidth <= 0 || outputHeight <= 0 || layout.logicalWidth <= 0 ||
        layout.logicalHeight <= 0) {
        return Fail(error, "School Days BackLog viewport is invalid");
    }
    const float viewportScale =
        std::min(static_cast<float>(outputWidth) / layout.logicalWidth,
                 static_cast<float>(outputHeight) / layout.logicalHeight);
    const float viewportX =
        (static_cast<float>(outputWidth) - layout.logicalWidth * viewportScale) * 0.5f;
    const float viewportY =
        (static_cast<float>(outputHeight) - layout.logicalHeight * viewportScale) * 0.5f;

    const auto physical = [&](const SchoolDaysBackLogRenderPlan::Rect& r) {
        return SDL_FRect{viewportX + r.x * viewportScale,
                         viewportY + r.y * viewportScale,
                         r.w * viewportScale, r.h * viewportScale};
    };

    const auto sprites = SchoolDaysBackLogRenderPlan::BuildSkin(model);
    for (const auto& sprite : sprites) {
        SDL_Texture* texture = sprite.texture == SchoolDaysBackLogRenderPlan::Texture::Base
                                   ? base_
                                   : chip_;
        const SDL_FRect destination = physical(sprite.destination);
        SDL_FRect source{};
        const SDL_FRect* sourcePtr = nullptr;
        if (sprite.hasSource) {
            source = {sprite.source.x, sprite.source.y, sprite.source.w, sprite.source.h};
            sourcePtr = &source;
        }
        if (!SDL_RenderTexture(renderer, texture, sourcePtr, &destination)) {
            if (error != nullptr) *error = SDL_GetError();
            return false;
        }
    }

    // Native BackLogView still draws the menu skin when the transient message
    // collection is empty; there is simply no FONTDATA pass to submit.
    if (model.RawCountSnapshot() == 0u || model.CurrentRawIndex() < 0) return true;

    if (!useEnglish_ || orientation_ != SchoolDaysBackLogModel::Orientation::Horizon) {
        return Fail(error, "School Days BackLog native text path is not authenticated for this profile");
    }
    const auto textPlan =
        SchoolDaysBackLogRenderPlan::BuildRetailHorizonEnglish(model);
    if (!textPlan.valid) {
        return Fail(error, textPlan.unsupportedMarkup
                               ? "School Days BackLog contains unsupported native markup"
                               : "School Days BackLog native text plan is invalid");
    }
    std::fill(textPixels_.begin(), textPixels_.end(), 0u);
    SchoolDaysFontData::Glyph glyph;
    for (const auto& block : textPlan.blocks) {
        for (const auto& placement : block.glyphs) {
            if (!font_.DecodeGlyph(placement.codepoint, &glyph, error)) return false;
            for (int gy = 0; gy < SchoolDaysFontData::kGlyphHeight; ++gy) {
                const int y = placement.y + gy;
                if (y < 0 || y >= 1024) continue;
                for (int gx = 0; gx < SchoolDaysFontData::kGlyphWidth; ++gx) {
                    const int x = placement.x + gx;
                    if (x < 0 || x >= 2048) continue;
                    const size_t source = static_cast<size_t>(gy) *
                                              SchoolDaysFontData::kGlyphWidth +
                                          static_cast<size_t>(gx);
                    const uint8_t alpha = glyph.alpha[source];
                    const uint8_t gray = glyph.gray[source];
                    const uint32_t packed = (static_cast<uint32_t>(alpha) << 24u) |
                                            (static_cast<uint32_t>(gray) << 16u) |
                                            (static_cast<uint32_t>(gray) << 8u) |
                                            static_cast<uint32_t>(gray);
                    uint32_t& destination = textPixels_[static_cast<size_t>(y) * 2048u +
                                                        static_cast<size_t>(x)];
                    if (destination < packed) destination = packed;
                }
            }
        }
    }
    if (!SDL_UpdateTexture(text_, nullptr, textPixels_.data(),
                           2048 * static_cast<int>(sizeof(uint32_t)))) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    const auto textSource = SchoolDaysBackLogRenderPlan::TextSurfaceSource();
    const auto textDestination =
        SchoolDaysBackLogRenderPlan::TextSurfaceDestination(model.CurrentVariant());
    const SDL_FRect source{textSource.x, textSource.y, textSource.w, textSource.h};
    const SDL_FRect destination = physical(textDestination);
    if (!SDL_RenderTexture(renderer, text_, &source, &destination)) {
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    return true;
}

} // namespace Kotonoha
