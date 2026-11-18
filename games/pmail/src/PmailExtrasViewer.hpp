#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <map>
#include <string>
#include <vector>

namespace Kotonoha {
class Engine;
}

namespace Kotonoha::PmailPort {

class PmailGame;

/**
 * Native omake viewers from pmail.exe:
 *   sys 208 (0x41d810) MUSIC MODE
 *   sys 209 (0x405390) CG MODE
 *   sys 210 (0x42a4c0) KAIROKU MODE
 *
 * The original composes CG / KAIROKU / MUSIC screens from GRAPHIC.DAT and
 * hit-tests the tabs, thumbnail grid, page arrows and player buttons through
 * CGMODE00.MAP / MUSMODE00.MAP. Only identifier names come from the retail
 * tables; the artwork and music remain external game assets.
 */
class PmailExtrasViewer {
public:
  enum class HitKind {
    None,
    Cell,
    Category,
    PrevPage,
    NextPage,
    Exit,
    Track,
    PrevTrack,
    Play,
    Stop,
    NextTrack,
    Repeat
  };
  struct Hit {
    HitKind kind = HitKind::None;
    int value = 0;
  };

  PmailExtrasViewer(Kotonoha::Engine &engine, PmailGame &game,
                    Kotonoha::Providers::SnowAssets &assets);

  Hit hitTest(float x, float y) const;
  void render(Kotonoha::RendererRef renderer);

private:
  struct Texture {
    Kotonoha::TexturePtr tex;
    float w = 0;
    float h = 0;
  };

  SDL_Texture *texture(Kotonoha::RendererRef renderer, const std::string &file);
  SDL_Texture *background(Kotonoha::RendererRef renderer);
  bool loadMap(const std::string &file) const;
  void renderGrid(Kotonoha::RendererRef renderer, bool recollection);
  void renderMusic(Kotonoha::RendererRef renderer);
  void renderFullView(Kotonoha::RendererRef renderer);

  Kotonoha::Engine &engine_;
  PmailGame &game_;
  Kotonoha::Providers::SnowAssets &assets_;

  std::map<std::string, Texture> textures_;
  mutable std::vector<uint8_t> map_;
  mutable std::string mapName_;
};

} // namespace Kotonoha::PmailPort
