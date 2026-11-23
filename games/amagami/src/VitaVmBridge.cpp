#include <Amagami/VitaVmBridge.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

void SpriteTween::start(const std::array<double, 3> &values, std::size_t n,
                        int mode, double frames) {
  if (n == 0 || n > 3 || !std::isfinite(frames) || frames < 0 ||
      frames > 360000)
    throw std::runtime_error("Amagami Vita: invalid animation duration");
  for (std::size_t i = 0; i < n; ++i)
    if (!std::isfinite(values[i]) || std::abs(values[i]) > 1000000)
      throw std::runtime_error("Amagami Vita: invalid tween target");
  length = n;
  target = values;
  remaining = frames;
  if (frames == 0) {
    current = target;
    return;
  }
  for (std::size_t i = 0; i < n; ++i) {
    const double d = target[i] - current[i];
    if (mode == 1) { // Fast start, slowing toward the target (Vita mode 1).
      a[i] = d / (frames * frames);
      b[i] = 0;
    } else if (mode == 2) { // Slow start, accelerating (Vita mode 2).
      a[i] = -d / (frames * frames);
      b[i] = 2 * d / frames;
    } else { // Linear, including native unrecognized modes.
      a[i] = 0;
      b[i] = d / frames;
    }
  }
}
void SpriteTween::step(double frames) {
  if (remaining <= 0)
    return;
  remaining = std::max(0.0, remaining - frames);
  const double r = remaining;
  for (std::size_t i = 0; i < length; ++i)
    current[i] = target[i] - (a[i] * r * r + b[i] * r);
}
void SpriteTween::finish() {
  remaining = 0;
  current = target;
}

void SpinState::set(double value, int mode, double frames, int component) {
  if (component != 0 && component != 1)
    throw std::runtime_error("Amagami Vita: invalid spin component");
  std::array<double, 3> target{{value, 0, 0}};
  SpriteTween next = component == 0 ? base : speed;
  next.start(target, 1, mode, frames); // Validate before changing either field.
  if (component == 1) {
    speed = next;
    return;
  }
  // The Vita spin absolute-angle tween inherits the running angular
  // velocity as its initial slope; setting the absolute angle then stops the
  // velocity and restarts the accumulated-turn counter (open-amagami Spin).
  if (frames > 0 && (mode == 1 || mode == 2)) {
    const double initialSlope = speed.current[0] / 30.0;
    next.a[0] += (mode == 1 ? -initialSlope : initialSlope) / frames;
  }
  base = next;
  speed.start({{0, 0, 0}}, 1, 0, 0);
  turned = 0;
}
void SpinState::step(double frames) {
  speed.step(frames);
  const double velocity = speed.current[0];
  turned += frames * velocity / 30.0;
  if (turned >= 2.0 || turned <= -2.0)
    turned = std::fmod(turned, 2.0);
  base.step(frames);
}

void SpriteState::refresh() {
  const auto value = [this](std::size_t group, std::size_t i) -> double {
    const auto &base = tweens[group][0].current;
    const auto &amp = tweens[group][1].current;
    const auto &freq = tweens[group][2].current;
    const double wave = std::sin(wavePhase[group][i] * freq[i] *
                                 3.14159265358979323846 / 30.0) *
                        amp[i];
    if (group == 3) // Zoom's fractional waves have multiplicative scaling.
      return wave >= 0 ? base[i] * (1.0 + wave) : base[i] / (1.0 - wave);
    return base[i] + wave;
  };
  alpha = value(0, 0);
  angle = spin.value();
  for (std::size_t i = 0; i < 3; ++i)
    light[i] = value(1, i);
  // Vita character/sprite position waves affect Y, both oscillators.
  position[0] = tweens[2][0].current[0];
  position[1] = tweens[2][0].current[1];
  for (std::size_t i = 0; i < 2; ++i) {
    const auto &amp = tweens[2][1].current;
    const auto &freq = tweens[2][2].current;
    position[1] +=
        std::sin(wavePhase[2][i] * freq[i] * 3.14159265358979323846 / 30.0) *
        amp[i];
    zoom[i] = value(3, i);
  }
}
// Console scripts read SceCtrl positions rather than the host's PS2 layout.
// L/R are also exposed in the Vita trigger bits, matching the original port.
namespace {
uint32_t sceButtons(uint32_t physical) {
  const uint32_t bytes = ((physical & 0xffu) << 8) | ((physical >> 8) & 0xffu);
  return bytes | ((physical & 4u) ? 0x100u : 0u) |
         ((physical & 8u) ? 0x200u : 0u);
}
} // namespace

