#pragma once

#include <Amagami/Vm.hpp>
#include <array>
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

// Records screen objects created by the original Vita Sprite SCF primitives.
// The renderer can resolve a sprite via VitaGraphicsAssets::uiSurface using
// texture/variant and apply rect/layer without reinterpreting the VM bytecode.
struct SpriteState {
  uint64_t uid = 0;
  uint32_t texture = 0, variant = 0;
  int64_t layer = 0;
  uint64_t order = 0;
  std::array<double, 4> rect{{0, 0, 0, 0}};
  bool hasImage = false;
  bool paused = false;
};

// Handles Vita K2X_Script screen primitives and the first Sprite primitives.
// This is a *headless scene model*: the presentation loop and animation
// primitives still need integration; unhandled primitives are never swallowed.
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
  const ViewState &view() const { return view_; }
  const std::map<uint64_t, SpriteState> &sprites() const { return sprites_; }
  std::optional<VmValue> primitive(const std::string &owner,
                                   const std::string &method,
                                   bool classMethod, const VmValue &receiver,
                                   unsigned number,
                                   const std::vector<VmValue> &arguments);

private:
  uint64_t newSprite(uint32_t texture, uint32_t variant, int64_t layer);
  void setTexture(SpriteState &sprite, uint32_t texture, uint32_t variant);
  ViewState view_{};
  std::map<uint64_t, SpriteState> sprites_;
  uint64_t nextUid_ = 1, nextOrder_ = 0;
  TextureSize textureSize_;
};

} // namespace Amagami::Vita
