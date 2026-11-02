#include <SummerRadish/Pack.hpp>
#include <stdexcept>
using namespace SummerRadish;
namespace {
void graphicsRequire(bool ok) {
  if (!ok)
    throw std::runtime_error("Summer graphics regression failed");
}
void appendWord(Bytes &bytes, uint32_t n) {
  for (int i = 0; i < 4; ++i)
    bytes.push_back(uint8_t(n >> (i * 8)));
}
Image canvas(uint8_t value, int width = 1, int height = 1) {
  Image image;
  image.width = width;
  image.height = height;
  image.rgba.resize(size_t(width) * height * 4, value);
  for (size_t i = 3; i < image.rgba.size(); i += 4)
    image.rgba[i] = 255;
  return image;
}
} // namespace
void testGraphics() {
  // Texture.mod FUN_10001300: alpha is coverage/128, and source is already
  // premultiplied. The half-covered source contributes 20 + 100/2, not 20/2.
  Bytes texb;
  appendWord(texb, 1);
  appendWord(texb, 1);
  texb.insert(texb.end(), {10, 20, 30, 64});
  const auto texture = decodeImage(texb, "TEXB");
  graphicsRequire(texture.rgba == Bytes({60, 40, 20, 128}));
  graphicsRequire(texture.nativePixels == Bytes({30, 20, 10, 64}));
  auto target = canvas(100);
  compositeImage(target, texture, 0, 0);
  graphicsRequire(target.rgba == Bytes({80, 70, 60, 255}));
  texb.back() = 128;
  auto opaque = decodeImage(texb, "TEXB");
  target = canvas(100);
  compositeImage(target, opaque, 0, 0);
  graphicsRequire(target.rgba == Bytes({30, 20, 10, 255}));
  texb.back() = 0;
  auto clear = decodeImage(texb, "TEXB");
  target = canvas(100);
  compositeImage(target, clear, 0, 0);
  graphicsRequire(target.rgba == Bytes({100, 100, 100, 255}));
  // TEXB storage rows run bottom up; channel order is BGRA.
  Bytes rows;
  appendWord(rows, 1);
  appendWord(rows, 2);
  rows.insert(rows.end(), {1, 2, 3, 128, 4, 5, 6, 128});
  auto vertical = decodeImage(rows, "TEXB");
  graphicsRequire(vertical.rgba == Bytes({6, 5, 4, 255, 3, 2, 1, 255}));
  target = canvas(100);
  compositeImageRegion(target, vertical, 0, 0, 0, 1, 1, 1);
  graphicsRequire(target.rgba == Bytes({3, 2, 1, 255}));
  // CHR control zero is one /128 coverage unit, rather than transparency.
  Bytes chr;
  for (auto word : {uint32_t(42), uint32_t(42), uint32_t(3), uint32_t(1),
                    uint32_t(17), uint32_t(29), uint32_t(0), uint32_t(0)})
    appendWord(chr, word);
  chr.insert(chr.end(), {0, 128, 128, 128, 127, 10, 20, 30, 255});
  auto character = decodeImage(chr, "CHR");
  graphicsRequire(character.x == 17 && character.y == 29);
  target = canvas(255, 3);
  compositeImage(target, character, 0, 0);
  graphicsRequire(target.rgba == Bytes({254, 254, 254, 255, 30, 20, 10, 255,
                                        255, 255, 255, 255}));
  // A missing trailing transparent run is legal; partial clipping preserves
  // source coordinate alignment and pixels outside the destination are ignored.
  target = canvas(100, 2);
  compositeImage(target, character, -1, 0);
  graphicsRequire(target.rgba == Bytes({30, 20, 10, 255, 100, 100, 100, 255}));
  // CHR SubAnime: signed actor/selector, three straight-BGRA frames, absolute
  // coordinates. The loader premultiplies the embedded pixels by alpha/128.
  Bytes animated;
  for (auto value : {uint32_t(68), uint32_t(32), uint32_t(1), uint32_t(1),
                     uint32_t(0), uint32_t(0), uint32_t(0), uint32_t(0)})
    appendWord(animated, value);
  appendWord(animated, 32);
  animated.insert(animated.end(),
                  {3, 0, 0, 0, 3, 0, 0, 0, 17, 0, 29, 0, 1, 0, 1, 0});
  animated.insert(animated.end(),
                  {20, 40, 60, 64, 10, 20, 30, 128, 0, 0, 0, 0});
  appendWord(animated, 0);
  auto animations = decodeCharacterAnimations(animated);
  graphicsRequire(animations.size() == 1 && animations[0].actor == 3 &&
                  animations[0].frames.size() == 3);
  graphicsRequire(animations[0].frames[0].x == 17 &&
                  animations[0].frames[0].y == 29);
  target = canvas(100);
  compositeImage(target, animations[0].frames[0], 0, 0);
  graphicsRequire(target.rgba == Bytes({80, 70, 60, 255}));
  target = canvas(100);
  compositeCharacterAnimations(target, animations, -17, -29);
  graphicsRequire(target.rgba == Bytes({80, 70, 60, 255}));
  graphicsRequire(selectCharacterAnimationFrame(animations, 3, 0, 1));
  graphicsRequire(animations[0].currentFrame == 1);
  target = canvas(100);
  compositeCharacterAnimations(target, animations, -17, -29);
  graphicsRequire(target.rgba == Bytes({30, 20, 10, 255}));
  graphicsRequire(!selectCharacterAnimationFrame(animations, 3, 1, 0));
  graphicsRequire(!selectCharacterAnimationFrame(animations, 4, 0, 0));
  graphicsRequire(!selectCharacterAnimationFrame(animations, 3, 0, -1));
  graphicsRequire(!selectCharacterAnimationFrame(animations, 3, 0, 3));
  graphicsRequire(animations[0].currentFrame == 1);
  animations.push_back(animations[0]);
  graphicsRequire(selectCharacterAnimationFrame(animations, 3, 0, 2));
  graphicsRequire(animations[0].currentFrame == 2 &&
                  animations[1].currentFrame == 2);
}
