#include <Amagami/VitaVmBridge.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Amagami::VmValue;
VmValue n(double number) { return VmValue::num(number); }
void expect(bool test, const char *message) {
  if (!test)
    throw std::runtime_error(message);
}
void call(Amagami::Vita::VitaVmBridge &bridge, const char *owner,
          const VmValue &self, unsigned number, std::vector<VmValue> args) {
  expect(bool(bridge.primitive(owner, "", false, self, number, args)),
         "Vita vibration native not implemented");
}
uint64_t uid(const VmValue &self) {
  return uint64_t(self.object->fields.at(0).number);
}
bool equal(double a, double b) { return std::abs(a - b) < 0.0001; }
} // namespace

int main() {
  try {
    using namespace Amagami::Vita;
    VitaVmBridge bridge;

    // open-amagami docs/technical/objects.md: mode 1 is fast-first
    // (decelerating) and mode 2 is slow-first (accelerating). Both must
    // reach the exact target, including fractional-frame updates.
    SpriteTween fast, slow, linear;
    fast.start({{8, 0, 0}}, 1, 1, 8);
    slow.start({{8, 0, 0}}, 1, 2, 8);
    linear.start({{8, 0, 0}}, 1, 0, 8);
    fast.step(2);
    slow.step(2);
    linear.step(2);
    expect(equal(fast.current[0], 3.5) &&
               equal(slow.current[0], 0.5) &&
               equal(linear.current[0], 2.0),
           "Vita native interpolation modes follow reference quadratic curves");
    fast.step(6);
    slow.step(6);
    linear.step(6);
    expect(equal(fast.current[0], 8) &&
               equal(slow.current[0], 8) &&
               equal(linear.current[0], 8),
           "All Vita tween modes land exactly on the final target");

    auto sprite = VmValue::ref(std::make_shared<Amagami::VmObject>());
    call(bridge, "Sprite", sprite, 1, {n(0), n(0), n(10)});
    const uint64_t spriteId = uid(sprite);

    // Native #15: position amplitude (x,y), mode, frequency(x,y), mode,
    // duration. In the original Vita implementation BOTH waves affect Y.
    call(bridge, "Sprite", sprite, 15,
         {n(3), n(8), n(0), n(15), n(15), n(0), n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(spriteId).position[0], 0) &&
               equal(bridge.sprites().at(spriteId).position[1], 11),
           "Position vibration sums both oscillators into Y, not X");
    bridge.tick(1);
    expect(equal(bridge.sprites().at(spriteId).position[1], 0),
           "Vibration crosses the zero point at half the period");
    bridge.tick(1);
    expect(equal(bridge.sprites().at(spriteId).position[1], -11),
           "Vibration oscillates below the base position");
    bridge.tick(1);
    expect(equal(bridge.sprites().at(spriteId).position[1], 0),
           "Vibration wraps phase at exactly 60 / frequency frames");

    // Original Sprite #14 has three light amplitudes and ONE frequency.
    call(bridge, "Sprite", sprite, 14,
         {n(.25), n(.5), n(.75), n(0), n(15), n(0), n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(spriteId).light[0], 1.25) &&
               equal(bridge.sprites().at(spriteId).light[1], 1.5) &&
               equal(bridge.sprites().at(spriteId).light[2], 1.75),
           "RGB light waves broadcast their single native frequency");

    // Alpha vibration changes output alpha, and pause freezes phase.
    call(bridge, "Sprite", sprite, 13, {n(.5), n(0), n(15), n(0), n(0)});
    bridge.tick(1);
    const auto alpha = bridge.sprites().at(spriteId).alpha;
    expect(equal(alpha, 1.5), "Native alpha vibration updates rendered alpha");
    call(bridge, "Sprite", sprite, 18, {n(1)});
    bridge.tick(4);
    expect(equal(bridge.sprites().at(spriteId).alpha, alpha),
           "Paused native sprite freezes vibratory phase");
    call(bridge, "Sprite", sprite, 18, {n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(spriteId).alpha, 1.0),
           "Unpaused sprite advances vibration phase");

    // FontChar position/zoom and BG_Picture alpha/zoom call the same
    // original vibrate primitive signatures; skip snaps the tween targets.
    auto font = VmValue::ref(std::make_shared<Amagami::VmObject>());
    call(bridge, "FontChar", font, 0,
         {n(0x829f), n(0), n(11), n(0), n(0), n(1), n(1)});
    const uint64_t fontId = uid(font);
    call(bridge, "FontChar", font, 8,
         {n(0), n(7), n(0), n(15), n(15), n(0), n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(fontId).position[1], 7),
           "FontChar vibrates its glyph's position without rerasterization");
    call(bridge, "FontChar", font, 9,
         {n(.2), n(.2), n(0), n(15), n(15), n(0), n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(fontId).zoom[0], 1.2),
           "FontChar zoom vibration multiplies the base scale");

    auto background = VmValue::ref(std::make_shared<Amagami::VmObject>());
    bridge.setNamedPictureSizeResolver(
        [](const std::string &)
            -> std::optional<std::pair<uint32_t, uint32_t>> {
          return std::make_pair(uint32_t(960), uint32_t(720));
        });
    call(bridge, "BG_Picture", background, 1, {n(122), n(1), n(0), n(0)});
    const uint64_t bgId = uid(background);
    call(bridge, "BG_Picture", background, 11,
         {n(.1), n(0), n(15), n(0), n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(bgId).alpha, 1.1),
           "BG_Picture supports alpha vibration via original SCF number");
    call(bridge, "BG_Picture", background, 13,
         {n(.5), n(.5), n(0), n(15), n(15), n(0), n(0)});
    bridge.tick(1);
    expect(equal(bridge.sprites().at(bgId).zoom[0], 1.5),
           "BG_Picture zoom vibration multiplies the base scale");

    // Invalid argument count/negative duration must fail WITHOUT changing
    // either amplitude or frequency of the live object's channel.
    const auto before = bridge.sprites().at(bgId).tweens[0];
    bool rejected = false;
    try {
      call(bridge, "BG_Picture", background, 11,
           {n(.5), n(0), n(15), n(0), n(-1)});
    } catch (const std::exception &) {
      rejected = true;
    }
    expect(rejected, "Reject negative vibration duration");
    expect(bridge.sprites().at(bgId).tweens[0][1].target == before[1].target &&
               bridge.sprites().at(bgId).tweens[0][2].target ==
                   before[2].target,
           "Invalid vibration must not partially mutate visual tween state");
    rejected = false;
    try {
      call(bridge, "Sprite", sprite, 14, {n(1), n(2)});
    } catch (const std::exception &) {
      rejected = true;
    }
    expect(rejected, "Reject incorrect RGB vibration native arity");

    std::cout << "Vita Sprite/FontChar/BG_Picture waveform tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
