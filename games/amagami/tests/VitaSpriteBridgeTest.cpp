#include <Amagami/VitaVmBridge.hpp>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
  bool caught = false;
  try {
    fn();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught, "Expected unsupported or invalid Vita primitive to fail");
}
Amagami::Script scriptedSprite() {
  Amagami::Script s;
  s.name = "Sprite";
  struct NativeSpec {
    const char *method;
    uint8_t number, arguments;
  };
  for (const auto spec :
       {NativeSpec{"spawn", 1, 3}, NativeSpec{"clone", 2, 1},
        NativeSpec{"destroy", 3, 0}, NativeSpec{"setTexture", 4, 2},
        NativeSpec{"setRect", 5, 4}, NativeSpec{"setLayer", 6, 1},
        NativeSpec{"update", 7, 0}, NativeSpec{"animate", 8, 6},
        NativeSpec{"pause", 18, 1}}) {
    Amagami::Method m;
    m.name = spec.method;
    m.arguments = spec.arguments;
    m.code = {0x34, spec.number, 2}; // native, return
    s.methods.push_back(std::move(m));
  }
  return s;
}
void run(Amagami::Vm &vm, const Amagami::VmValue &object,
         const std::string &method, std::vector<Amagami::VmValue> args = {}) {
  vm.call(object, method, std::move(args));
  vm.run(1000);
  check(vm.finished(), "Synthetic sprite primitive terminated");
}
} // namespace

int main() {
  try {
    using namespace Amagami;
    Vita::VitaVmBridge bridge;
    size_t resolutions = 0;
    bridge.setTextureSizeResolver(
        [&](uint32_t id,
            uint32_t variant) -> std::optional<std::pair<uint32_t, uint32_t>> {
          ++resolutions;
          if (id == 8 && variant == 2)
            return std::make_pair(uint32_t(64), uint32_t(32));
          if (id == 9 && variant == 0)
            return std::make_pair(uint32_t(128), uint32_t(48));
          return std::nullopt;
        });
    Vm vm;
    vm.add(scriptedSprite());
    vm.scopedPrimitive = [&bridge](const std::string &owner,
                                   const std::string &method, bool cls,
                                   const VmValue &receiver, unsigned code,
                                   const std::vector<VmValue> &args) {
      return bridge.primitive(owner, method, cls, receiver, code, args);
    };
    auto original = vm.instance("Sprite");
    run(vm, original, "spawn",
        {VmValue::num(8), VmValue::num(2), VmValue::num(11)});
    check(bridge.sprites().size() == 1 && resolutions == 1,
          "Sprite creation goes through the Vita graphics resolver");
    const auto id = uint64_t(original.object->fields[0].number);
    const auto &sprite = bridge.sprites().at(id);
    check(sprite.texture == 8 && sprite.variant == 2 && sprite.layer == 11 &&
              sprite.rect[2] == 64 && sprite.rect[3] == 32 && sprite.hasImage,
          "Sprite keeps texture ID, variant, layer and decoded size");

    run(vm, original, "setRect",
        {VmValue::num(1), VmValue::num(2), VmValue::num(20), VmValue::num(12)});
    check(bridge.sprites().at(id).rect ==
              (std::array<double, 4>{{2, 4, 40, 24}}),
          "Vita image source crop is two texels per script unit");
    const auto oldOrder = bridge.sprites().at(id).order;
    run(vm, original, "setLayer", {VmValue::num(30)});
    check(bridge.sprites().at(id).layer == 30 &&
              bridge.sprites().at(id).order > oldOrder,
          "Layer updates preserve order for later draw sorting");
    run(vm, original, "pause", {VmValue::num(1)});
    check(bridge.sprites().at(id).paused, "Sprite pause flag");
    run(vm, original, "pause", {VmValue::num(0)});
    check(!bridge.sprites().at(id).paused, "Numeric zero unpauses");

    auto second = vm.instance("Sprite");
    run(vm, second, "clone", {VmValue::num(id)});
    const auto cloneId = uint64_t(second.object->fields[0].number);
    check(cloneId != id && bridge.sprites().size() == 2 &&
              bridge.sprites().at(cloneId).rect == bridge.sprites().at(id).rect,
          "Clone has a new UID with the source picture and crop");
    run(vm, second, "setTexture", {VmValue::num(9), VmValue::num(0)});
    check(bridge.sprites().at(cloneId).texture == 9 &&
              bridge.sprites().at(cloneId).rect[2] == 128 &&
              bridge.sprites().at(id).texture == 8,
          "setTex replaces only the selected sprite");

    run(vm, original, "setTexture", {VmValue::num(0), VmValue::num(0)});
    check(!bridge.sprites().at(id).hasImage &&
              bridge.sprites().at(id).rect[2] == 480 &&
              bridge.sprites().at(id).rect[3] == 272,
          "Texture zero is a whole-screen plain rectangle");
    run(vm, original, "setRect",
        {VmValue::num(1), VmValue::num(2), VmValue::num(20), VmValue::num(12)});
    check(bridge.sprites().at(id).rect[2] == 20,
          "Plain rectangle source coordinates are not doubled");
    run(vm, original, "update");
    check(original.object->fields[1].number == 0 &&
              original.object->fields[4].number == 0,
          "Non-animated sprite update reports zero remaining time");

    rejects([&] {
      bridge.primitive("Sprite", "spawn", false, original, 1,
                       {VmValue::num(337), VmValue::num(0), VmValue::num(0)});
    });
    rejects([&] {
      bridge.primitive("Sprite", "spawn", false, original, 1,
                       {VmValue::num(1.5), VmValue::num(0), VmValue::num(0)});
    });
    rejects([&] {
      bridge.primitive("Sprite", "setRect", false, original, 5,
                       {VmValue::num(1)});
    });
    rejects([&] {
      bridge.primitive("Sprite", "animate", false, original, 8, {});
    }); // Now implemented with strict native arity.
    rejects([&] {
      bridge.primitive("Sprite", "spin", false, original, 12, {});
    }); // Spin is implemented with strict native arity.
    rejects([&] {
      bridge.primitive("BG_Picture", "spawn", false, original, 1, {});
    }); // BG_Picture now exists, with strict native argument count.
    check(!bridge.primitive("ActorPicture", "spawn", false, original, 1, {}),
          "Unimplemented actor remains an explicit native error");
    check(bridge.sprites().size() == 2,
          "Rejected operations do not insert phantom scene objects");

    run(vm, second, "destroy");
    check(bridge.sprites().size() == 1 && second.object->fields[0].number == 0,
          "Destroy clears Sprite UID");
    run(vm, original, "destroy");
    check(bridge.sprites().empty(), "No stale objects after destruction");

    run(vm, original, "spawn",
        {VmValue::num(8), VmValue::num(2), VmValue::num(0)});
    Amagami::Script k2x;
    k2x.name = "K2X_Script";
    Amagami::Method reset;
    reset.name = "resetPrim";
    reset.code = {0x34, 0, 2};
    k2x.methods.push_back(reset);
    vm.add(k2x);
    run(vm, vm.instance("K2X_Script"), "resetPrim");
    check(bridge.sprites().empty(),
          "K2X_Script.resetPrim clears Vita scene objects");
    std::cout << "Vita Sprite SCF bridge tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
