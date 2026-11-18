#include <Amagami/VitaSpriteRenderer.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Amagami::VmValue;
using Amagami::Vita::VitaVmBridge;
VmValue n(double v) { return VmValue::num(v); }
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void invoke(VitaVmBridge &bridge, const char *owner, const VmValue &obj,
            unsigned primitive, std::vector<VmValue> args) {
  require(bool(bridge.primitive(owner, "", false, obj, primitive, args)),
          "Expected Vita native primitive");
}
void close(double a, double b, const char *message) {
  require(std::abs(a - b) < 0.000001, message);
}
Kotonoha::SurfacePtr striped() {
  Kotonoha::SurfacePtr surface(
      SDL_CreateSurface(64, 16, SDL_PIXELFORMAT_RGBA32));
  require(bool(surface), "Create colored Vita source");
  SDL_Rect left{0, 0, 32, 16}, right{32, 0, 32, 16};
  require(
      SDL_FillSurfaceRect(surface.get(), &left,
                          SDL_MapSurfaceRGBA(surface.get(), 230, 15, 15, 255)),
      "Fill red half of source");
  require(
      SDL_FillSurfaceRect(surface.get(), &right,
                          SDL_MapSurfaceRGBA(surface.get(), 15, 230, 15, 255)),
      "Fill green half of source");
  return surface;
}
std::array<Uint8, 4> pixel(SDL_Surface *surface, int x, int y) {
  std::array<Uint8, 4> rgb{};
  require(
      SDL_ReadSurfacePixel(surface, x, y, &rgb[0], &rgb[1], &rgb[2], &rgb[3]),
      "Read rotated pixel");
  return rgb;
}
void draw(Amagami::Vita::VitaSpriteRenderer &presenter, VitaVmBridge &bridge,
          SDL_Renderer *software) {
  require(SDL_SetRenderDrawColor(software, 255, 255, 255, 255),
          "Set white contrast background");
  require(SDL_RenderClear(software), "Clear test canvas");
  presenter.draw(bridge, false);
  require(SDL_RenderPresent(software), "Present rotated Vita scene");
}
} // namespace

