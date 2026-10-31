#pragma once

#include <Kotonoha/media/PcmPlayer.hpp>

#include <SDL3/SDL.h>

namespace ZeroNoTsukaima {

// A texture the game draws; owns it unless it comes from the engine asset cache.
class Picture {
public:
  Picture() = default;
  explicit Picture(SDL_Texture *texture, bool owned = true) { Reset(texture, owned); }
  ~Picture() { Reset(nullptr); }
  Picture(const Picture &) = delete;
  Picture &operator=(const Picture &) = delete;
  Picture(Picture &&other) noexcept;
  Picture &operator=(Picture &&other) noexcept;

  // `scale` is texture pixels per logical pixel (the upscaling factor): sizes
  // and source rects stay in the game's 640x448 units.
  void Reset(SDL_Texture *texture, bool owned = true, float scale = 1.0f);
  void Touch() {}
  SDL_Texture *Source() const { return texture_; }
  explicit operator bool() const { return texture_ != nullptr; }
  float Width() const;
  float Height() const;
  bool Draw(SDL_Renderer *renderer, const SDL_FRect *src, const SDL_FRect *dst);

private:
  SDL_Texture *texture_ = nullptr;
  bool owned_ = false;
  float scale_ = 1.0f;
};

using PcmPlayer = Kotonoha::PcmPlayer;

} // namespace ZeroNoTsukaima
