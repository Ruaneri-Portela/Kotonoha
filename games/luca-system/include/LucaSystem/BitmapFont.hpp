#pragma once

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace LucaSystem {

// FONT__INFO.PAK layout and 100-column atlas, as documented by LuckSystem.
class BitmapFont {
public:
  struct Glyph {
    int x, y, offsetX, offsetY, advance;
  };

  explicit BitmapFont(std::vector<std::uint8_t> bytes)
      : bytes_(std::move(bytes)) {
    if (bytes_.size() < 6)
      throw std::runtime_error("Luca font: truncated header");
    size_ = word(2);
    count_ = word(4);
    if (count_ == 100) {
      if (bytes_.size() < 8)
        throw std::runtime_error("Luca font: truncated legacy header");
      count_ = word(6);
      draw_ = 8;
    }
    index_ = draw_ + size_t(count_) * 3;
    metrics_ = index_ + 2 * kUnicodeCount;
    if (size_ == 0 || count_ == 0 ||
        bytes_.size() < metrics_ + 2 * kUnicodeCount)
      throw std::runtime_error("Luca font: invalid metrics table");
  }

  int cellSize() const { return size_; }
  const std::vector<std::uint8_t> &bytes() const { return bytes_; }

  void setGlyphMetrics(std::uint32_t codepoint, int offsetX, int offsetY,
                       int drawWidth, int advance) {
    if (!glyph(codepoint))
      throw std::runtime_error("Luca font: glyph is not in the atlas");
    validateMetrics(offsetX, offsetY, drawWidth, advance);
    const auto id = word(index_ + 2 * codepoint);
    const auto draw = draw_ + 3 * id;
    bytes_[draw] = static_cast<std::uint8_t>(offsetX);
    bytes_[draw + 1] = static_cast<std::uint8_t>(drawWidth);
    bytes_[draw + 2] = static_cast<std::uint8_t>(offsetY);
    bytes_[metrics_ + 2 * codepoint + 1] = static_cast<std::uint8_t>(advance);
  }

  Glyph appendGlyph(std::uint32_t codepoint, int offsetX, int offsetY,
                    int drawWidth, int advance) {
    if (!codepoint || codepoint >= kUnicodeCount || codepoint == ' ' ||
        codepoint == '\r' || codepoint == '\n' ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
      throw std::runtime_error("Luca font: invalid new glyph codepoint");
    if (word(index_ + 2 * codepoint) != 0)
      throw std::runtime_error("Luca font: codepoint already has a mapping");
    if (count_ == UINT16_MAX)
      throw std::runtime_error("Luca font: no more glyph slots");
    validateMetrics(offsetX, offsetY, drawWidth, advance);
    const int id = count_;
    // A count of 100 is also the legacy marker. Introduce the additional
    // count field when growing a normal header from 99 to 100 glyphs.
    const bool addLegacyField = draw_ == 6 && count_ == 99;
    bytes_.insert(bytes_.begin() + index_, 3, 0);
    if (addLegacyField) {
      bytes_.insert(bytes_.begin() + draw_, 2, 0);
      draw_ += 2;
    }
    ++count_;
    index_ = draw_ + size_t(count_) * 3;
    metrics_ = index_ + 2 * kUnicodeCount;
    putWord(4, draw_ == 8 ? 100 : count_);
    if (draw_ == 8)
      putWord(6, count_);
    putWord(index_ + 2 * codepoint, id);
    bytes_[metrics_ + 2 * codepoint] = 0;
    setGlyphMetrics(codepoint, offsetX, offsetY, drawWidth, advance);
    return *glyph(codepoint);
  }

  bool fitsAtlas(float width, float height) const {
    return width >= (count_ < 100 ? count_ : 100) * size_ &&
           height >= ((count_ + 99) / 100) * size_;
  }

  std::optional<Glyph> glyph(std::uint32_t codepoint) const {
    if (codepoint >= kUnicodeCount)
      return std::nullopt;
    const auto id = word(index_ + 2 * codepoint);
    if (id >= count_ || (id == 0 && codepoint != ' '))
      return std::nullopt;
    const auto draw = draw_ + 3 * id;
    const auto advance = bytes_[metrics_ + 2 * codepoint + 1];
    return Glyph{(id % 100) * size_, (id / 100) * size_,
                 signedByte(bytes_[draw]), signedByte(bytes_[draw + 2]),
                 advance ? advance : bytes_[draw + 1]};
  }

  // Return false if any glyph is missing so callers can fall back for the
  // entire run (including shaping and wrapping), rather than mixing fonts.
  template <class Visit>
  bool forEachGlyph(std::string_view text, Visit visit) const {
    const char *next = text.data();
    size_t left = text.size();
    while (left != 0) {
      const auto before = left;
      const auto codepoint = SDL_StepUTF8(&next, &left);
      if (codepoint == '\n' || codepoint == '\r')
        continue;
      const auto found = glyph(codepoint);
      if (!found ||
          (codepoint == SDL_INVALID_UNICODE_CODEPOINT && before - left != 3))
        return false;
      visit(*found);
    }
    return true;
  }

  // The caller checks coverage for the complete run before drawing it.
  void render(SDL_Renderer &renderer, SDL_Texture &atlas, std::string_view text,
              float x, float y, SDL_Color color) const {
    Uint8 r = 255, g = 255, b = 255, a = 255;
    SDL_GetTextureColorMod(&atlas, &r, &g, &b);
    SDL_GetTextureAlphaMod(&atlas, &a);
    SDL_SetTextureColorMod(&atlas, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(&atlas, color.a);
    const float size = float(cellSize());
    forEachGlyph(text, [&](const Glyph &glyph) {
      const SDL_FRect src{float(glyph.x), float(glyph.y), size, size};
      const SDL_FRect dst{x + glyph.offsetX, y + glyph.offsetY, size, size};
      SDL_RenderTexture(&renderer, &atlas, &src, &dst);
      x += glyph.advance;
    });
    SDL_SetTextureColorMod(&atlas, r, g, b);
    SDL_SetTextureAlphaMod(&atlas, a);
  }

private:
  static constexpr size_t kUnicodeCount = 65536;
  std::vector<std::uint8_t> bytes_;
  int size_ = 0;
  int count_ = 0;
  size_t draw_ = 6;
  size_t index_ = 0;
  size_t metrics_ = 0;

  int word(size_t offset) const {
    return bytes_[offset] | (int(bytes_[offset + 1]) << 8);
  }

  void putWord(size_t offset, int value) {
    bytes_[offset] = static_cast<std::uint8_t>(value);
    bytes_[offset + 1] = static_cast<std::uint8_t>(value >> 8);
  }

  static void validateMetrics(int x, int y, int width, int advance) {
    if (x < -128 || x > 127 || y < -128 || y > 127 || width < 0 ||
        width > 255 || advance < 0 || advance > 255)
      throw std::runtime_error("Luca font: glyph metrics exceed byte range");
  }

  static int signedByte(std::uint8_t value) {
    return value < 128 ? value : int(value) - 256;
  }
};

} // namespace LucaSystem
