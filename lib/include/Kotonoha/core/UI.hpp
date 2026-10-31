#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <unordered_map>

namespace Kotonoha {

std::size_t utf8Prefix(const std::string &text, std::size_t characters);
void eraseLastCharacter(std::string &text);

class UI {
  struct CachedText {
    TexturePtr texture;
    float width = 0, height = 0;
  };
  SDLHandle<TTF_Font, TTF_CloseFont> font;
  std::unordered_map<std::string, CachedText> cache;
  float density = 1;

public:
  explicit UI(const std::string &fontPath = {});
  ~UI();
  UI(const UI &) = delete;
  UI &operator=(const UI &) = delete;
  void setPixelDensity(float value);
  float pixelDensity() const { return density; }
  float rasterFontSize() const { return TTF_GetFontSize(font.get()); }
  static SDL_FPoint viewSize(RendererRef renderer);
  float text(RendererRef renderer, const std::string &value, float x, float y,
             float width, SDL_Color color = {240, 240, 245, 255});
  float textHeight(RendererRef renderer, const std::string &value, float width);
  SDL_FPoint textSize(const std::string &value, float width) const;
  void panel(RendererRef renderer, SDL_FRect rect,
             SDL_Color color = {20, 23, 36, 235});
  void button(RendererRef renderer, SDL_FRect rect, const std::string &label,
              bool selected = false);
  static SDL_FRect centered(float width, float height);
  static bool clicked(EventRef event, SDL_FRect rect);
  static constexpr float width = 1280, height = 720;

  // A content area (e.g. the design space or a game's native resolution)
  // scaled to fit and centred inside a viewport.
  struct Frame {
    float scale = 1, x = 0, y = 0, width = 0, height = 0;
    bool identity() const { return scale == 1 && x == 0 && y == 0; }
  };
  static Frame fit(SDL_FPoint content, SDL_Point viewport);
  static Frame designFrame(SDL_Point viewport) {
    return fit({width, height}, viewport);
  }
  // Maps pointer coordinates of an event from viewport to frame units.
  static SDL_Event toFrame(const SDL_Event &event, Frame frame);
  // Draws in frame units (clipped to the content area) until destroyed.
  class FrameScope {
    SDL_Renderer *renderer = nullptr;

  public:
    FrameScope(RendererRef renderer, Frame frame);
    ~FrameScope();
    FrameScope(const FrameScope &) = delete;
    FrameScope &operator=(const FrameScope &) = delete;
  };
};

} // namespace Kotonoha
