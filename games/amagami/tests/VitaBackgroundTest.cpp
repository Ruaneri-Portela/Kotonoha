#include <Amagami/VitaSpriteRenderer.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
using Amagami::VmValue;
VmValue num(double n) { return VmValue::num(n); }
void native(Amagami::Vita::VitaVmBridge &bridge, const VmValue &self,
            unsigned id, std::vector<VmValue> args) {
  check(bool(bridge.primitive("BG_Picture", "test", false, self, id, args)),
        "Original BG_Picture primitive unavailable");
}
Kotonoha::SurfacePtr solid(unsigned width, unsigned height, Uint8 red,
                           Uint8 green, Uint8 blue) {
  Kotonoha::SurfacePtr out(
      SDL_CreateSurface(int(width), int(height), SDL_PIXELFORMAT_RGBA32));
  check(bool(out), "SDL3 test surface");
  const auto mapped = SDL_MapSurfaceRGBA(out.get(), red, green, blue, 255);
  check(SDL_FillSurfaceRect(out.get(), nullptr, mapped),
        "Fill Vita synthetic background");
  return out;
}
std::array<Uint8, 4> sample(SDL_Surface *s, int x, int y) {
  std::array<Uint8, 4> rgb{};
  check(SDL_ReadSurfacePixel(s, x, y, &rgb[0], &rgb[1], &rgb[2], &rgb[3]),
        "Vita scene sampled pixel");
  return rgb;
}
} // namespace

int main() {
  try {
    Amagami::Vita::VitaVmBridge bridge;
    unsigned resolved = 0, decoded = 0;
    bridge.setNamedPictureSizeResolver(
        [&](const std::string &name)
            -> std::optional<std::pair<uint32_t, uint32_t>> {
          ++resolved;
          if (name == "bg/122_10.gxt")
            return std::make_pair(uint32_t(960), uint32_t(720));
          return std::nullopt;
        });
    auto bg = VmValue::ref(std::make_shared<Amagami::VmObject>());
    const auto cls = bridge.primitive("BG_Picture", "preload", true, bg, 0,
                                      {num(122), num(1), num(0)});
    check(cls && cls->kind == VmValue::Boolean && cls->number == 1,
          "Vita BG_Picture preload checks original named asset");
    native(bridge, bg, 1, {num(122), num(1), num(0), num(0)});
    const auto uid = uint64_t(bg.object->fields[0].number);
    check(bridge.sprites().size() == 1 &&
              bridge.sprites().at(uid).assetName == "bg/122_10.gxt" &&
              bridge.sprites().at(uid).worldView &&
              bridge.sprites().at(uid).rect[2] == 960 &&
              bridge.sprites().at(uid).rect[3] == 720,
          "BG picture enters scene with correct file and source dimensions");

    auto clone = VmValue::ref(std::make_shared<Amagami::VmObject>());
    native(bridge, clone, 2, {num(uid)});
    const auto cloneId = uint64_t(clone.object->fields[0].number);
    check(cloneId != uid &&
              bridge.sprites().at(cloneId).assetName == "bg/122_10.gxt",
          "BG attach duplicates named picture and its world view");
    native(bridge, clone, 3, {});
    check(bridge.sprites().size() == 1, "BG_Picture destructor drops clone");

    Kotonoha::SurfacePtr canvas(
        SDL_CreateSurface(960, 544, SDL_PIXELFORMAT_RGBA32));
    check(bool(canvas), "Create Vita output");
    Kotonoha::RendererPtr renderer(SDL_CreateSoftwareRenderer(canvas.get()));
    check(bool(renderer), "Create Vita software renderer");
    Amagami::Vita::VitaSpriteRenderer presenter(
        renderer.get(),
        [](uint32_t, uint32_t) { return Kotonoha::SurfacePtr{}; });
    presenter.setNamedSource([&](const std::string &name) {
      ++decoded;
      check(name == "bg/122_10.gxt", "Resolver receives canonical CPK asset");
      return solid(960, 720, 240, 60, 10);
    });
    presenter.draw(bridge);
    check(SDL_RenderPresent(renderer.get()), "Render background");
    auto p = sample(canvas.get(), 100, 100);
    check(p[0] > 200 && p[1] < 100 && p[2] < 50,
          "Vita background pixels composite in the world view");
    presenter.draw(bridge);
    check(decoded == 1 && presenter.cachedTextures() == 1,
          "Named background decodes once and shares SDL texture cache");

    native(bridge, bg, 9, {num(100), num(0), num(0), num(10)});
    bridge.tick(5);
    check(std::abs(bridge.sprites().at(uid).position[0] - 50.0) < 0.001,
          "BG_Picture setPos uses original interpolation");
    native(bridge, bg, 4, {});
    check(bg.object->fields[3].number == 5,
          "BG.update reports 5 remaining position frames");
    native(bridge, bg, 15, {});
    check(bridge.sprites().at(uid).position[0] == 100,
          "BG.skip finishes position tween");
    native(bridge, bg, 16, {num(1)});
    check(bridge.sprites().at(uid).paused, "BG.pause sets paused state");
    const auto camera =
        bridge.primitive("K2X_Script", "setScrScl", true, bg, 6, {num(0.5)});
    check(bool(camera), "Set world camera scale");
    presenter.draw(bridge);
    check(SDL_RenderPresent(renderer.get()), "Render camera transform");
    p = sample(canvas.get(), 0, 0);
    check(p[0] == 0 && p[1] == 0,
          "World camera scale changes background coverage");
    p = sample(canvas.get(), 580, 272);
    check(p[0] > 200,
          "World camera image remains visible at translated centre");

    native(bridge, bg, 3, {});
    check(bridge.sprites().empty(), "Destroy background removes scene object");
    bool malformed = false;
    try {
      native(bridge, bg, 1, {num(-1), num(0), num(0), num(0)});
    } catch (const std::exception &) {
      malformed = true;
    }
    check(malformed, "Reject malformed BG IDs");
    bool spinArityRejected = false;
    try {
      (void)bridge.primitive("BG_Picture", "", false, bg, 14, {});
    } catch (const std::exception &) {
      spinArityRejected = true;
    }
    check(spinArityRejected, "BG_Picture #14 enforces spin native arity");
    std::cout << "Vita BG_Picture native/SDL rendering tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
