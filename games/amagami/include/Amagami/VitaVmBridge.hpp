#pragma once

#include <Amagami/Vm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Amagami::Vita {

// This is deliberately a narrow bridge, NOT a Vita gameplay emulator.
// Unimplemented native classes/primitives still fail with the VM location.
struct ViewState {
  double scale = 1.0;
  double originY = 0.0;
};

// Original Vita Tween has three waveform components per channel: base,
// amplitude, frequency. These are stepped in script frames (usually 60 fps).
struct SpriteTween {
  std::array<double, 3> current{{0, 0, 0}};
  std::array<double, 3> target{{0, 0, 0}};
  std::array<double, 3> a{{0, 0, 0}}, b{{0, 0, 0}};
  std::size_t length = 0;
  double remaining = 0;
  void start(const std::array<double, 3> &value, std::size_t n,
             int mode, double frames);
  void step(double frames);
  void finish();
};

// Vita Spin has an absolute angle and an independently tweened angular
// velocity. One full turn is 2 units (angle is measured in multiples of pi).
struct SpinState {
  SpriteTween base, speed;
  double turned = 0.0;
  void set(double value, int mode, double frames, int component);
  void step(double frames);
  double value() const { return base.current[0] + turned; }
};

// Records screen objects created by the original Vita Sprite SCF primitives.
// The renderer can resolve a sprite via VitaGraphicsAssets::uiSurface using
// texture/variant and apply rect/layer without reinterpreting the VM bytecode.
struct SpriteState {
  uint64_t uid = 0;
  uint32_t texture = 0, variant = 0;
  // Empty for a Sprite atlas texture. BG_Picture uses the original GRAPH.CPK
  // filename (e.g. bg/122_10.gxt) and the world camera transform.
  std::string assetName;
  bool worldView = false;
  // FontChar/FontCharEx (standard atlas) and FontChar2 (alternate atlas).
  // The SCF keeps a Shift-JIS code and a raw palette selector, not UTF-8.
  bool fontGlyph = false;
  unsigned fontAtlas = 0;
  uint16_t characterCode = 0;
  int fontPalette = 0;
  int64_t layer = 0;
  uint64_t order = 0;
  std::array<double, 4> rect{{0, 0, 0, 0}};
  bool hasImage = false;
  bool paused = false;
  // Current visual values, updated by VitaVmBridge::tick().
  double alpha = 1.0;
  double angle = 0.0; // Multiplier of pi; positive rotates counterclockwise.
  SpinState spin;
  std::array<double, 3> light{{1, 1, 1}};
  std::array<double, 2> position{{0, 0}}, zoom{{1, 1}};
  // channel order: alpha, light, position, zoom; component 0 base,
  // 1 amplitude, 2 frequency
  std::array<std::array<SpriteTween, 3>, 4> tweens{};
  std::array<double, 3> posPhase{{0, 0, 0}};
  std::array<std::array<double, 3>, 4> wavePhase{};
  void refresh();
};

// Handles the implemented Vita K2X_Script, Sprite, BG_Picture and FontChar
// native subset. The scene can be rendered through VitaSpriteRenderer;
// remaining unsupported natives are never swallowed.
class VitaVmBridge {
public:
  // Optional provider-backed resolver; no retail graphics are built in.
  // Nullopt = missing/unknown texture; the full-screen texture zero needs no
  // image. Resolvers can call VitaGraphicsAssets::uiSurface(id, variant).
  using TextureSize = std::function<
      std::optional<std::pair<uint32_t, uint32_t>>(uint32_t, uint32_t)>;
  void setTextureSizeResolver(TextureSize resolver) {
    textureSize_ = std::move(resolver);
  }
  using NamedPictureSize =
      std::function<std::optional<std::pair<uint32_t, uint32_t>>(
          const std::string &)>;
  void setNamedPictureSizeResolver(NamedPictureSize resolver) {
    namedPictureSize_ = std::move(resolver);
  }
  // Vita K2X_Script.loadClass loads a scene only when the script asks.
  // This callback normally forwards to Vm::loadClass after the CPK lookup.
  using ClassLoader = std::function<bool(const std::string &)>;
  void setClassLoader(ClassLoader loader) { classLoader_ = std::move(loader); }
  const ViewState &view() const { return view_; }
  uint64_t frameCount() const { return uint64_t(frameCount_); }
  const std::map<uint64_t, SpriteState> &sprites() const { return sprites_; }
  // Ingest one hardware/input frame. Buttons use the PS2-style physical
  // positions in the port (e.g. circle=32, up=4096); system Vita SCFs read
  // SceCtrl's byte-swapped mask. Call this once per presented frame.
  void inputFrame(uint32_t heldButtons,
                  std::array<int, 4> analog = {{0, 0, 0, 0}});
  uint32_t vitaButtonsHeld() const;
  uint32_t vitaButtonsPressed() const;
  uint32_t vitaButtonsRapid() const;
  // Advance at the same frame cadence as SCF animation commands.
  // Scene snapshots stay valid between ticks; pause freezes individual sprites.
  void tick(double frames);
  std::optional<VmValue> primitive(const std::string &owner,
                                   const std::string &method,
                                   bool classMethod, const VmValue &receiver,
                                   unsigned number,
                                   const std::vector<VmValue> &arguments,
                                   bool ps2Layout = false);

private:
  uint64_t newSprite(uint32_t texture, uint32_t variant, int64_t layer);
  void setTexture(SpriteState &sprite, uint32_t texture, uint32_t variant);
  ViewState view_{};
  ClassLoader classLoader_;
  double frameCount_ = 0.0;
  std::map<uint64_t, SpriteState> sprites_;
  uint64_t nextUid_ = 1, nextOrder_ = 0;
  TextureSize textureSize_;
  NamedPictureSize namedPictureSize_;
  uint32_t padHeld_ = 0, padPressed_ = 0, padRapid_ = 0;
  unsigned rapidCount_ = 0;
  std::array<int, 4> padAnalog_{{0, 0, 0, 0}};
};

} // namespace Amagami::Vita
