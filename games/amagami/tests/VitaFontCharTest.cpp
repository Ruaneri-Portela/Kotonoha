#include <Amagami/VitaSpriteRenderer.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
using Amagami::VmValue;
VmValue n(double x) { return VmValue::num(x); }

void native(Amagami::Vita::VitaVmBridge &bridge, const char *owner,
            const VmValue &self, unsigned primitive,
            std::vector<VmValue> args) {
  require(bool(bridge.primitive(owner, "", false, self, primitive, args)),
          "Expected a native FontChar primitive");
}
Kotonoha::SurfacePtr colored(Uint8 red, Uint8 green, Uint8 blue) {
  Kotonoha::SurfacePtr out(SDL_CreateSurface(36, 42, SDL_PIXELFORMAT_RGBA32));
  require(bool(out), "Create synthetic glyph");
  const Uint32 packed = SDL_MapSurfaceRGBA(out.get(), red, green, blue, 255);
  require(SDL_FillSurfaceRect(out.get(), nullptr, packed),
          "Fill synthetic Shift-JIS glyph");
  return out;
}
std::array<Uint8, 4> pixel(SDL_Surface *surface, int x, int y) {
  std::array<Uint8, 4> rgba{};
  require(SDL_ReadSurfacePixel(surface, x, y, &rgba[0], &rgba[1], &rgba[2],
                               &rgba[3]),
          "Read glyph pixel");
  return rgba;
}
void present(Amagami::Vita::VitaSpriteRenderer &renderer,
             const Amagami::Vita::VitaVmBridge &bridge, SDL_Renderer *out) {
  // A mid-grey backdrop makes black and white shadow pixels observable.
  require(SDL_SetRenderDrawColor(out, 100, 100, 100, 255),
          "Set contrast background");
  require(SDL_RenderClear(out), "Clear contrast background");
  renderer.draw(bridge, false);
  require(SDL_RenderPresent(out), "Present Vita font frame");
}
} // namespace

