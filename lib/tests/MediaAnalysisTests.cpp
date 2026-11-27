#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/SurfaceTexture.hpp>
#include <Kotonoha/media/TextureAlpha.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

std::vector<Uint8> wave(int samples) {
  std::vector<Uint8> bytes;
  const auto u32 = [&](Uint32 value) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(Uint8(value >> (i * 8)));
  };
  const auto u16 = [&](Uint16 value) {
    bytes.push_back(Uint8(value));
    bytes.push_back(Uint8(value >> 8));
  };
  const auto tag = [&](const char *text) { bytes.insert(bytes.end(), text, text + 4); };
  tag("RIFF");
  u32(36 + Uint32(samples) * 4);
  tag("WAVE");
  tag("fmt ");
  u32(16);
  u16(1);
  u16(2);
  u32(8000);
  u32(32000);
  u16(4);
  u16(16);
  tag("data");
  u32(Uint32(samples) * 4);
  for (int i = 0; i < samples; ++i) {
    u16(Uint16(12000));
    u16(Uint16(4000));
  }
  return bytes;
}

void pcmDecode() {
  const auto source = wave(800); // 100 ms at 8 kHz.
  std::vector<std::int16_t> pcm;
  require(Kotonoha::MediaDecoder::decodeMonoS16At44100(source, pcm),
          "mono s16 analysis decode failed");
  require(pcm.size() >= 4400 && pcm.size() <= 4420,
          "analysis decode did not normalize to 44.1 kHz");

  require(Kotonoha::MediaDecoder::decodeMonoS16At44100(source, pcm, nullptr, 20),
          "bounded analysis decode failed");
  require(!pcm.empty() && pcm.size() <= 882,
          "authored duration cap did not bound PCM samples");

  std::atomic_bool cancelled{true};
  require(!Kotonoha::MediaDecoder::decodeMonoS16At44100(source, pcm, &cancelled) &&
              pcm.empty(),
          "cancelled analysis decode retained PCM");

  require(!Kotonoha::MediaDecoder::decodeMonoS16At44100({1, 2, 3}, pcm) &&
              pcm.empty(),
          "invalid analysis media did not fail closed");
}

void straightAlphaEdges() {
  std::vector<std::uint8_t> rgba(5 * 5 * 4, 0);
  const std::size_t center = (2 * 5 + 2) * 4;
  rgba[center] = 90;
  rgba[center + 1] = 120;
  rgba[center + 2] = 150;
  rgba[center + 3] = 128;
  const auto original = rgba;

  require(Kotonoha::prepareStraightAlphaForLinearFiltering(
              rgba.data(), 5, 5, 5 * 4) == 8,
          "straight-alpha edge prep did not fill the one-pixel margin");
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      const std::size_t p = std::size_t(y * 5 + x) * 4;
      const bool neighbor = x >= 1 && x <= 3 && y >= 1 && y <= 3 &&
                            !(x == 2 && y == 2);
      require(rgba[p + 3] == original[p + 3],
              "straight-alpha edge prep changed alpha");
      if (x == 2 && y == 2) {
        require(rgba[p] == 90 && rgba[p + 1] == 120 && rgba[p + 2] == 150,
                "straight-alpha edge prep changed a visible texel");
      } else if (neighbor) {
        require(rgba[p] == 90 && rgba[p + 1] == 120 && rgba[p + 2] == 150,
                "straight-alpha edge prep missed an adjacent transparent texel");
      } else {
        require(rgba[p] == 0 && rgba[p + 1] == 0 && rgba[p + 2] == 0,
                "straight-alpha edge prep spread beyond one pixel");
      }
    }
  }
}

struct Pixel {
  Uint8 r, g, b, a;
};

Pixel pixel(const SDL_Surface &surface, int x, int y) {
  require(surface.format == SDL_PIXELFORMAT_RGBA32,
          "Normalized surface must use byte-order RGBA32");
  require(x >= 0 && x < surface.w && y >= 0 && y < surface.h,
          "Pixel coordinates are in bounds");
  const auto *ptr = static_cast<const Uint8 *>(surface.pixels) +
                    std::size_t(y) * surface.pitch + x * 4;
  return {ptr[0], ptr[1], ptr[2], ptr[3]};
}

