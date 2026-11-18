#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Kotonoha {
class Engine;
}

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * Native "omake" extras menu from pmail.exe:0x41e9f0.
 *
 * The original composes OMAMENU00.CRGB at 800x600, uses OMAMENU00.MAP as the
 * hit-test mask for the four options, and overlays one of OMAMENU01a..d.CHAR
 * while the pointer is over that option. The CHAR files carry their own
 * placement inside the 800x600 canvas (decoded by SnowAssets::Sprite), so the
 * selected item is drawn exactly where the original stores it.
 */
class PmailExtrasMenu {
public:
  PmailExtrasMenu(Kotonoha::Engine &engine, PmailGame &game,
                  Kotonoha::Providers::SnowAssets &assets);

  // Returns 0 outside every item, otherwise 1..4.
  int hitTest(float x, float y) const;
  void render(Kotonoha::RendererRef renderer);

private:
  struct Texture {
    Kotonoha::TexturePtr tex;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
  };

  SDL_Texture *loadTexture(Kotonoha::RendererRef renderer,
                           const std::string &key, const std::string &file);
  bool loadMap() const;

  Kotonoha::Engine &engine_;
  PmailGame &game_;
  Kotonoha::Providers::SnowAssets &assets_;

  std::map<std::string, Texture> textures_;
  mutable std::vector<uint8_t> map_;
  mutable bool mapLoaded_ = false;
};

} // namespace Kotonoha::PmailPort
