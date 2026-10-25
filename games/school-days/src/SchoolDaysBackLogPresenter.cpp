#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>

extern "C" {
#include <Kotonoha/renders/ImageRender.h>
}

namespace Kotonoha {

namespace {

std::string Utf16ToUtf8BackLog(const std::u16string& text) {
    if (text.empty()) return {};
    char* converted = SDL_iconv_string(
        "UTF-8", "UTF-16LE", reinterpret_cast<const char*>(text.c_str()),
        (text.size() + 1u) * sizeof(char16_t));
    if (converted == nullptr) return {};
    std::string result(converted);
    SDL_free(converted);
    return result;
}

} // namespace

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
    TTF_Font* primaryFont, TTF_Font* japaneseFallback, std::string* error) {
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
    if (primaryFont == nullptr) {
        Close();
        return Fail(error, "School Days BackLog UI font is unavailable");
    }
    font_ = TTF_CopyFont(primaryFont);
    if (font_ == nullptr) {
        Close();
        return Fail(error, "School Days BackLog UI font copy failed");
    }
    if (japaneseFallback != nullptr && japaneseFallback != primaryFont) {
        fallbackFont_ = TTF_CopyFont(japaneseFallback);
        if (fallbackFont_ == nullptr) {
            Close();
            return Fail(error, "School Days BackLog Japanese fallback copy failed");
        }
        const float primarySize = TTF_GetFontSize(font_);
        if (primarySize > 0.0f && !TTF_SetFontSize(fallbackFont_, primarySize)) {
            Close();
            return Fail(error, "School Days BackLog fallback size setup failed");
        }
        if (!TTF_AddFallbackFont(font_, fallbackFont_)) {
            Close();
            return Fail(error, "School Days BackLog fallback chain setup failed");
        }
    }
    text_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, 2048, 1024);
    if (text_ == nullptr || !SDL_SetTextureBlendMode(text_, SDL_BLENDMODE_BLEND)) {
        Close();
        return Fail(error, "School Days BackLog text texture setup failed");
    }
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
    if (font_ != nullptr) {
        TTF_ClearFallbackFonts(font_);
        TTF_CloseFont(font_);
    }
    if (fallbackFont_ != nullptr) TTF_CloseFont(fallbackFont_);
    font_ = nullptr;
    fallbackFont_ = nullptr;
    useEnglish_ = false;
    opened_ = false;
}

bool SchoolDaysBackLogPresenter::Render(
    SDL_Renderer* renderer, const SchoolDaysBackLogModel& model,
    std::string* error) {
    if (!opened_ || renderer == nullptr || base_ == nullptr || chip_ == nullptr ||
        text_ == nullptr || font_ == nullptr ||
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
        return Fail(error, "School Days BackLog text layout is not authenticated for this profile");
    }
    const auto textPlan =
        SchoolDaysBackLogRenderPlan::BuildRetailHorizonEnglish(model);
    if (!textPlan.valid) {
        return Fail(error, textPlan.unsupportedMarkup
                               ? "School Days BackLog contains unsupported native markup"
                               : "School Days BackLog native text plan is invalid");
    }
    SDL_Surface* scratch =
        SDL_CreateSurface(2048, 1024, SDL_PIXELFORMAT_RGBA32);
    if (scratch == nullptr || !SDL_FillSurfaceRect(scratch, nullptr, 0u)) {
        if (scratch != nullptr) SDL_DestroySurface(scratch);
        return Fail(error, "School Days BackLog text surface setup failed");
    }
    for (const auto& block : textPlan.blocks) {
        // Build one shaped TTF run per authenticated FONTDATA row. The run is
        // then fitted back into the exact native advance span, so wrapping,
        // centering and scroll geometry stay unchanged while glyph coverage
        // comes from the engine's UI font chain.
        std::map<int, std::vector<SchoolDaysBackLogRenderPlan::NativeGlyph>> rows;
        for (const auto& placement : block.glyphs) rows[placement.y].push_back(placement);
        for (const auto& [y, placements] : rows) {
            if (placements.empty()) continue;
            std::u16string text;
            text.reserve(placements.size());
            for (const auto& placement : placements)
                text.push_back(static_cast<char16_t>(placement.codepoint));
            const std::string utf8 = Utf16ToUtf8BackLog(text);
            if (utf8.empty()) continue;

            SDL_Surface* rendered = SchoolDaysSubtitleTypography::Render(
                font_, utf8.c_str(), utf8.size());
            if (rendered == nullptr) {
                SDL_DestroySurface(scratch);
                return Fail(error, "School Days BackLog TTF rasterization failed");
            }
            const auto& first = placements.front();
            const auto& last = placements.back();
            const int span = std::max(
                1, last.x +
                       SchoolDaysBackLogRenderPlan::RetailEnglishGlyphAdvance(
                           last.codepoint) -
                       first.x);
            SDL_Rect destination{first.x, y, span, 48};
            const bool blitted = SDL_BlitSurfaceScaled(
                rendered, nullptr, scratch, &destination, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(rendered);
            if (!blitted) {
                SDL_DestroySurface(scratch);
                return Fail(error, "School Days BackLog TTF composition failed");
            }
        }
    }
    if (!SDL_UpdateTexture(text_, nullptr, scratch->pixels, scratch->pitch)) {
        SDL_DestroySurface(scratch);
        if (error != nullptr) *error = SDL_GetError();
        return false;
    }
    SDL_DestroySurface(scratch);
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
