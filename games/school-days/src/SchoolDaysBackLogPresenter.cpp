#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>

#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

namespace Kotonoha {

namespace {

std::string Utf16ToUtf8BackLog(const std::u16string &text) {
  return Kotonoha::Utf16ToUtf8(text);
}

// The bundled UI font and common Windows UI/CJK fonts do not necessarily
// contain supplementary emoji. Only borrow a font from the Windows install;
// a missing or unsupported system font is NOT a BackLog initialization error.
TTF_Font *OpenWindowsEmojiFont(float pointSize) {
#if defined(_WIN32) && !defined(__ANDROID__)
  const char *windowsRoot = SDL_getenv("WINDIR");
  if (windowsRoot == nullptr || *windowsRoot == '\0')
    windowsRoot = SDL_getenv("SystemRoot");
  if (windowsRoot == nullptr || *windowsRoot == '\0' || pointSize <= 0.0f)
    return nullptr;

  const auto path =
      std::filesystem::u8path(windowsRoot) / "Fonts" / "seguiemj.ttf";
  TTF_Font *candidate = TTF_OpenFont(path.u8string().c_str(), pointSize);
  if (candidate == nullptr)
    return nullptr;
  if (!TTF_FontHasGlyph(candidate, 0x1F642u)) {
    TTF_CloseFont(candidate);
    return nullptr;
  }
  return candidate;
#else
  (void)pointSize;
  return nullptr;
#endif
}

} // namespace

SchoolDaysBackLogPresenter::~SchoolDaysBackLogPresenter() { Close(); }

bool SchoolDaysBackLogPresenter::Fail(std::string *error, const char *text) {
  if (error != nullptr) {
    *error = text != nullptr ? text : "School Days BackLog presenter error";
  }
  return false;
}

bool SchoolDaysBackLogPresenter::Open(
    SDL_Renderer *renderer, const std::string &assetsRoot,
    SchoolDaysBackLogModel::Orientation orientation, bool useEnglish,
    TTF_Font *primaryFont, TTF_Font *japaneseFallback, std::string *error) {
  Close();
  if (renderer == nullptr)
    return Fail(error, "School Days BackLog renderer is null");

  const auto baseAsset = ResolveSchoolDaysAssetPath(
      assetsRoot.c_str(),
      SchoolDaysBackLogModel::BaseRelativePath(orientation));
  const auto chipAsset = ResolveSchoolDaysAssetPath(
      assetsRoot.c_str(),
      SchoolDaysBackLogModel::ChipRelativePath(orientation));
  if (!baseAsset.present || baseAsset.collision || !chipAsset.present ||
      chipAsset.collision) {
    return Fail(error,
                "School Days BackLog PNG/CHIP asset could not be resolved");
  }

  const auto load = [&](const std::string &physical) -> SDL_Texture * {
    if (assets_ == nullptr)
      return nullptr;
    try {
      const SurfacePtr surface = assets_->loadSurface(physical);
      return SDL_CreateTextureFromSurface(renderer, surface.get());
    } catch (const std::exception &failure) {
      SDL_SetError("%s", failure.what());
      return nullptr;
    }
  };
  base_ = load(baseAsset.physical);
  chip_ = load(chipAsset.physical);
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
  // Preserve the requested CJK fallback first. Emoji is the last resort,
  // added only when no earlier font in the chain can draw the smiley.
  if (!TTF_FontHasGlyph(font_, 0x1F642u)) {
    TTF_Font *candidate = OpenWindowsEmojiFont(TTF_GetFontSize(font_));
    if (candidate != nullptr) {
      if (TTF_AddFallbackFont(font_, candidate))
        emojiFallbackFont_ = candidate;
      else
        TTF_CloseFont(candidate);
    }
  }
  text_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STREAMING, 2048, 1024);
  if (text_ == nullptr ||
      !SDL_SetTextureBlendMode(text_, SDL_BLENDMODE_BLEND)) {
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
  if (base_ != nullptr)
    SDL_DestroyTexture(base_);
  if (chip_ != nullptr)
    SDL_DestroyTexture(chip_);
  if (text_ != nullptr)
    SDL_DestroyTexture(text_);
  base_ = nullptr;
  chip_ = nullptr;
  text_ = nullptr;
  if (font_ != nullptr) {
    TTF_ClearFallbackFonts(font_);
    TTF_CloseFont(font_);
  }
  if (fallbackFont_ != nullptr)
    TTF_CloseFont(fallbackFont_);
  if (emojiFallbackFont_ != nullptr)
    TTF_CloseFont(emojiFallbackFont_);
  font_ = nullptr;
  fallbackFont_ = nullptr;
  emojiFallbackFont_ = nullptr;
  useEnglish_ = false;
  opened_ = false;
}

bool SchoolDaysBackLogPresenter::Render(SDL_Renderer *renderer,
                                        const SchoolDaysBackLogModel &model,
                                        std::string *error) {
  if (!opened_ || renderer == nullptr || base_ == nullptr || chip_ == nullptr ||
      text_ == nullptr || font_ == nullptr || !model.IsOpen() ||
      model.CurrentOrientation() != orientation_) {
    return Fail(error,
                "School Days BackLog presenter is not open for this model");
  }

  const int outputWidth = area_.x;
  const int outputHeight = area_.y;
  const auto layout =
      SchoolDaysBackLogRenderPlan::LayoutFor(model.CurrentVariant());
  if (outputWidth <= 0 || outputHeight <= 0 || layout.logicalWidth <= 0 ||
      layout.logicalHeight <= 0) {
    return Fail(error, "School Days BackLog viewport is invalid");
  }
  const float viewportScale =
      std::min(static_cast<float>(outputWidth) / layout.logicalWidth,
               static_cast<float>(outputHeight) / layout.logicalHeight);
  const float viewportX =
      (static_cast<float>(outputWidth) - layout.logicalWidth * viewportScale) *
      0.5f;
  const float viewportY = (static_cast<float>(outputHeight) -
                           layout.logicalHeight * viewportScale) *
                          0.5f;

  const auto physical = [&](const SchoolDaysBackLogRenderPlan::Rect &r) {
    return SDL_FRect{viewportX + r.x * viewportScale,
                     viewportY + r.y * viewportScale, r.w * viewportScale,
                     r.h * viewportScale};
  };

  const auto sprites = SchoolDaysBackLogRenderPlan::BuildSkin(model);
  for (const auto &sprite : sprites) {
    SDL_Texture *texture =
        sprite.texture == SchoolDaysBackLogRenderPlan::Texture::Base ? base_
                                                                     : chip_;
    const SDL_FRect destination = physical(sprite.destination);
    SDL_FRect source{};
    const SDL_FRect *sourcePtr = nullptr;
    if (sprite.hasSource) {
      source = {sprite.source.x, sprite.source.y, sprite.source.w,
                sprite.source.h};
      sourcePtr = &source;
    }
    if (!SDL_RenderTexture(renderer, texture, sourcePtr, &destination)) {
      if (error != nullptr)
        *error = SDL_GetError();
      return false;
    }
  }

  // Native BackLogView still draws the menu skin when the transient message
  // collection is empty; there is simply no FONTDATA pass to submit.
  if (model.RawCountSnapshot() == 0u || model.CurrentRawIndex() < 0)
    return true;

  if (!useEnglish_ ||
      orientation_ != SchoolDaysBackLogModel::Orientation::Horizon) {
    return Fail(error, "School Days BackLog text layout is not authenticated "
                       "for this profile");
  }
  const auto textPlan =
      SchoolDaysBackLogRenderPlan::BuildRetailHorizonEnglish(model);
  if (!textPlan.valid) {
    return Fail(error,
                textPlan.unsupportedMarkup
                    ? "School Days BackLog contains unsupported native markup"
                    : "School Days BackLog native text plan is invalid");
  }
  SDL_Surface *scratch = SDL_CreateSurface(2048, 1024, SDL_PIXELFORMAT_RGBA32);
  if (scratch == nullptr || !SDL_FillSurfaceRect(scratch, nullptr, 0u)) {
    if (scratch != nullptr)
      SDL_DestroySurface(scratch);
    return Fail(error, "School Days BackLog text surface setup failed");
  }
  const auto clip = SchoolDaysBackLogRenderPlan::TextSurfaceClip();
  const SDL_Rect safeArea{static_cast<int>(clip.x), static_cast<int>(clip.y),
                          static_cast<int>(clip.w), static_cast<int>(clip.h)};
  if (!SDL_SetSurfaceClipRect(scratch, &safeArea)) {
    SDL_DestroySurface(scratch);
    return Fail(error, "School Days BackLog text clipping setup failed");
  }
  for (const auto &block : textPlan.blocks) {
    // Build one shaped TTF run per authenticated FONTDATA row. The run is
    // then fitted proportionally within its allowed width and line height,
    // preserving the aspect ratio of accented and supplementary glyphs.
    std::map<int, std::vector<SchoolDaysBackLogRenderPlan::NativeGlyph>> rows;
    for (const auto &placement : block.glyphs)
      rows[placement.y].push_back(placement);
    for (const auto &[y, placements] : rows) {
      if (placements.empty())
        continue;
      std::u16string text;
      text.reserve(placements.size());
      for (const auto &placement : placements)
        text.push_back(static_cast<char16_t>(placement.codepoint));
      const std::string utf8 = Utf16ToUtf8BackLog(text);
      if (utf8.empty())
        continue;

      SDL_Surface *rendered = SchoolDaysSubtitleTypography::Render(
          font_, utf8.c_str(), utf8.size());
      if (rendered == nullptr) {
        SDL_DestroySurface(scratch);
        return Fail(error, "School Days BackLog TTF rasterization failed");
      }
      const auto &first = placements.front();
      const auto &last = placements.back();
      const int span = std::max(
          1, last.x +
                 SchoolDaysBackLogRenderPlan::RetailEnglishGlyphAdvance(
                     last.codepoint) -
                 first.x);
      const auto size = SchoolDaysBackLogRenderPlan::FitTextRaster(
          span, rendered->w, rendered->h);
      // A smaller retail layout may shrink TTF text, never enlarge it.
      SDL_Rect destination{first.x, y, std::min(size.w, rendered->w),
                           std::min(size.h, rendered->h)};
      const bool blitted = SDL_BlitSurfaceScaled(
          rendered, nullptr, scratch, &destination, SDL_SCALEMODE_LINEAR);
      SDL_DestroySurface(rendered);
      if (!blitted) {
        SDL_DestroySurface(scratch);
        return Fail(error, "School Days BackLog TTF composition failed");
      }
    }
  }
  if (!MarkTTFTextRaster(*text_) ||
      !SDL_UpdateTexture(text_, nullptr, scratch->pixels, scratch->pitch)) {
    SDL_DestroySurface(scratch);
    if (error != nullptr)
      *error = SDL_GetError();
    return false;
  }
  SDL_DestroySurface(scratch);
  const auto textSource = SchoolDaysBackLogRenderPlan::TextSurfaceSource();
  const auto textDestination =
      SchoolDaysBackLogRenderPlan::TextSurfaceDestination(
          model.CurrentVariant());
  const SDL_FRect source{textSource.x, textSource.y, textSource.w,
                         textSource.h};
  const SDL_FRect destination = physical(textDestination);
  if (!SDL_RenderTexture(renderer, text_, &source, &destination)) {
    if (error != nullptr)
      *error = SDL_GetError();
    return false;
  }
  return true;
}

} // namespace Kotonoha
