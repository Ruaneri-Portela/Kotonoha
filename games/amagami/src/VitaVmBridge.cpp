#include <Amagami/VitaVmBridge.hpp>
#include <cmath>
#include <ctime>
#include <limits>
#include <stdexcept>

namespace Amagami::Vita {
namespace {
double viewArgument(const std::vector<VmValue> &args) {
  if (args.size() != 1 || args[0].kind != VmValue::Number ||
      !std::isfinite(args[0].number))
    throw std::runtime_error("Amagami Vita: invalid screen native argument");
  return args[0].number;
}
// SCF numeric arguments are the same two kinds for literal integers/floats.
double valueNumber(const VmValue &value) {
  if (value.kind != VmValue::Number || !std::isfinite(value.number) ||
      std::abs(value.number) > 1000000)
    throw std::runtime_error("Amagami Vita: invalid sprite numeric argument");
  return value.number;
}
int64_t integerArgument(const VmValue &value) {
  const double n = valueNumber(value);
  if (std::trunc(n) != n)
    throw std::runtime_error("Amagami Vita: fractional sprite integer");
  return int64_t(n);
}
int64_t argument(const std::vector<VmValue> &args, size_t slot) {
  if (slot >= args.size())
    throw std::runtime_error("Amagami Vita: missing sprite native argument");
  return integerArgument(args[slot]);
}
void expectCount(const std::vector<VmValue> &args, size_t expected) {
  if (args.size() != expected)
    throw std::runtime_error("Amagami Vita: wrong sprite native arity");
}
std::vector<VmValue> &spriteFields(const VmValue &self) {
  if (self.kind != VmValue::Object || !self.object)
    throw std::runtime_error("Amagami Vita: sprite primitive needs an object");
  return self.object->fields;
}
} // namespace

void VitaVmBridge::setTexture(SpriteState &sprite, uint32_t tex,
                              uint32_t variant) {
  sprite.texture = tex;
  sprite.variant = variant;
  sprite.hasImage = false;
  sprite.rect = {{0, 0, 0, 0}};
  if (tex == 0) {
    sprite.rect[2] = 480; // Blank full-screen UI object: script units.
    sprite.rect[3] = 272;
  } else if (textureSize_) {
    const auto dimensions = textureSize_(tex, variant);
    if (dimensions) {
      if (!dimensions->first || !dimensions->second ||
          dimensions->first > 4096 || dimensions->second > 4096)
        throw std::runtime_error("Amagami Vita: invalid resolved texture size");
      sprite.hasImage = true;
      sprite.rect[2] = dimensions->first;  // Source texture dimensions in texels.
      sprite.rect[3] = dimensions->second;
    }
  }
}

uint64_t VitaVmBridge::newSprite(uint32_t tex, uint32_t variant,
                                 int64_t layer) {
  if (sprites_.size() >= 16384 || nextUid_ >= 1000000)
    throw std::runtime_error("Amagami Vita: sprite scene object limit");
  SpriteState sprite;
  sprite.uid = nextUid_;
  sprite.layer = layer;
  sprite.order = nextOrder_ + 1;
  setTexture(sprite, tex, variant); // Throws before any scene changes.
  sprites_.emplace(sprite.uid, sprite);
  ++nextUid_;
  ++nextOrder_;
  return sprite.uid;
}

std::optional<VmValue>
VitaVmBridge::primitive(const std::string &owner, const std::string &method,
                        bool classMethod, const VmValue &receiver,
                        unsigned number, const std::vector<VmValue> &args) {
  // Sprite methods are resolved by the SCF class that owns the native
  // wrapper, including calls made through a subclass.
  if (owner == "Sprite" && !classMethod) {
    auto &fields = spriteFields(receiver);
    if (fields.empty())
      fields.resize(1);
    if (number == 1) { // new(texture, variant, layer)
      expectCount(args, 3);
      const int64_t tex = argument(args, 0), variant = argument(args, 1);
      const int64_t layer = argument(args, 2);
      if (tex < 0 || tex >= 337 || variant < 0 || variant > 255)
        throw std::runtime_error("Amagami Vita: Sprite texture id out of range");
      fields[0] = VmValue::num(
          newSprite(uint32_t(tex), uint32_t(variant), layer));
      return receiver;
    }
    if (number == 2) { // attach(sourceUid): clone source graphic
      expectCount(args, 1);
      const uint64_t source = uint64_t(argument(args, 0));
      const auto found = sprites_.find(source);
      if (found == sprites_.end()) {
        fields[0] = VmValue::num(0);
      } else {
        if (sprites_.size() >= 16384 || nextUid_ >= 1000000)
          throw std::runtime_error("Amagami Vita: sprite scene object limit");
        SpriteState clone = found->second;
        clone.uid = nextUid_++;
        clone.order = ++nextOrder_;
        sprites_.emplace(clone.uid, clone);
        fields[0] = VmValue::num(clone.uid);
      }
      return receiver;
    }
    if (number == 3) { // destruct
      expectCount(args, 0);
      if (fields[0].kind == VmValue::Number &&
          std::isfinite(fields[0].number) && fields[0].number > 0 &&
          fields[0].number < 1000000)
        sprites_.erase(uint64_t(fields[0].number));
      fields[0] = VmValue::num(0);
      return receiver;
    }
    uint64_t id = 0;
    if (fields[0].kind == VmValue::Number &&
        std::isfinite(fields[0].number) && fields[0].number > 0 &&
        fields[0].number < 1000000)
      id = uint64_t(fields[0].number);
    auto it = sprites_.find(id);
    // Operations on objects already destroyed are harmless in the Vita engine.
    if (number == 4) { // setTex(texture, variant)
      expectCount(args, 2);
      const int64_t tex = argument(args, 0), variant = argument(args, 1);
      if (tex < 0 || tex >= 337 || variant < 0 || variant > 255)
        throw std::runtime_error("Amagami Vita: Sprite texture id out of range");
      if (it != sprites_.end()) {
        auto candidate = it->second;
        setTexture(candidate, uint32_t(tex), uint32_t(variant));
        it->second = candidate;
      }
      return receiver;
    }
    if (number == 5) { // setTexRect(x, y, w, h), script units
      expectCount(args, 4);
      std::array<double, 4> rect{};
      for (size_t i = 0; i < rect.size(); ++i)
        rect[i] = valueNumber(args[i]);
      if (it != sprites_.end()) {
        const double texels = it->second.hasImage ? 2.0 : 1.0;
        for (auto &v : rect) v *= texels;
        it->second.rect = rect;
      }
      return receiver;
    }
    if (number == 6) { // setLayer(layer)
      expectCount(args, 1);
      const int64_t layer = argument(args, 0);
      if (it != sprites_.end()) {
        it->second.layer = layer;
        it->second.order = ++nextOrder_;
      }
      return receiver;
    }
    if (number == 7) { // update: no active tweens yet
      expectCount(args, 0);
      if (it != sprites_.end() && fields.size() < 5)
        fields.resize(5);
      if (it != sprites_.end()) {
        for (size_t i = 1; i <= 4; ++i)
          fields[i] = VmValue::num(0);
      }
      return receiver;
    }
    if (number == 18) { // pause(on)
      expectCount(args, 1);
      if (it != sprites_.end())
        it->second.paused = args[0].truth();
      return receiver;
    }
    return std::nullopt; // Animation/input primitives not implemented.
  }
  if (owner != "K2X_Script")
    return std::nullopt; // Do not hide unrelated PS2/Vita primitives.
  if (number == 0 && method == "resetPrim") {
    expectCount(args, 0);
    sprites_.clear(); // Original reset drops scene objects; IDs remain unique.
    return receiver;
  }
  if (!classMethod)
    return std::nullopt;

  if (number == 5 && method == "getClock") {
    if (!args.empty())
      throw std::runtime_error("Amagami Vita: getClock arguments");
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &now) != 0)
      throw std::runtime_error("Amagami Vita: getClock failed");
#else
    if (!localtime_r(&now, &local))
      throw std::runtime_error("Amagami Vita: getClock failed");
#endif
    auto object = std::make_shared<VmObject>();
    object->type = "Array";
    for (int value : {local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min})
      object->fields.push_back(VmValue::num(value));
    return VmValue::ref(std::move(object), VmValue::Array);
  }
  if (number == 6 && method == "setScrScl") {
    const double scale = viewArgument(args);
    if (scale <= 0 || scale > 16)
      throw std::runtime_error("Amagami Vita: invalid screen scale");
    view_.scale = scale;
    return receiver;
  }
  if (number == 7 && method == "setScrOrgY") {
    const double y = viewArgument(args);
    if (std::abs(y) > 8192)
      throw std::runtime_error("Amagami Vita: invalid screen origin");
    view_.originY = y;
    return receiver;
  }
  if (number == 8 && method == "unlockTrophy") {
    return receiver; // Platform-neutral: no trophy service is present.
  }
  return std::nullopt; // Unknown Vita primitive: surface a VM error.
}
} // namespace Amagami::Vita