void VitaVmBridge::inputFrame(uint32_t heldButtons, std::array<int, 4> analog) {
  const uint32_t previous = padHeld_;
  padHeld_ = heldButtons & 0xffffu;
  padPressed_ = padHeld_ & ~previous;
  if (!padHeld_ || padHeld_ != previous)
    rapidCount_ = 0;
  else
    rapidCount_ = rapidCount_ < 20
                      ? rapidCount_ + 1
                      : (rapidCount_ + 1 >= 25 ? 20 : rapidCount_ + 1);
  padRapid_ = padPressed_ | (rapidCount_ == 20 ? padHeld_ : 0u);
  for (std::size_t i = 0; i < 4; ++i)
    padAnalog_[i] = std::clamp(analog[i], -128, 127);
}
uint32_t VitaVmBridge::vitaButtonsHeld() const { return sceButtons(padHeld_); }
uint32_t VitaVmBridge::vitaButtonsPressed() const {
  return sceButtons(padPressed_);
}
uint32_t VitaVmBridge::vitaButtonsRapid() const {
  return sceButtons(padRapid_);
}

void VitaVmBridge::tick(double frames) {
  if (!std::isfinite(frames) || frames < 0 || frames > 360000)
    throw std::runtime_error("Amagami Vita: invalid scene time delta");
  // The native K2X_Script frame clock keeps running even on an empty scene.
  frameCount_ += frames;
  for (auto &[uid, sprite] : sprites_) {
    if (sprite.paused)
      continue;
    for (std::size_t group = 0; group < 4; ++group) {
      for (auto &component : sprite.tweens[group])
        component.step(frames);
      const auto &freq = sprite.tweens[group][2].current;
      for (std::size_t i = 0; i < sprite.tweens[group][0].length; ++i) {
        if (freq[i] <= 0) {
          sprite.wavePhase[group][i] = 0;
        } else {
          sprite.wavePhase[group][i] =
              std::fmod(sprite.wavePhase[group][i] + frames, 60.0 / freq[i]);
        }
      }
    }
    sprite.spin.step(frames);
    sprite.refresh();
  }
}

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
      sprite.rect[2] =
          dimensions->first; // Source texture dimensions in texels.
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
  // Native base channels start at alpha=1, white light, centre 0, zoom 1.
  sprite.tweens[0][0].start({{1, 0, 0}}, 1, 0, 0);
  sprite.tweens[1][0].start({{1, 1, 1}}, 3, 0, 0);
  sprite.tweens[2][0].start({{0, 0, 0}}, 2, 0, 0);
  sprite.tweens[3][0].start({{1, 1, 0}}, 2, 0, 0);
  for (std::size_t group = 0; group < 4; ++group) {
    const std::size_t n = sprite.tweens[group][0].length;
    sprite.tweens[group][1].start({{0, 0, 0}}, n, 0, 0);
    sprite.tweens[group][2].start({{0, 0, 0}}, n, 0, 0);
  }
  sprite.refresh();
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
                        unsigned number, const std::vector<VmValue> &args,
                        bool ps2Layout) {
  // The original native vibrate() sets two Tween components (amplitude and
  // frequency) of an existing visual channel. RGB light uses one shared
  // frequency; the other channels have one frequency per amplitude.
  auto vibrate = [&](std::size_t group, std::size_t count,
                     std::size_t frequencyCount) -> std::optional<VmValue> {
    expectCount(args, count + frequencyCount + 3);
    std::array<double, 3> amplitude{{0, 0, 0}};
    std::array<double, 3> frequency{{0, 0, 0}};
    for (std::size_t i = 0; i < count; ++i)
      amplitude[i] = valueNumber(args[i]);
    const int64_t amplitudeMode = argument(args, count);
    for (std::size_t i = 0; i < count; ++i)
      frequency[i] =
          valueNumber(args[count + 1 + (frequencyCount == 1 ? 0 : i)]);
    const int64_t frequencyMode = argument(args, count + 1 + frequencyCount);
    const double duration = valueNumber(args[count + 2 + frequencyCount]);

    // Perform both validations on temporaries before mutating a live sprite.
    // Invalid values must not leave a half-updated animation in the scene.
    SpriteTween ampCheck, frequencyCheck;
    ampCheck.start(amplitude, count, int(amplitudeMode), duration);
    frequencyCheck.start(frequency, count, int(frequencyMode), duration);
    auto &fields = spriteFields(receiver);
    if (fields.empty() || fields[0].kind != VmValue::Number ||
        !std::isfinite(fields[0].number) || fields[0].number <= 0 ||
        fields[0].number >= 1000000 ||
        std::trunc(fields[0].number) != fields[0].number)
      return receiver;
    const auto it = sprites_.find(uint64_t(fields[0].number));
    if (it == sprites_.end())
      return receiver;
    auto &channels = it->second.tweens[group];
    channels[1].start(amplitude, count, int(amplitudeMode), duration);
    channels[2].start(frequency, count, int(frequencyMode), duration);
    it->second.refresh();
    return receiver;
  };

  // Vita FontChar native wrappers. FontCharEx inherits the standard font;
  // FontChar2 uses the alternate "im" atlas. Each object is a real scene
  // graphic with inherited alpha/position/zoom channels and layer ordering.
  if (owner == "FontChar" || owner == "FontCharEx" || owner == "FontChar2") {
    if (classMethod)
      return std::nullopt;
    if (number == 0) { // new(code, palette, layer, x, y, sx, sy)
      expectCount(args, 7);
      const int64_t code = argument(args, 0);
      const int64_t palette = argument(args, 1);
      const int64_t layer = argument(args, 2);
      if (code < 0 || code > 0xffff || palette < -128 || palette > 128)
        throw std::runtime_error("Amagami Vita: invalid FontChar glyph");
      const double x = valueNumber(args[3]), y = valueNumber(args[4]);
      const double sx = valueNumber(args[5]), sy = valueNumber(args[6]);
      if (sx < 0 || sy < 0 || sx > 64 || sy > 64)
        throw std::runtime_error("Amagami Vita: invalid FontChar zoom");
      auto &fields = spriteFields(receiver);
      const uint64_t uid = newSprite(0, 0, layer);
      auto &picture = sprites_.at(uid);
      picture.fontGlyph = true;
      picture.fontAtlas = owner == "FontChar2" ? 1u : 0u;
      picture.characterCode = uint16_t(code);
      picture.fontPalette = int(palette);
      picture.hasImage = true;
      picture.rect = {{0, 0, 36, 42}};
      picture.tweens[2][0].start({{x, y, 0}}, 2, 0, 0);
      picture.tweens[3][0].start({{sx, sy, 0}}, 2, 0, 0);
      picture.refresh();
      if (fields.empty())
        fields.resize(1);
      fields[0] = VmValue::num(uid);
      return receiver;
    }
    if (owner == "FontChar2" && number == 1)
      return primitive("Sprite", method, false, receiver, 6, args);
    if (owner == "FontChar" && number == 6) { // setChar(code)
      expectCount(args, 1);
      const int64_t code = argument(args, 0);
      if (code < 0 || code > 0xffff)
        throw std::runtime_error("Amagami Vita: invalid FontChar code");
      auto &fields = spriteFields(receiver);
      if (!fields.empty() && fields[0].kind == VmValue::Number &&
          std::isfinite(fields[0].number) && fields[0].number > 0 &&
          fields[0].number < 1000000 &&
          std::trunc(fields[0].number) == fields[0].number) {
        auto it = sprites_.find(uint64_t(fields[0].number));
        if (it != sprites_.end() && it->second.fontGlyph)
          it->second.characterCode = uint16_t(code);
      }
      return receiver;
    }
    if (owner == "FontChar" && number >= 7 && number <= 9) {
      if (number == 7)
        return vibrate(0, 1, 1); // alpha
      if (number == 8)
        return vibrate(2, 2, 2); // position
      return vibrate(3, 2, 2);   // zoom
    }
    if (owner == "FontChar") {
      // The original FontChar animation primitives change only base
      // channels; BG_Picture has the exact same base-tween signatures.
      switch (number) {
      case 1:
        return primitive("Sprite", method, false, receiver, 3, args);
      case 2:
        return primitive("Sprite", method, false, receiver, 7, args);
      case 3:
        return primitive("BG_Picture", method, false, receiver, 8, args);
      case 4:
        return primitive("BG_Picture", method, false, receiver, 9, args);
      case 5:
        return primitive("BG_Picture", method, false, receiver, 10, args);
      case 10:
        return primitive("BG_Picture", method, false, receiver, 7, args);
      default:
        break; // Remaining font primitives are not implemented.
      }
    }
    return std::nullopt;
  }

  // BG_Picture uses the same visual channels as Sprite but references a
  // GRAPH.CPK picture and follows the world view. Primitive numbering comes
  // from the original Vita system classes, not the PS2 graphics methods.
  if (owner == "BG_Picture") {
    if (number == 0 || number == 1) {
      expectCount(args, number == 0 ? 3 : 4);
      const int64_t place = argument(args, 0);
      const int64_t scene = argument(args, 1);
      const int64_t mobility = argument(args, 2);
      if (place < 0 || place > 999 || scene < 0 || scene > 99 || mobility < 0 ||
          mobility > 99)
        throw std::runtime_error("Amagami Vita: invalid background ID");
      char filename[64]{};
      std::snprintf(filename, sizeof filename, "bg/%03lld_%lld%lld.gxt",
                    (long long)place, (long long)scene, (long long)mobility);
      const auto dimensions =
          namedPictureSize_ ? namedPictureSize_(filename) : std::nullopt;
      if (number == 0) {
        if (!classMethod)
          return std::nullopt;
        auto exists = VmValue::num(dimensions ? 1 : 0);
        exists.kind = VmValue::Boolean;
        return exists;
      }
      if (classMethod)
        return std::nullopt;
      const int64_t layer = argument(args, 3);
      if (dimensions && (!dimensions->first || !dimensions->second ||
                         dimensions->first > 4096 || dimensions->second > 4096))
        throw std::runtime_error("Amagami Vita: invalid background dimensions");
      // Create transactionally: an unavailable member is represented but never
      // painted as texture-zero black bars.
      const auto uid = newSprite(0, 0, layer);
      auto &picture = sprites_.at(uid);
      picture.assetName = filename;
      picture.worldView = true;
      picture.hasImage = bool(dimensions);
      picture.rect = {{0, 0, dimensions ? double(dimensions->first) : 960,
                       dimensions ? double(dimensions->second) : 544}};
      auto &fields = spriteFields(receiver);
      if (fields.empty())
        fields.resize(1);
      fields[0] = VmValue::num(uid);
      return receiver;
    }
    if (classMethod)
      return std::nullopt;
    // Shared lifecycle operations and base-channel tweens.
    if (number >= 7 && number <= 10) {
      const std::size_t group = number == 7   ? 1
                                : number == 8 ? 0
                                : number == 9 ? 2
                                              : 3;
      const std::size_t n = group == 1 ? 3 : group == 0 ? 1 : 2;
      expectCount(args, n + 2); // values, mode, duration; always component 0
      std::array<double, 3> target{{0, 0, 0}};
      for (std::size_t i = 0; i < n; ++i)
        target[i] = valueNumber(args[i]);
      const auto mode = argument(args, n);
      const double time = valueNumber(args[n + 1]);
      // Validate even when the visual object was already destroyed.
      SpriteTween verified;
      verified.start(target, n, int(mode), time);
      auto &fields = spriteFields(receiver);
      if (fields.empty() || fields[0].kind != VmValue::Number)
        return receiver;
      const double rawUid = fields[0].number;
      if (rawUid <= 0 || rawUid >= 1000000 || std::trunc(rawUid) != rawUid)
        return receiver;
      const auto found = sprites_.find(uint64_t(rawUid));
      if (found != sprites_.end()) {
        found->second.tweens[group][0].start(target, n, int(mode), time);
        found->second.refresh();
      }
      return receiver;
    }
    if (number >= 11 && number <= 13) {
      if (number == 11)
        return vibrate(0, 1, 1);
      if (number == 12)
        return vibrate(2, 2, 2);
      return vibrate(3, 2, 2);
    }
    unsigned shared = 0;
    switch (number) {
    case 2:
      shared = 2;
      break; // clone
    case 3:
      shared = 3;
      break; // destroy
    case 4:
      shared = 7;
      break; // update remaining channel frames
    case 6:
      shared = 6;
      break; // setLayer
    case 14:
      shared = 12;
      break; // original spin (angle or speed)
    case 15:
      shared = 17;
      break; // finish tweens
    case 16:
      shared = 18;
      break; // pause
    default:
      return std::nullopt; // Spin and other natives pending.
    }
    return primitive("Sprite", method, false, receiver, shared, args);
  }

  // Vita MemoryCard bootstrap queries. There is no native save backend in
  // this experimental adapter yet: report an empty but usable card, rather
  // than a false "save succeeded". Read/write operations 3..8 remain
  // deliberately unimplemented and must surface an SCF location error.
  if (owner == "MemoryCard" && !classMethod) {
    if (number == 0 || number == 1) {
      expectCount(args, 0);
      if (number == 0) {
        auto changed = VmValue::num(0);
        changed.kind = VmValue::Boolean;
        return changed; // isChanged: no external card changes.
      }
      return VmValue::num(6); // status: usable, no saved data yet.
    }
    if (number == 2) { // checkData(quiet): no pending card error.
      expectCount(args, 1);
      return receiver;
    }
    if (number == 9) { // No corrupted data to delete.
      expectCount(args, 0);
      return VmValue::num(0);
    }
    if (number == 10) { // No system error dialog without a card backend.
      return receiver;
    }
    if (number == 11) { // No pending system message.
      expectCount(args, 0);
      auto busy = VmValue::num(0);
      busy.kind = VmValue::Boolean;
      return busy;
    }
    return std::nullopt;
  }

  // Vita Voice.length(channel): without an audio backend no voice sample is
  // playing, hence there is no remaining duration. Playback itself stays
  // unsupported instead of reporting fake audio success.
  if (owner == "Voice" && !classMethod && number == 5) {
    expectCount(args, 1);
    (void)argument(args, 0);
    return VmValue::num(0);
  }

  // ControlPad primitives (Vita system image). The caller supplies one input
  // frame through inputFrame(); these getters must not poll or mutate SDL.
  // The live adapter distinguishes callers in the system image from
  // scenario classes that still test the original PS2 pad button bits.
  if (owner == "ControlPad") {
    if (number == 0 || number == 1 || number == 2 || number == 4) {
      expectCount(args, 0);
      const uint32_t mask = number == 4   ? 0u
                            : ps2Layout   ? (number == 0   ? padHeld_
                                             : number == 1 ? padPressed_
                                                           : padRapid_)
                            : number == 0 ? vitaButtonsHeld()
                            : number == 1 ? vitaButtonsPressed()
                                          : vitaButtonsRapid();
      return VmValue::num(mask);
    }
    if (number == 3) { // resetRapid()
      expectCount(args, 0);
      rapidCount_ = 0;
      padRapid_ = padPressed_;
      return receiver;
    }
    if (number == 5) { // analog(axis), 0..3 signed, 4/5 stick magnitude
      expectCount(args, 1);
      const int64_t axis = argument(args, 0);
      if (axis >= 0 && axis < 4)
        return VmValue::num(padAnalog_[std::size_t(axis)]);
      if (axis == 4 || axis == 5) {
        const std::size_t p = std::size_t(axis - 4) * 2;
        const double x = padAnalog_[p], y = padAnalog_[p + 1];
        return VmValue::num(std::trunc(std::sqrt(x * x + y * y)));
      }
      return VmValue::num(0);
    }
    if (number == 6) { // actPausePrim: vibration (no desktop haptics yet)
      expectCount(args, 2);
      (void)argument(args, 0);
      (void)argument(args, 1);
      return receiver;
    }
    return std::nullopt;
  }

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
        throw std::runtime_error(
            "Amagami Vita: Sprite texture id out of range");
      fields[0] =
          VmValue::num(newSprite(uint32_t(tex), uint32_t(variant), layer));
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
          std::trunc(fields[0].number) == fields[0].number &&
          fields[0].number < 1000000)
        sprites_.erase(uint64_t(fields[0].number));
      fields[0] = VmValue::num(0);
      return receiver;
    }
    uint64_t id = 0;
    if (fields[0].kind == VmValue::Number && std::isfinite(fields[0].number) &&
        fields[0].number > 0 &&
        std::trunc(fields[0].number) == fields[0].number &&
        fields[0].number < 1000000)
      id = uint64_t(fields[0].number);
    auto it = sprites_.find(id);
    // Operations on objects already destroyed are harmless in the Vita engine.
    if (number == 4) { // setTex(texture, variant)
      expectCount(args, 2);
      const int64_t tex = argument(args, 0), variant = argument(args, 1);
      if (tex < 0 || tex >= 337 || variant < 0 || variant > 255)
        throw std::runtime_error(
            "Amagami Vita: Sprite texture id out of range");
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
        for (auto &v : rect)
          v *= texels;
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
    if (number == 7) { // update: report remaining frames per channel.
      expectCount(args, 0);
      if (fields.size() < 5)
        fields.resize(5);
      for (std::size_t group = 0; group < 4; ++group) {
        double remaining = 0;
        if (it != sprites_.end())
          for (const auto &component : it->second.tweens[group])
            remaining = std::max(remaining, component.remaining);
        fields[group + 1] = VmValue::num(std::ceil(remaining));
      }
      return receiver;
    }
    if (number >= 8 && number <= 11) { // light, alpha, position, zoom
      const std::size_t group = number == 8    ? 1
                                : number == 9  ? 0
                                : number == 10 ? 2
                                               : 3;
      const std::size_t n = group == 1 ? 3 : group == 0 ? 1 : 2;
      expectCount(args, n + 3); // values, interpolation mode, time, component
      std::array<double, 3> target{{0, 0, 0}};
      for (std::size_t i = 0; i < n; ++i)
        target[i] = valueNumber(args[i]);
      const int64_t mode = argument(args, n);
      const double time = valueNumber(args[n + 1]);
      const int64_t component = argument(args, n + 2);
      if (component < 0 || component > 2)
        throw std::runtime_error("Amagami Vita: invalid tween component");
      if (it != sprites_.end()) {
        it->second.tweens[group][size_t(component)].start(target, n, int(mode),
                                                          time);
        it->second.refresh();
      }
      return receiver;
    }
    if (number == 12) { // spin(value, mode, duration, component)
      expectCount(args, 4);
      const double value = valueNumber(args[0]);
      const int mode = int(argument(args, 1));
      const double duration = valueNumber(args[2]);
      const int component = int(argument(args, 3));
      SpinState next;
      if (it != sprites_.end())
        next = it->second.spin;
      next.set(value, mode, duration, component);
      if (it != sprites_.end()) {
        it->second.spin = next;
        it->second.refresh();
      }
      return receiver;
    }
    if (number >= 13 && number <= 16) {
      if (number == 13)
        return vibrate(0, 1, 1); // alpha
      if (number == 14)
        return vibrate(1, 3, 1); // RGB light
      if (number == 15)
        return vibrate(2, 2, 2); // position
      return vibrate(3, 2, 2);   // zoom
    }
    if (number == 17) { // skip: snap all in-progress channels to target
      expectCount(args, 0);
      if (it != sprites_.end()) {
        for (auto &channels : it->second.tweens)
          for (auto &component : channels)
            component.finish();
        it->second.refresh();
      }
      return receiver;
    }
    if (number == 18) { // pause(on)
      expectCount(args, 1);
      if (it != sprites_.end())
        it->second.paused =
            args[0].truth() &&
            !(args[0].kind == VmValue::Number && args[0].number == 0);
      return receiver;
    }
    return std::nullopt; // Unknown sprite native method.
  }
  if (owner != "K2X_Script")
    return std::nullopt; // Do not hide unrelated PS2/Vita primitives.
  if (number == 0 && method == "resetPrim") {
    expectCount(args, 0);
    sprites_.clear(); // Original reset drops scene objects; IDs remain unique.
    return receiver;
  }
  // The Vita retains the PS2 instance-side K2X_Script primitive table.
  // 1: disc-change state, 2: explicit scenario class load, 3: ready state,
  // 4: game-frame clock. Vita-only class methods begin at number 5.
  if (!classMethod && number == 1) {
    expectCount(args, 0);
    auto result = VmValue::num(0);
    result.kind = VmValue::Boolean;
    return result;
  }
  if (!classMethod && number == 2) {
    expectCount(args, 1);
    if (args[0].kind != VmValue::Text && args[0].kind != VmValue::Symbol)
      throw std::runtime_error("Amagami Vita: loadClass expects a name");
    const std::string &name = args[0].asText();
    if (name.empty() || name.size() > 256)
      return VmValue{};
    if (classLoader_ && classLoader_(name))
      return VmValue::str(name, VmValue::Class);
    return VmValue{}; // Unknown scenario: original VM returns nil.
  }
  if (!classMethod && number == 3) {
    expectCount(args, 0);
    auto result = VmValue::num(1);
    result.kind = VmValue::Boolean;
    return result;
  }
  if (!classMethod && number == 4) {
    expectCount(args, 0);
    return VmValue::num(double(frameCount()));
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
    for (int value :
         {local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min})
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
