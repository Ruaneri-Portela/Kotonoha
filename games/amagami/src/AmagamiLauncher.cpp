#include "AmagamiLauncher.hpp"
#include <Kotonoha/core/DiscImage.hpp>
namespace Amagami {
AmagamiLauncher::AmagamiLauncher(std::string settingsPath, std::string disc)
    : GameLauncher("Amagami - Launcher", std::move(settingsPath),
                   {"Amagami Japan PS2 disc",
                    PathKind::File,
                    {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}},
                    "disc.txt",
                    "Choose your Amagami disc. The prologue is playable; the "
                    "calendar and later routes are still in development."}) {
  setAudioChannels({"bgm", "voice", "se"});
  loadPath(std::move(disc));
}
bool AmagamiLauncher::validatePath(const std::string &path, std::string &info) {
  Kotonoha::DiscImage disc;
  if (!disc.tryOpen(path, &info))
    return false;
  for (const char *name :
       {"SLPS_259.18", "SCENARIO.ARC", "AMAGAMI.IMG", "GRAPH/GRAPH0.ARC",
        "GRAPH/GRAPH1.ARC", "GRAPH/GRAPH2.ARC", "SOUND/VOICE.AFS",
        "SOUND/MUSIC.AFS"}) {
    if (!disc.find(name)) {
      info = std::string("Amagami Japan disc is missing ") + name;
      return false;
    }
  }
  info = "Amagami Japan (SLPS_259.18), volume '" + disc.volumeLabel() + "'";
  return true;
}
} // namespace Amagami
