#include <Amagami/VitaSpriteRenderer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace Amagami::Vita {
namespace {
Uint8 modulate(double value) {
  return Uint8(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}
void require(bool ok) {
  if (!ok)
    throw std::runtime_error(std::string("Amagami Vita renderer: ") +
                             SDL_GetError());
}
} // namespace

VitaSpriteRenderer::VitaSpriteRenderer(SDL_Renderer *renderer,
                                       SurfaceFactory source)
    : renderer_(renderer), source_(std::move(source)) {
  if (!renderer_)
    throw std::invalid_argument("Amagami Vita renderer requires SDL_Renderer");
}

SDL_Texture *VitaSpriteRenderer::textureFor(uint32_t id, uint32_t variant) {
  const auto key = std::make_pair(id, variant);
  const auto cached = cache_.find(key);
  if (cached != cache_.end())
    return cached->second.get();
  if (!source_)
    return nullptr;
  Kotonoha::SurfacePtr surface = source_(id, variant);
  if (!surface)
    return nullptr;
  Kotonoha::TexturePtr texture(
      SDL_CreateTextureFromSurface(renderer_, surface.get()));
  if (!texture)
    throw std::runtime_error(std::string("Amagami Vita texture: ") +
                             SDL_GetError());
  require(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND));
  require(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR));
  auto *handle = texture.get();
  cache_.emplace(key, std::move(texture));
  return handle;
}

SDL_Texture *VitaSpriteRenderer::namedTextureFor(const std::string &name) {
  const auto it = namedCache_.find(name);
  if (it != namedCache_.end())
    return it->second.get();
  if (!namedSource_)
    return nullptr;
  Kotonoha::SurfacePtr surface = namedSource_(name);
  if (!surface)
    return nullptr;
  Kotonoha::TexturePtr texture(
      SDL_CreateTextureFromSurface(renderer_, surface.get()));
  if (!texture)
    throw std::runtime_error(std::string("Amagami Vita named texture: ") +
                             SDL_GetError());
  require(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND));
  require(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR));
  auto *handle = texture.get();
  namedCache_.emplace(name, std::move(texture));
  return handle;
}

SDL_Texture *VitaSpriteRenderer::fontTextureFor(unsigned font, uint16_t code,
                                                unsigned palette) {
  const auto key = std::make_tuple(font, code, palette);
  auto it = fontCache_.find(key);
  if (it != fontCache_.end())
    return it->second.get();
  if (!fontSource_)
    return nullptr;
  Kotonoha::SurfacePtr surface = fontSource_(font, code, palette);
  if (!surface)
    return nullptr;
  Kotonoha::TexturePtr texture(
      SDL_CreateTextureFromSurface(renderer_, surface.get()));
  if (!texture)
    throw std::runtime_error(std::string("Amagami Vita font texture: ") +
                             SDL_GetError());
  require(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND));
  require(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR));
  auto *handle = texture.get();
  fontCache_.emplace(key, std::move(texture));
  return handle;
}