void surfaceUploadFormats() {
  require(!Kotonoha::prepareSurfaceForLinearSampling(nullptr),
          "Null input surface must fail safely");

  // Color-keyed RGB24: the hidden magenta backdrop must become alpha zero,
  // including when the upload source does not provide an alpha channel.
  Kotonoha::SurfacePtr keyed(SDL_CreateSurface(3, 3, SDL_PIXELFORMAT_RGB24));
  require(bool(keyed), "Allocate RGB24 color-keyed surface");
  const Uint32 key = SDL_MapSurfaceRGB(keyed.get(), 255, 0, 255);
  require(SDL_FillSurfaceRect(keyed.get(), nullptr, key) &&
              SDL_SetSurfaceColorKey(keyed.get(), true, key) &&
              SDL_SetSurfaceBlendMode(keyed.get(), SDL_BLENDMODE_BLEND),
          "Configure RGB24 color-keyed surface");
  const SDL_Rect center{1, 1, 1, 1};
  require(SDL_FillSurfaceRect(keyed.get(), &center,
                              SDL_MapSurfaceRGB(keyed.get(), 18, 91, 220)),
          "Draw a single visible pixel into keyed surface");
  SDL_BlendMode originalBlend = SDL_BLENDMODE_INVALID;
  require(SDL_GetSurfaceBlendMode(keyed.get(), &originalBlend),
          "Read original blit mode");
  auto prepared = Kotonoha::prepareSurfaceForLinearSampling(keyed.get());
  require(bool(prepared) && prepared->w == 3 && prepared->h == 3,
          "Color-keyed surface conversion succeeds");
  const auto middle = pixel(*prepared, 1, 1);
  require(middle.r == 18 && middle.g == 91 && middle.b == 220 &&
              middle.a == 255,
          "Color-keyed conversion retains visible pixel colors");
  const auto border = pixel(*prepared, 0, 1);
  require(border.a == 0 && border.r == middle.r && border.g == middle.g &&
              border.b == middle.b,
          "Linear-sampling preparation fills RGB of transparent neighbors");
  require(pixel(*prepared, 0, 0).a == 0,
          "Color-keyed canvas background stays transparent");
  SDL_BlendMode afterBlend = SDL_BLENDMODE_INVALID;
  require(SDL_GetSurfaceBlendMode(keyed.get(), &afterBlend) &&
              afterBlend == originalBlend &&
              SDL_SurfaceHasColorKey(keyed.get()),
          "Preparation must not change source blend mode or color key");

  // Palette-index color keys refer to the original pixel index, not its
  // RGB value. Duplicated palette colors must not acquire false transparency.
  Kotonoha::SurfacePtr indexed(SDL_CreateSurface(3, 3, SDL_PIXELFORMAT_INDEX8));
  require(bool(indexed), "Allocate INDEX8 source surface");
  auto *palette = SDL_CreateSurfacePalette(indexed.get());
  const SDL_Color colors[3] = {
      {255, 0, 255, 255}, {18, 91, 220, 255}, {255, 0, 255, 255}};
  require(palette && SDL_SetPaletteColors(palette, colors, 0, 3) &&
              SDL_FillSurfaceRect(indexed.get(), nullptr, 0) &&
              SDL_SetSurfaceColorKey(indexed.get(), true, 0),
          "Configure indexed palette and its transparent pixel index");
  const SDL_Rect otherIndex{0, 2, 1, 1};
  require(SDL_FillSurfaceRect(indexed.get(), &center, 1) &&
              SDL_FillSurfaceRect(indexed.get(), &otherIndex, 2),
          "Paint two distinct indexed entries");
  const auto indexedPrepared =
      Kotonoha::prepareSurfaceForLinearSampling(indexed.get());
  require(bool(indexedPrepared) && pixel(*indexedPrepared, 0, 0).a == 0 &&
              pixel(*indexedPrepared, 1, 1).a == 255 &&
              pixel(*indexedPrepared, 0, 2).a == 255 &&
              pixel(*indexedPrepared, 0, 2).r == 255 &&
              SDL_SurfaceHasColorKey(indexed.get()) &&
              SDL_GetSurfacePalette(indexed.get()) == palette,
          "Indexed color-key conversion must preserve non-key palette entries");

  // A non-keyed RGB source must stay opaque and convert accurately.
  Kotonoha::SurfacePtr rgb(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGB24));
  require(bool(rgb) &&
              SDL_FillSurfaceRect(rgb.get(), nullptr,
                                  SDL_MapSurfaceRGB(rgb.get(), 31, 49, 68)),
          "Create opaque RGB24 surface");
  const auto opaque = Kotonoha::prepareSurfaceForLinearSampling(rgb.get());
  require(bool(opaque) && pixel(*opaque, 0, 0).a == 255 &&
              pixel(*opaque, 0, 0).r == 31 && pixel(*opaque, 0, 0).g == 49 &&
              pixel(*opaque, 0, 0).b == 68,
          "RGB24 conversion must not add artificial transparency");

  // A 32-bit BGRA surface must preserve straight RGBA values.
  Kotonoha::SurfacePtr bgra(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_BGRA32));
  require(bool(bgra) && SDL_FillSurfaceRect(
                            bgra.get(), nullptr,
                            SDL_MapSurfaceRGBA(bgra.get(), 121, 44, 239, 127)),
          "Create BGRA32 surface");
  const auto converted = Kotonoha::prepareSurfaceForLinearSampling(bgra.get());
  const auto value = converted ? pixel(*converted, 0, 0) : Pixel{0, 0, 0, 0};
  require(converted && value.r == 121 && value.g == 44 && value.b == 239 &&
              value.a == 127,
          "BGRA32 conversion must preserve channels and partial alpha");
}

