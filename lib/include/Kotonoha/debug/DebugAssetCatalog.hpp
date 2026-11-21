#pragma once
// Shared, allocation-light asset classification for the Kotonoha debugger.
// Every native engine exposes logical IDs through AssetProvider::list(), not
// engine-specific filesystem paths or archive decoder internals.
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Kotonoha::DebugAssetCatalog {

inline bool isAudio(std::string_view id) {
  // Archive IDs have no extension (e.g. LucaSystem voice:73, BGM:100001).
  std::string lower;
  lower.reserve(id.size());
  for (unsigned char ch : id)
    lower.push_back(static_cast<char>(std::tolower(ch)));
  constexpr std::string_view prefixes[]{
      "voice:", "bgm:", "se:", "audio:", "sound:", "music:",
      "voice/", "bgm/", "se/", "audio/", "sound/", "music/",
      "snd/", "voices/", "sounds/", "musics/"};
  for (const auto prefix : prefixes)
    if (lower.compare(0, prefix.size(), prefix) == 0)
      return true;
  constexpr std::string_view suffixes[]{
      ".wav", ".ogg", ".opus", ".mp3", ".flac", ".m4a", ".aac",
      ".aiff", ".aif", ".nwa", ".adx", ".hca", ".at3", ".vag",
      ".wma", ".snd", ".bgm", ".mid", ".midi", ".acm", ".dwa"};
  for (const auto suffix : suffixes)
    if (lower.size() >= suffix.size() &&
        lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0)
      return true;
  return false;
}

} // namespace Kotonoha::DebugAssetCatalog
