#include <Kotonoha/components/CanvasImage.hpp>

#include <utility>

namespace Kotonoha {

CanvasImage::CanvasImage(CanvasImage &&other) noexcept
    : source_(std::exchange(other.source_, nullptr)), owned_(std::exchange(other.owned_, false)),
      serial_(other.serial_), cache_(other.cache_) {
  SDL_zero(other.cache_);
}

CanvasImage &CanvasImage::operator=(CanvasImage &&other) noexcept {
  if (this != &other) {
    Reset(nullptr);
    source_ = std::exchange(other.source_, nullptr);
    owned_ = std::exchange(other.owned_, false);
    serial_ = other.serial_;
    cache_ = other.cache_;
    SDL_zero(other.cache_);
  }
  return *this;
}

void CanvasImage::Reset(SDL_Texture *texture, bool owned) {
  KotonohaUpscalingReleaseTexture(&cache_);
  if (owned_ && source_ != nullptr && source_ != texture) {
    SDL_DestroyTexture(source_);
  }
  source_ = texture;
  owned_ = owned && texture != nullptr;
  ++serial_;
}

SDL_Texture *CanvasImage::Display(SDL_Renderer *renderer) {
  if (source_ == nullptr || renderer == nullptr) {
    return source_;
  }
  if (!cache_.initialized || cache_.sourceSerial != serial_ ||
      cache_.settingsRevision != KotonohaUpscalingGetRevision()) {
    KotonohaUpscalingUpdateTexture(renderer, source_, serial_, &cache_, nullptr);
  }
  return cache_.output != nullptr ? cache_.output : source_;
}

bool CanvasImage::Draw(SDL_Renderer *renderer, const SDL_FRect *src, const SDL_FRect *dst) {
  SDL_Texture *shown = Display(renderer);
  if (shown == nullptr) {
    return false;
  }
  if (shown == source_) {
    return SDL_RenderTexture(renderer, source_, src, dst);
  }
  float r = 1, g = 1, b = 1, a = 1;
  SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
  SDL_GetTextureColorModFloat(source_, &r, &g, &b);
  SDL_GetTextureAlphaModFloat(source_, &a);
  SDL_GetTextureBlendMode(source_, &blend);
  SDL_SetTextureColorModFloat(shown, r, g, b);
  SDL_SetTextureAlphaModFloat(shown, a);
  SDL_SetTextureBlendMode(shown, blend);
  if (src == nullptr) {
    return SDL_RenderTexture(renderer, shown, nullptr, dst);
  }
  float sw = 0, sh = 0, ow = 0, oh = 0;
  SDL_GetTextureSize(source_, &sw, &sh);
  SDL_GetTextureSize(shown, &ow, &oh);
  const float fx = sw > 0 ? ow / sw : 1.0f, fy = sh > 0 ? oh / sh : 1.0f;
  const SDL_FRect scaled{src->x * fx, src->y * fy, src->w * fx, src->h * fy};
  return SDL_RenderTexture(renderer, shown, &scaled, dst);
}

} // namespace Kotonoha
