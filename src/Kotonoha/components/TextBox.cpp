#include <Kotonoha/components/TextBox.hpp>

#include <Kotonoha/components/Panel.hpp>

namespace Kotonoha {

TextBox::~TextBox() { Release(); }

void TextBox::Release() {
  for (Cached *c : {&speakerTex_, &bodyTex_, &captionTex_}) {
    if (c->texture != nullptr) SDL_DestroyTexture(c->texture);
    *c = Cached{};
  }
  dirty_ = true;
}

void TextBox::SetFont(TTF_Font *font) {
  if (font == font_) return;
  font_ = font;
  Release();
}

void TextBox::SetStyle(const Style &style) {
  style_ = style;
  Release();
}

void TextBox::SetMessage(const std::string &speaker, const std::string &body) {
  if (speaker == speaker_ && body == body_) return;
  speaker_ = speaker;
  body_ = body;
  Release();
}

void TextBox::SetCaption(const std::string &caption) {
  if (caption == caption_) return;
  caption_ = caption;
  Release();
}

float TextBox::Density(SDL_Renderer *renderer) {
  int outW = 0, outH = 0, logW = 0, logH = 0;
  SDL_RendererLogicalPresentation mode;
  if (!SDL_GetRenderOutputSize(renderer, &outW, &outH)) return 1.0f;
  if (!SDL_GetRenderLogicalPresentation(renderer, &logW, &logH, &mode) ||
      logW <= 0 || logH <= 0 || mode == SDL_LOGICAL_PRESENTATION_DISABLED) {
    return 1.0f;
  }
  const float sx = static_cast<float>(outW) / static_cast<float>(logW);
  const float sy = static_cast<float>(outH) / static_cast<float>(logH);
  return sx < sy ? sx : sy;
}

TextBox::Cached TextBox::Rasterize(SDL_Renderer *renderer,
                                   const std::string &text, SDL_Color color,
                                   float logicalWrap, float density) const {
  Cached out;
  if (font_ == nullptr || text.empty()) return out;
  TTF_SetFontSize(font_, style_.fontSize * density);
  const int wrap = logicalWrap > 0 ? static_cast<int>(logicalWrap * density) : 0;
  const int defaultSkip = TTF_GetFontLineSkip(font_);
  if (style_.lineHeight > 0) TTF_SetFontLineSkip(font_, static_cast<int>(style_.lineHeight * density + 0.5f));
  SDL_Surface *surface =
      wrap > 0 ? TTF_RenderText_Blended_Wrapped(font_, text.c_str(), 0, color, wrap)
               : TTF_RenderText_Blended(font_, text.c_str(), 0, color);
  if (style_.lineHeight > 0) TTF_SetFontLineSkip(font_, defaultSkip);
  if (surface == nullptr) return out;
  out.texture = SDL_CreateTextureFromSurface(renderer, surface);
  out.width = static_cast<float>(surface->w) / density;
  out.height = static_cast<float>(surface->h) / density;
  SDL_DestroySurface(surface);
  if (out.texture != nullptr)
    SDL_SetTextureScaleMode(out.texture, SDL_SCALEMODE_LINEAR);
  return out;
}

void TextBox::Blit(SDL_Renderer *renderer, const Cached &c, float x, float y) {
  if (c.texture == nullptr) return;
  SDL_FRect r{x, y, c.width, c.height};
  SDL_RenderTexture(renderer, c.texture, nullptr, &r);
}

void TextBox::Draw(SDL_Renderer *renderer) {
  if (renderer == nullptr || font_ == nullptr) return;
  const float density = Density(renderer);
  if (density != rasterDensity_) {
    Release();
    rasterDensity_ = density;
  }
  if (dirty_) {
    const float wrap = style_.box.w - style_.padding * 2;
    speakerTex_ = Rasterize(renderer, speaker_, style_.speaker, 0, density);
    bodyTex_ = Rasterize(renderer, body_, style_.body, wrap, density);
    captionTex_ = Rasterize(renderer, caption_, style_.caption, 0, density);
    dirty_ = false;
  }

  SDL_BlendMode oldMode = SDL_BLENDMODE_NONE;
  Uint8 r = 0, g = 0, b = 0, a = 0;
  SDL_GetRenderDrawBlendMode(renderer, &oldMode);
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);

  if (captionTex_.texture != nullptr) {
    PanelStyle chip;
    chip.radius = 8.0f;
    chip.top = {20, 24, 44, 170};
    chip.bottom = {10, 12, 28, 190};
    chip.border = {150, 160, 255, 90};
    DrawPanel(renderer, {10, 8, captionTex_.width + 20, captionTex_.height + 8}, chip);
    Blit(renderer, captionTex_, 20, 12);
  }
  if (Visible() && !style_.frame) {
    if (style_.speakerPlate && speakerTex_.texture != nullptr) Blit(renderer, speakerTex_, style_.box.x, style_.box.y - speakerTex_.height);
    Blit(renderer, bodyTex_, style_.box.x + style_.padding, style_.box.y + style_.padding);
  } else if (Visible()) {
    DrawPanel(renderer, style_.box);
    float y = style_.box.y + style_.padding * 0.6f;
    if (style_.speakerPlate && speakerTex_.texture != nullptr) {
      // Name plate straddling the top edge of the box.
      PanelStyle plate;
      plate.radius = 8.0f;
      plate.top = {70, 60, 140, 245};
      plate.bottom = {40, 34, 96, 245};
      plate.border = {255, 230, 140, 200};
      const SDL_FRect r{style_.box.x + style_.padding, style_.box.y - speakerTex_.height * 0.55f,
                        speakerTex_.width + 28, speakerTex_.height + 8};
      DrawPanel(renderer, r, plate);
      Blit(renderer, speakerTex_, r.x + 14, r.y + 4);
      y = style_.box.y + speakerTex_.height * 0.45f + style_.padding * 0.5f;
    }
    Blit(renderer, bodyTex_, style_.box.x + style_.padding, y);
  }

  SDL_SetRenderDrawBlendMode(renderer, oldMode);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

} // namespace Kotonoha
