#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {
namespace workers {

struct EmbeddedSubtitleResult {
  // False when this FFmpeg build has no usable subtitle decoder. The reason is
  // always populated so callers can log an explicit, actionable message.
  bool decoderAvailable = false;
  // True when at least one subtitle stream was found and converted.
  bool found = false;
  std::string reason;
  std::string script;
};

// Reads an in-memory video/container and converts its embedded subtitle stream
// (ASS/SSA packets or decoded text subtitles) into a standalone ASS script.
// Never throws: unsupported codecs and corrupt input degrade to a result with
// decoderAvailable=false/found=false and a human-readable reason.
EmbeddedSubtitleResult
extractEmbeddedSubtitles(const std::vector<Uint8> &mediaBytes);

} // namespace workers
} // namespace Kotonoha
