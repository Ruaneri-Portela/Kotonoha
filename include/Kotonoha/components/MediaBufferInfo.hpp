#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include <string>
#include <vector>

namespace Kotonoha {

// Extra texture attached to a media item (e.g. a mouth-overlay layer).
struct MediaTextureInfo {
  std::string label;
  SDL_Texture *texture = nullptr;
};

// Snapshot of one scene media item, used by the Scene Buffers debug window.
// Times are in milliseconds; audio/video fill played/cached/eof, images only
// provide the texture(s).
struct MediaBufferInfo {
  Uint64 id = 0;
  std::string type;
  std::string channel;
  std::string path;
  Uint64 totalMs = 0;  // full media length, 0 when unknown
  Uint64 playedMs = 0; // elapsed playback time
  Uint64 cachedMs = 0; // buffered time ahead of the playhead
  bool eof = false;    // demuxer reached the end of the file
  SDL_Texture *texture = nullptr;
  std::vector<MediaTextureInfo> overlays;
};

} // namespace Kotonoha
