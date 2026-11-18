#pragma once

#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/ScnRunner.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <set>
#include <string>

namespace Kirikiri {

class EmoteRenderer {
public:
  EmoteRenderer() = default;

  static std::string
  resolveVisualAsset(const Kotonoha::Providers::KirikiriAssets &assets,
                     std::string storage, bool foreground);

  void clearFailedCache() { failedAssets_.clear(); }

  void renderLayer(Kotonoha::RendererRef renderer, Kotonoha::Engine *engine,
                   Kotonoha::Providers::KirikiriAssets &assets,
                   const ScnLayerState &layer, bool fullScreen);

  void renderCharacter(Kotonoha::RendererRef renderer, Kotonoha::Engine *engine,
                       Kotonoha::Providers::KirikiriAssets &assets,
                       const ScnCharacterState &ch);

private:
  std::set<std::string> failedAssets_;
};

} // namespace Kirikiri