void nativeCanvasComposite() {
  // This is a synthetic UI composition fixture, not original game artwork.
  // Exercise the same 800x600 presentation and bilinear alpha pathway used
  // by game menus, save dialogs, overlays and splash screens.
  Kotonoha::SurfacePtr canvas(
      SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32));
  require(bool(canvas) && SDL_FillSurfaceRect(
                              canvas.get(), nullptr,
                              SDL_MapSurfaceRGBA(canvas.get(), 3, 46, 58, 255)),
          "Create opaque 800x600 presentation canvas");

  Kotonoha::SurfacePtr sprite(SDL_CreateSurface(3, 3, SDL_PIXELFORMAT_RGBA32));
  require(bool(sprite) &&
              SDL_FillSurfaceRect(sprite.get(), nullptr,
                                  SDL_MapSurfaceRGBA(sprite.get(), 0, 0, 0, 0)),
          "Create transparent overlay fixture");
  const SDL_Rect center{1, 1, 1, 1};
  require(
      SDL_FillSurfaceRect(sprite.get(), &center,
                          SDL_MapSurfaceRGBA(sprite.get(), 231, 119, 43, 255)),
      "Draw overlay foreground pixel");
  const auto prepared = Kotonoha::prepareSurfaceForLinearSampling(sprite.get());
  require(bool(prepared) && pixel(*sprite, 0, 1).r == 0 &&
              pixel(*prepared, 0, 1).r == 231,
          "Uploading must not modify source pixels or lose edge color");
  Kotonoha::SurfacePtr scaled(
      SDL_ScaleSurface(prepared.get(), 96, 96, SDL_SCALEMODE_LINEAR));
  require(bool(scaled) && scaled->w == 96 && scaled->h == 96,
          "Scale transparent overlay using linear interpolation");
  const SDL_Rect destination{290, 170, 96, 96};
  require(
      SDL_SetSurfaceBlendMode(scaled.get(), SDL_BLENDMODE_BLEND) &&
          SDL_BlitSurface(scaled.get(), nullptr, canvas.get(), &destination),
      "Composite overlay on native-resolution canvas");
  const auto background = pixel(*canvas, 0, 0);
  const auto foreground = pixel(*canvas, 338, 218);
  require(background.r == 3 && background.g == 46 && background.b == 58 &&
              background.a == 255,
          "Scene composition must preserve the rest of the 800x600 canvas");
  require(foreground.r > background.r && foreground.b != background.b &&
              foreground.a == 255,
          "Scaled overlay must composite visible color, not black fringe");
}

} // namespace

int main() {
  try {
    pcmDecode();
    straightAlphaEdges();
    surfaceUploadFormats();
    nativeCanvasComposite();
    std::cout << "KotonohaMediaAnalysisTests PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "KotonohaMediaAnalysisTests FAIL: " << error.what() << '\n';
    return 1;
  }
}