void VitaSpriteRenderer::draw(const VitaVmBridge &bridge, bool clear) {
  if (clear) {
    require(SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255));
    require(SDL_RenderClear(renderer_));
  }
  std::vector<const SpriteState *> drawList;
  drawList.reserve(bridge.sprites().size());
  for (const auto &[uid, sprite] : bridge.sprites())
    drawList.push_back(&sprite);
  std::stable_sort(drawList.begin(), drawList.end(),
                   [](const SpriteState *a, const SpriteState *b) {
                     if (a->layer != b->layer)
                       return a->layer < b->layer;
                     return a->order < b->order;
                   });
  for (const SpriteState *sprite : drawList) {
    if (sprite->alpha <= 0 || sprite->zoom[0] <= 0 || sprite->zoom[1] <= 0 ||
        sprite->rect[2] <= 0 || sprite->rect[3] <= 0)
      continue;
    // The original Vita UI view has 2 screen pixels per SCF coordinate.
    // Raster pictures already contain those full-resolution source texels.
    const double cameraScale = sprite->worldView ? bridge.view().scale : 1.0;
    const double width = sprite->rect[2] * sprite->zoom[0] *
                         (sprite->hasImage ? 1.0 : 2.0) * cameraScale;
    const double height = sprite->rect[3] * sprite->zoom[1] *
                          (sprite->hasImage ? 1.0 : 2.0) * cameraScale;
    if (width <= 0 || height <= 0 || width > 100000 || height > 100000)
      continue;
    const SDL_FRect dst{
        float(480.0 + sprite->position[0] * 2.0 * cameraScale - width / 2.0),
        float(272.0 + sprite->position[1] * 2.0 * cameraScale +
              (sprite->worldView ? bridge.view().originY * 2.0 : 0.0) -
              height / 2.0),
        float(width), float(height)};
    // SDL reports clockwise-positive degrees, while the Vita SCF angle is
    // counterclockwise-positive in multiples of pi. Preserve the original
    // sprite centre as the rotation pivot even for atlas-clipped rectangles.
    const auto renderImage = [&](SDL_Texture *tex, const SDL_FRect *source,
                                 const SDL_FRect &destination) {
      if (std::abs(sprite->angle) < 1e-10) {
        require(Kotonoha::RenderUpscaledTexture(renderer_, tex, source,
                                                &destination));
      } else {
        const SDL_FPoint pivot{dst.x + dst.w / 2 - destination.x,
                               dst.y + dst.h / 2 - destination.y};
        require(Kotonoha::RenderUpscaledTextureRotated(
            renderer_, tex, source, &destination, -sprite->angle * 180.0,
            &pivot, SDL_FLIP_NONE));
      }
    };
    if (sprite->fontGlyph) {
      // Vita font atlas cells are 36x42 texels. FontChar coordinates are in
      // the fixed UI view (not the background's world camera). Palette
      // selectors 3/7/11 use the white palette tinted to 20% brightness.
      const int normalized = ((sprite->fontPalette % 4) + 4) % 4;
      SDL_Texture *text =
          fontTextureFor(sprite->fontAtlas, sprite->characterCode,
                         unsigned(normalized == 3 ? 0 : normalized));
      if (!text)
        continue;
      // The original FontChar emits a shadow first, shifted one script unit
      // right and down; it is black for palette 0..2, white for palette 3.
      if (sprite->fontPalette >= 0 && sprite->fontPalette < 4) {
        SDL_Texture *shadow =
            fontTextureFor(sprite->fontAtlas, sprite->characterCode, 0);
        if (shadow) {
          const Uint8 shade = sprite->fontPalette == 3 ? 255 : 0;
          require(SDL_SetTextureAlphaMod(shadow, modulate(sprite->alpha)));
          require(SDL_SetTextureColorMod(shadow, shade, shade, shade));
          SDL_FRect shadowRect = dst;
          shadowRect.x += 2.0f;
          shadowRect.y += 2.0f;
          renderImage(shadow, nullptr, shadowRect);
        }
      }
      const double tint = normalized == 3 ? 0.2 : 1.0;
      require(SDL_SetTextureAlphaMod(text, modulate(sprite->alpha)));
      require(SDL_SetTextureColorMod(text, modulate(sprite->light[0] * tint),
                                     modulate(sprite->light[1] * tint),
                                     modulate(sprite->light[2] * tint)));
      renderImage(text, nullptr, dst);
      continue;
    }
    if (!sprite->hasImage) {
      if (sprite->texture != 0 || !sprite->assetName.empty())
        continue; // Unresolved resources are not black bars.
      // Texture zero is the opaque black screen/side-bar rectangle. It must
      // occlude lower layers; it is not a missing white placeholder.
      require(SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND));
      require(
          SDL_SetRenderDrawColor(renderer_, 0, 0, 0, modulate(sprite->alpha)));
      if (std::abs(sprite->angle) < 1e-10) {
        require(SDL_RenderFillRect(renderer_, &dst));
      } else {
        if (!solidBlack_) {
          Kotonoha::SurfacePtr black(
              SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
          if (!black)
            throw std::runtime_error(SDL_GetError());
          const Uint32 pixel = SDL_MapSurfaceRGBA(black.get(), 0, 0, 0, 255);
          require(SDL_FillSurfaceRect(black.get(), nullptr, pixel));
          solidBlack_.reset(
              SDL_CreateTextureFromSurface(renderer_, black.get()));
          if (!solidBlack_)
            throw std::runtime_error(SDL_GetError());
          require(
              SDL_SetTextureBlendMode(solidBlack_.get(), SDL_BLENDMODE_BLEND));
        }
        require(
            SDL_SetTextureAlphaMod(solidBlack_.get(), modulate(sprite->alpha)));
        renderImage(solidBlack_.get(), nullptr, dst);
      }
      continue;
    }
    SDL_Texture *texture = sprite->assetName.empty()
                               ? textureFor(sprite->texture, sprite->variant)
                               : namedTextureFor(sprite->assetName);
    if (!texture)
      continue; // A missing texture can be diagnosed separately.
    float textureW = 0, textureH = 0;
    require(SDL_GetTextureSize(texture, &textureW, &textureH));
    SDL_FRect src{float(sprite->rect[0]), float(sprite->rect[1]),
                  float(sprite->rect[2]), float(sprite->rect[3])};
    // Source coordinates may be negative or exceed the atlas. Crop both
    // source and destination proportionally rather than stretching the part
    // that was inside the texture to the full requested destination.
    const float left = std::max(0.0f, -src.x);
    const float top = std::max(0.0f, -src.y);
    const float right = std::max(0.0f, src.x + src.w - textureW);
    const float bottom = std::max(0.0f, src.y + src.h - textureH);
    if (left + right >= src.w || top + bottom >= src.h)
      continue;
    SDL_FRect clipped = dst;
    clipped.x += dst.w * left / src.w;
    clipped.y += dst.h * top / src.h;
    clipped.w *= (src.w - left - right) / src.w;
    clipped.h *= (src.h - top - bottom) / src.h;
    src.x += left;
    src.y += top;
    src.w -= left + right;
    src.h -= top + bottom;
    require(SDL_SetTextureAlphaMod(texture, modulate(sprite->alpha)));
    require(SDL_SetTextureColorMod(texture, modulate(sprite->light[0]),
                                   modulate(sprite->light[1]),
                                   modulate(sprite->light[2])));
    renderImage(texture, &src, clipped);
  }
}
} // namespace Amagami::Vita
