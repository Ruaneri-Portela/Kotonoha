#include <Amagami/VitaSpriteRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
Amagami::VmValue n(double x) { return Amagami::VmValue::num(x); }
void call(Amagami::Vita::VitaVmBridge &bridge, const Amagami::VmValue &obj,
          unsigned primitive, std::vector<Amagami::VmValue> args) {
  check(bool(bridge.primitive("Sprite", "", false, obj, primitive, args)),
        "Primitive failed");
}
Kotonoha::SurfacePtr coloured(uint32_t id) {
  Kotonoha::SurfacePtr s(SDL_CreateSurface(40, 40, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    throw std::runtime_error(SDL_GetError());
  const auto rgb = id == 1 ? SDL_MapSurfaceRGBA(s.get(), 255, 0, 0, 255)
                           : SDL_MapSurfaceRGBA(s.get(), 0, 255, 0, 255);
  check(SDL_FillSurfaceRect(s.get(), nullptr, rgb), "Fill test texture");
  return s;
}
void draw(Amagami::Vita::VitaSpriteRenderer &presenter,
          Amagami::Vita::VitaVmBridge &bridge, SDL_Renderer *renderer) {
  presenter.draw(bridge);
  check(SDL_RenderPresent(renderer), "Present software frame");
}
std::array<Uint8, 4> pixel(SDL_Surface *canvas, int x, int y) {
  std::array<Uint8, 4> rgb{{0, 0, 0, 0}};
  check(SDL_ReadSurfacePixel(canvas, x, y, &rgb[0], &rgb[1], &rgb[2], &rgb[3]),
        "Read source pixel");
  return rgb;
}
} // namespace

int main() {
  try {
    Amagami::Vita::VitaVmBridge bridge;
    bridge.setTextureSizeResolver(
        [](uint32_t id,
           uint32_t) -> std::optional<std::pair<uint32_t, uint32_t>> {
          if (id == 1 || id == 2)
            return std::make_pair(uint32_t(40), uint32_t(40));
          return std::nullopt;
        });
    auto red = Amagami::VmValue::ref(std::make_shared<Amagami::VmObject>());
    auto green = Amagami::VmValue::ref(std::make_shared<Amagami::VmObject>());
    call(bridge, red, 1, {n(1), n(0), n(1)});
    call(bridge, green, 1, {n(2), n(0), n(2)});

    Kotonoha::SurfacePtr canvas(
        SDL_CreateSurface(960, 544, SDL_PIXELFORMAT_RGBA32));
    check(bool(canvas), "Create 960x544 output");
    Kotonoha::RendererPtr software(SDL_CreateSoftwareRenderer(canvas.get()));
    check(bool(software), "Create SDL3 software renderer");
    int created = 0;
    Amagami::Vita::VitaSpriteRenderer presenter(
        software.get(), [&](uint32_t id, uint32_t) -> Kotonoha::SurfacePtr {
          ++created;
          return coloured(id);
        });

    draw(presenter, bridge, software.get());
    auto p = pixel(canvas.get(), 480, 272);
    check(p[1] > p[0], "Higher sprite layer occludes lower layer");
    check(created == 2 && presenter.cachedTextures() == 2,
          "Texture objects cached once by ID and variant");
    draw(presenter, bridge, software.get());
    check(created == 2, "Second frame reuses SDL textures");

    // Position base tween (native 10): x=100, y=0 over ten script frames.
    call(bridge, green, 10, {n(100), n(0), n(0), n(10), n(0)});
    bridge.tick(5);
    const auto greenId = uint64_t(green.object->fields[0].number);
    check(std::abs(bridge.sprites().at(greenId).position[0] - 50) < 1e-8,
          "Native linear tween interpolates across script frames");
    call(bridge, green, 7, {});
    check(green.object->fields[3].number == 5,
          "Sprite.update reports remaining position tween time");
    draw(presenter, bridge, software.get());
    p = pixel(canvas.get(), 480, 272);
    check(p[0] > p[1], "Moved green sprite exposes original red sprite");
    p = pixel(canvas.get(), 580, 272);
    check(p[1] > p[0], "Position converts 50 script units to 100 pixels");

    // Pause freezes running tweens; unpause completes them.
    call(bridge, green, 18, {n(1)});
    bridge.tick(5);
    check(std::abs(bridge.sprites().at(greenId).position[0] - 50) < 1e-8,
          "Paused sprites do not advance position");
    call(bridge, green, 18, {n(0)});
    bridge.tick(5);
    check(std::abs(bridge.sprites().at(greenId).position[0] - 100) < 1e-8,
          "Unpaused sprite reaches tween target");

    // Opacity and zoom use the same native SCF path.
    call(bridge, red, 9, {n(0.0), n(2), n(10), n(0)});
    bridge.tick(5);
    const auto redId = uint64_t(red.object->fields[0].number);
    // Upstream Vita mode 2 is ease-in: alpha fades slowly first and
    // reaches 0.75 at the midpoint of a 1.0 -> 0.0 ten-frame tween.
    check(std::abs(bridge.sprites().at(redId).alpha - 0.75) < 1e-8,
          "Vita mode-2 ease-in opacity at midpoint");
    call(bridge, red, 11, {n(1.5), n(1.5), n(0), n(4), n(0)});
    bridge.tick(2);
    check(std::abs(bridge.sprites().at(redId).zoom[0] - 1.25) < 1e-8,
          "Vita zoom interpolates");
    call(bridge, red, 17, {});
    check(bridge.sprites().at(redId).alpha == 0 &&
              bridge.sprites().at(redId).zoom[0] == 1.5,
          "Skip animation snaps all active tweens");
    bool rejected = false;
    try {
      bridge.tick(-1);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "Negative animation delta rejected");
    std::cout << "Vita SDL3 scene renderer and tween synthetic tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
