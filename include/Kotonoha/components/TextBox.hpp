#pragma once
#include <string>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace Kotonoha {

// Message window for on-screen visual-novel text: a translucent box with an
// optional speaker name and a word-wrapped body, plus a free caption line.
// Layout is expressed in the renderer's logical coordinates; glyphs are
// rasterized at the real output density so the text stays sharp on HiDPI
// displays (never upscaled from a low-resolution bitmap).
class TextBox {
public:
  struct Style {
    SDL_FRect box{20, 440, 760, 150}; // logical coordinates
    float fontSize = 26.0f;           // logical pixels
    float padding = 20.0f;
    SDL_Color background{0, 0, 0, 175};
    SDL_Color body{255, 255, 255, 255};
    SDL_Color speaker{255, 230, 140, 255};
    SDL_Color caption{200, 200, 255, 255};
  };

  TextBox() = default;
  ~TextBox();
  TextBox(const TextBox &) = delete;
  TextBox &operator=(const TextBox &) = delete;

  void SetFont(TTF_Font *font);
  void SetStyle(const Style &style);
  const Style &GetStyle() const { return style_; }

  // Empty body hides the window. Redundant calls are cheap.
  void SetMessage(const std::string &speaker, const std::string &body);
  void SetCaption(const std::string &caption);
  bool Visible() const { return !body_.empty(); }
  const std::string &Body() const { return body_; }
  const std::string &Speaker() const { return speaker_; }

  // Draws inside the current logical presentation.
  void Draw(SDL_Renderer *renderer);

private:
  struct Cached {
    SDL_Texture *texture = nullptr;
    float width = 0, height = 0; // logical size
  };

  void Release();
  Cached Rasterize(SDL_Renderer *renderer, const std::string &text,
                   SDL_Color color, float logicalWrap, float density) const;
  static void Blit(SDL_Renderer *renderer, const Cached &c, float x, float y);
  static float Density(SDL_Renderer *renderer);

  TTF_Font *font_ = nullptr;
  Style style_{};
  std::string speaker_, body_, caption_;
  Cached speakerTex_, bodyTex_, captionTex_;
  float rasterDensity_ = 0.0f;
  bool dirty_ = true;
};

} // namespace Kotonoha
