#include <Kotonoha/debug/DebugAssetCatalog.hpp>

#include <iostream>
#include <string_view>
#include <vector>

int main() {
  constexpr std::string_view supported[]{
      "voice:73",           "image/../VOICE/1.OGG",
      "bgm:100001",         "se:42",
      "VOICE/100.ogg",      "sound/mystery",
      "music/track",        "Audio/foo",
      "bgm/main.BGM",       "voice/clip.SND",
      "effects/damage.ADX", "sound_effects/fanfare.wav",
      "voices/character/1"};
  for (const auto id : supported) {
    if (!Kotonoha::DebugAssetCatalog::isAudio(id)) {
      std::cerr << "Missing audio ID: " << id << '\n';
      return 1;
    }
  }
  constexpr std::string_view rejected[]{"image:1127",
                                        "scene/script.ks",
                                        "scenedat/12",
                                        "background.png",
                                        "sounds_like_image.png",
                                        "prefix/voice/graphic.png",
                                        "",
                                        "seen170"};
  for (const auto id : rejected) {
    if (Kotonoha::DebugAssetCatalog::isAudio(id)) {
      std::cerr << "Misclassified non-audio ID: " << id << '\n';
      return 1;
    }
  }
  std::cout << "Kotonoha debug asset catalog passed\n";
  return 0;
}