int main() {
  try {
    Amagami::Vita::VitaVmBridge bridge;
    auto white = VmValue::ref(std::make_shared<Amagami::VmObject>());
    auto dim = VmValue::ref(std::make_shared<Amagami::VmObject>());
    auto alternate = VmValue::ref(std::make_shared<Amagami::VmObject>());
    auto extended = VmValue::ref(std::make_shared<Amagami::VmObject>());

    native(bridge, "FontChar", white, 0,
           {n(0x829f), n(0), n(10), n(-50), n(0), n(1), n(1)});
    native(bridge, "FontChar", dim, 0,
           {n(0x82a0), n(3), n(11), n(0), n(0), n(1), n(1)});
    native(bridge, "FontChar2", alternate, 0,
           {n(0x829f), n(4), n(12), n(50), n(0), n(1), n(1)});
    native(bridge, "FontCharEx", extended, 0,
           {n(0x82a1), n(0), n(9), n(0), n(80), n(1), n(1)});

    const auto id = uint64_t(white.object->fields[0].number);
    const auto &first = bridge.sprites().at(id);
    require(first.fontGlyph && first.fontAtlas == 0 &&
                first.characterCode == 0x829f && first.rect[2] == 36 &&
                first.rect[3] == 42 && first.position[0] == -50 &&
                first.zoom[1] == 1,
            "FontChar preserves native 36x42 glyph data and placement");
    const auto altId = uint64_t(alternate.object->fields[0].number);
    require(bridge.sprites().at(altId).fontAtlas == 1 &&
                bridge.sprites().at(altId).fontPalette == 4,
            "FontChar2 selects alternate IM font atlas");
    const auto exId = uint64_t(extended.object->fields[0].number);
    require(bridge.sprites().at(exId).fontAtlas == 0,
            "FontCharEx uses the standard font atlas");

    Kotonoha::SurfacePtr canvas(
        SDL_CreateSurface(960, 544, SDL_PIXELFORMAT_RGBA32));
    require(bool(canvas), "Create 960x544 canvas");
    Kotonoha::RendererPtr software(SDL_CreateSoftwareRenderer(canvas.get()));
    require(bool(software), "Create software SDL3 renderer");

    unsigned rasterizations = 0;
    Amagami::Vita::VitaSpriteRenderer presenter(
        software.get(),
        [](uint32_t, uint32_t) { return Kotonoha::SurfacePtr{}; });
    presenter.setFontSource([&](unsigned font, uint16_t code,
                                unsigned palette) -> Kotonoha::SurfacePtr {
      ++rasterizations;
      require(palette == 0, "Shadow and dark palette reuse white base glyph");
      if (font == 1)
        return colored(10, 60, 250);
      if (code == 0x829f)
        return colored(240, 240, 240);
      if (code == 0x82a0)
        return colored(220, 220, 220);
      return colored(180, 200, 180);
    });
    present(presenter, bridge, software.get());
    auto sample = pixel(canvas.get(), 375, 260);
    require(sample[0] > 200, "Original FontChar glyph displayed");
    sample = pixel(canvas.get(), 399, 260);
    require(sample[0] == 0 && sample[1] == 0,
            "FontChar shadow is black and shifted one script unit");
    sample = pixel(canvas.get(), 480, 272);
    require(sample[0] > 20 && sample[0] < 80,
            "Palette three applies a dark text tint to original font glyph");
    sample = pixel(canvas.get(), 499, 272);
    require(sample[0] > 200, "Palette three has an unmodulated white shadow");
    sample = pixel(canvas.get(), 580, 272);
    require(sample[2] > 180 && sample[0] < 40,
            "FontChar2 uses the alternate font surface");
    present(presenter, bridge, software.get());
    require(rasterizations == 4 && presenter.cachedTextures() == 4,
            "Glyphs and shadows reuse textures on the second frame");

    native(bridge, "FontChar", white, 6, {n(0x82a0)});
    require(bridge.sprites().at(id).characterCode == 0x82a0,
            "FontChar setChar updates source Shift-JIS code");
    native(bridge, "FontChar", white, 4, {n(0), n(0), n(0), n(10)});
    bridge.tick(5);
    require(std::abs(bridge.sprites().at(id).position[0] + 25) < 0.001,
            "FontChar position tween advances five native frames");
    native(bridge, "FontChar", white, 2, {});
    require(white.object->fields[3].number == 5,
            "FontChar update reports remaining position tween frames");
    native(bridge, "FontChar2", alternate, 1, {n(20)});
    require(bridge.sprites().at(altId).layer == 20,
            "FontChar2 setLayer updates draw order");
    native(bridge, "FontChar", white, 1, {});
    require(!bridge.sprites().count(id) && white.object->fields[0].number == 0,
            "FontChar destroy removes a glyph from the scene");
    bool invalid = false;
    try {
      native(bridge, "FontChar", white, 0,
             {n(0x10000), n(0), n(1), n(0), n(0), n(1), n(1)});
    } catch (const std::exception &) {
      invalid = true;
    }
    require(invalid, "Reject invalid Shift-JIS code before creating object");
    bool arityRejected = false;
    try {
      bridge.primitive("FontChar", "", false, dim, 7, {});
    } catch (const std::exception &) {
      arityRejected = true;
    }
    require(arityRejected,
            "Implemented FontChar vibration enforces original arity");
    require(!bridge.primitive("FontChar", "", false, dim, 11, {}),
            "Unknown font primitives are not silently accepted");

    // Verify the real SCF bytecode dispatcher reaches FontChar rather than
    // exercising only direct calls to the bridge.
    Amagami::Script script;
    script.name = "FontChar";
    Amagami::Method method;
    method.name = "create";
    method.arguments = 7;
    method.code = {0x34, 0, 2}; // native FontChar #0, return
    script.methods.push_back(method);
    Amagami::Vm vm;
    vm.add(script);
    vm.scopedPrimitive = [&bridge](const std::string &owner,
                                   const std::string &selector, bool cls,
                                   const VmValue &receiver, unsigned primitive,
                                   const std::vector<VmValue> &args) {
      return bridge.primitive(owner, selector, cls, receiver, primitive, args);
    };
    const auto dispatched = vm.instance("FontChar");
    vm.call(dispatched, "create",
            {n(0x82a2), n(1), n(12), n(60), n(5), n(1), n(1)});
    vm.run(1000);
    require(vm.finished() && dispatched.object &&
                dispatched.object->fields[0].kind == VmValue::Number,
            "Original SCF FontChar native method returns to the VM");
    const uint64_t dispatchedId = uint64_t(dispatched.object->fields[0].number);
    require(bridge.sprites().at(dispatchedId).characterCode == 0x82a2 &&
                bridge.sprites().at(dispatchedId).fontPalette == 1,
            "SCF native method arguments create a real FontChar scene object");

    std::cout << "Vita FontChar native/SDL3 rendering tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
