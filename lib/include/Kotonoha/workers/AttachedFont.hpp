// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha::workers {

// Neutral payload for a font attached to a media container (e.g. an MKV
// attachment). Produced by the media worker and consumed by libass through
// SubtitleScene; neither side needs to know the other's internals.
struct AttachedFont {
  std::string name;
  std::vector<Uint8> bytes;
};

} // namespace Kotonoha::workers
