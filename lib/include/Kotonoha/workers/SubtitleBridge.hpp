// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/components/SubtitleScene.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/workers/AttachedFont.hpp>

namespace Kotonoha::workers {

// Hand the fonts attached to an opened video straight to libass. The media
// worker only collects/bounds the bytes; SubtitleScene registers them with
// ass_add_font on its rasterizer before the next frame is rendered.
inline void transferAttachedFonts(const MediaPlayer &player,
                                  SubtitleScene &subtitles) {
  subtitles.setAttachedFonts(player.attachedFonts());
}

} // namespace Kotonoha::workers