int main() {
  try {
    VitaVmBridge bridge;
    bridge.setTextureSizeResolver(
        [](uint32_t texture,
           uint32_t) -> std::optional<std::pair<uint32_t, uint32_t>> {
          if (texture == 1)
            return std::make_pair(uint32_t(64), uint32_t(16));
          return std::nullopt;
        });
    auto sprite = VmValue::ref(std::make_shared<Amagami::VmObject>());
    invoke(bridge, "Sprite", sprite, 1, {n(1), n(0), n(5)});
    const uint64_t id = uint64_t(sprite.object->fields[0].number);

    Kotonoha::SurfacePtr canvas(
        SDL_CreateSurface(960, 544, SDL_PIXELFORMAT_RGBA32));
    require(bool(canvas), "Create full Vita canvas");
    Kotonoha::RendererPtr renderer(SDL_CreateSoftwareRenderer(canvas.get()));
    require(bool(renderer), "Create Vita SDL software renderer");
    Amagami::Vita::VitaSpriteRenderer presenter(
        renderer.get(), [](uint32_t id, uint32_t) {
          return id == 1 ? striped() : Kotonoha::SurfacePtr{};
        });

    draw(presenter, bridge, renderer.get());
    auto red = pixel(canvas.get(), 456, 272);
    auto green = pixel(canvas.get(), 504, 272);
    require(red[0] > 200 && red[1] < 100 && green[1] > 200 && green[0] < 100,
            "Initial unrotated sprite has red left, green right");

    // Spin absolute angle uses units of pi, positive counterclockwise.
    invoke(bridge, "Sprite", sprite, 12, {n(0.5), n(0), n(0), n(0)});
    close(bridge.sprites().at(id).angle, 0.5,
          "Immediate absolute-angle native");
    draw(presenter, bridge, renderer.get());
    red = pixel(canvas.get(), 480, 294);
    green = pixel(canvas.get(), 480, 250);
    require(
        red[0] > 200 && red[1] < 100 && green[1] > 200 && green[0] < 100,
        "Vita positive half-pi turns red-left downward and green-right upward");
    require(presenter.cachedTextures() == 1,
            "Spin reuses the cached GPU texture");

    // Angular speed 15 contributes 15/30 = 0.5*pi per script frame.
    invoke(bridge, "Sprite", sprite, 12, {n(15), n(0), n(0), n(1)});
    bridge.tick(1);
    close(bridge.sprites().at(id).angle, 1,
          "Speed component accumulates half-turn units per frame");
    invoke(bridge, "Sprite", sprite, 18, {n(1)});
    bridge.tick(5);
    close(bridge.sprites().at(id).angle, 1,
          "Pause freezes the spin velocity and angle");
    invoke(bridge, "Sprite", sprite, 18, {n(0)});
    bridge.tick(1);
    close(bridge.sprites().at(id).angle, 1.5,
          "Unpause advances the angular velocity");

    // A new absolute-angle request resets angular speed and previous turns.
    invoke(bridge, "Sprite", sprite, 12, {n(0), n(0), n(0), n(0)});
    close(bridge.sprites().at(id).angle, 0,
          "Absolute spin resets velocity and accumulated turns");
    invoke(bridge, "Sprite", sprite, 12, {n(1), n(0), n(10), n(0)});
    bridge.tick(5);
    close(bridge.sprites().at(id).angle, 0.5,
          "Angle interpolates over original SCF frame duration");
    invoke(bridge, "Sprite", sprite, 17, {});
    close(bridge.sprites().at(id).angle, 0.5,
          "Original skip() does not finish the independent Spin channel");
    bridge.tick(5);
    close(bridge.sprites().at(id).angle, 1,
          "Spin reaches the absolute target after remaining frames");

    // BG_Picture uses primitive #14 for exactly the same spin channel.
    bridge.setNamedPictureSizeResolver(
        [](const std::string &)
            -> std::optional<std::pair<uint32_t, uint32_t>> {
          return std::make_pair(uint32_t(64), uint32_t(16));
        });
    auto bg = VmValue::ref(std::make_shared<Amagami::VmObject>());
    invoke(bridge, "BG_Picture", bg, 1, {n(122), n(1), n(0), n(0)});
    const uint64_t bgId = uint64_t(bg.object->fields[0].number);
    invoke(bridge, "BG_Picture", bg, 14, {n(0.25), n(0), n(0), n(0)});
    close(bridge.sprites().at(bgId).angle, 0.25,
          "Original BG_Picture #14 enables scene rotation");

    // Texture-zero rectangles are composited with the same rotation rather
    // than unrotated SDL_RenderFillRect.
    VitaVmBridge rectangleBridge;
    auto bar = VmValue::ref(std::make_shared<Amagami::VmObject>());
    invoke(rectangleBridge, "Sprite", bar, 1, {n(0), n(0), n(2)});
    invoke(rectangleBridge, "Sprite", bar, 5, {n(0), n(0), n(30), n(10)});
    invoke(rectangleBridge, "Sprite", bar, 12, {n(0.5), n(0), n(0), n(0)});
    draw(presenter, rectangleBridge, renderer.get());
    auto black = pixel(canvas.get(), 480, 250);
    auto white = pixel(canvas.get(), 450, 272);
    require(black[0] < 10 && white[0] > 245,
            "Vita rotated black rectangle occludes only its rotated bounds");

    // Reject malformed spin calls without changing the scene.
    const auto before = bridge.sprites().at(id).angle;
    bool rejected = false;
    try {
      invoke(bridge, "Sprite", sprite, 12, {n(1), n(0), n(-1), n(0)});
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Negative angle duration must be rejected");
    close(bridge.sprites().at(id).angle, before,
          "Invalid spin leaves previous interpolation intact");
    rejected = false;
    try {
      invoke(bridge, "Sprite", sprite, 12, {n(1), n(0), n(10), n(2)});
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Unknown spin component rejected");
    // Also verify SCF native bytecode dispatch (opcode 0x34, native #12)
    // actually reaches the new Spin implementation through its class owner.
    Amagami::Script scripted;
    scripted.name = "Sprite";
    Amagami::Method spinMethod;
    spinMethod.name = "spin";
    spinMethod.arguments = 4;
    spinMethod.code = {0x34, 12, 2}; // native, return
    scripted.methods.push_back(spinMethod);
    Amagami::Vm vm;
    vm.add(scripted);
    vm.scopedPrimitive = [&bridge](const std::string &owner,
                                   const std::string &selector, bool isClass,
                                   const VmValue &self, unsigned primitive,
                                   const std::vector<VmValue> &args) {
      return bridge.primitive(owner, selector, isClass, self, primitive, args);
    };
    auto dispatched = vm.instance("Sprite");
    invoke(bridge, "Sprite", dispatched, 1, {n(1), n(0), n(9)});
    vm.call(dispatched, "spin", {n(0.5), n(0), n(0), n(0)});
    vm.run(1000);
    require(vm.finished(), "Original SCF spin method returns normally");
    close(bridge.sprites()
              .at(uint64_t(dispatched.object->fields[0].number))
              .angle,
          0.5, "SCF bytecode dispatch changes rotation of the scene object");

    std::cout << "Vita Sprite/BG_Picture spin and SDL rotation passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
