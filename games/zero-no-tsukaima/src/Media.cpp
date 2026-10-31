#include "Media.hpp"

#include <algorithm>
#include <utility>

namespace ZeroNoTsukaima {

Picture::Picture(Picture &&other) noexcept
    : texture_(std::exchange(other.texture_, nullptr)), owned_(std::exchange(other.owned_, false)),
      scale_(std::exchange(other.scale_, 1.0f)) {}

Picture &Picture::operator=(Picture &&other) noexcept {
  if (this != &other) {
    Reset(nullptr);
    texture_ = std::exchange(other.texture_, nullptr);
    owned_ = std::exchange(other.owned_, false);
    scale_ = std::exchange(other.scale_, 1.0f);
  }
  return *this;
}

void Picture::Reset(SDL_Texture *texture, bool owned, float scale) {
  if (texture_ && owned_ && texture_ != texture) SDL_DestroyTexture(texture_);
  texture_ = texture;
  owned_ = texture != nullptr && owned;
  scale_ = scale > 0 ? scale : 1.0f;
}

float Picture::Width() const {
  float w = 0, h = 0;
  if (texture_) SDL_GetTextureSize(texture_, &w, &h);
  return w / scale_;
}

float Picture::Height() const {
  float w = 0, h = 0;
  if (texture_) SDL_GetTextureSize(texture_, &w, &h);
  return h / scale_;
}

bool Picture::Draw(SDL_Renderer *renderer, const SDL_FRect *src, const SDL_FRect *dst) {
  if (!texture_) return false;
  if (!src || scale_ == 1.0f) return SDL_RenderTexture(renderer, texture_, src, dst);
  const SDL_FRect scaled{src->x * scale_, src->y * scale_, src->w * scale_, src->h * scale_};
  return SDL_RenderTexture(renderer, texture_, &scaled, dst);
}

} // namespace ZeroNoTsukaima
