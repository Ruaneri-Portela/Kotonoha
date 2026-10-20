#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include <string>
#include <vector>

namespace Kotonoha {

struct MediaTextureInfo {
  std::string label;
  SDL_Texture *texture = nullptr;
};

struct MediaBufferInfo {
  Uint64 id = 0;
  std::string type;
  std::string channel;
  std::string path;
  Uint64 playedMs = 0;
  Uint64 cachedMs = 0;
  bool eof = false;
  SDL_Texture *texture = nullptr;
  std::vector<MediaTextureInfo> overlays;
};

} // namespace Kotonoha
