#pragma once
#include <ZeroNoTsukaima/Catalog.hpp>
#include <ZeroNoTsukaima/Disc.hpp>
#include <ZeroNoTsukaima/Tim2.hpp>

#include <map>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// Scene scripts straight from the disc image: SYSTEM entry 32 (SCENES.NUT)
// gives the script names, SCENE_ID entry i is the compiled scene i.
class SceneSource {
public:
  bool Open(const std::string &discPath, std::string *error);
  const std::vector<std::string> &Names() const { return names_; }
  bool LoadScene(size_t index, std::vector<uint8_t> *out) const;
  // SCENEDAT picture (TIM2) by index; names are resolved through Catalog().
  bool LoadPicture(int index, Image *out) const;
  const ImageCatalog &Catalog() const { return catalog_; }
  // Raw SPU-ADPCM streams: VOICE_ID (one per spoken line) and SOUND_ID (BGM / effects).
  bool LoadVoice(size_t index, std::vector<uint8_t> *out) const;
  bool LoadSound(size_t index, std::vector<uint8_t> *out) const;

  // Archive entry names from the *.LST tables (NORMAL entries 7..11), keyed by
  // upper-case base name without extension ("TITLE_TITLEBG", "BGMA01"); -1 if unknown.
  int NormalIndex(const std::string &name) const { return Lookup(normalNames_, name); }
  int ScenedatIndex(const std::string &name) const { return Lookup(scenedatNames_, name); }
  int SoundIndex(const std::string &name) const { return Lookup(soundNames_, name); }
  // NORMAL picture (system UI: title, menus, message window) by index.
  bool LoadNormalPicture(int index, Image *out) const;
  // SYSVOICES.NUT (SYSTEM #36): system voice name ("RU9Y0005") -> VOICE_ID index.
  int SystemVoice(const std::string &name) const { return Lookup(systemVoices_, name); }
  // MPEG-PS movie number n of the NORMAL #247 table (0 = ZERO2OP.PSS, the opening).
  bool LoadMovie(int number, std::vector<uint8_t> *out) const;

private:
  static int Lookup(const std::map<std::string, int> &table, const std::string &name);
  bool ReadEntry(const Disc::Entry &bin, const std::vector<ArchiveEntry> &index, size_t i,
                 std::vector<uint8_t> *out) const;

  Disc disc_;
  const Disc::Entry *pictureBin_ = nullptr;
  std::vector<ArchiveEntry> pictureIndex_;
  ImageCatalog catalog_;
  const Disc::Entry *sceneBin_ = nullptr;
  std::vector<ArchiveEntry> sceneIndex_;
  const Disc::Entry *voiceBin_ = nullptr, *soundBin_ = nullptr;
  std::vector<ArchiveEntry> voiceIndex_, soundIndex_;
  std::vector<std::string> names_;
  const Disc::Entry *normalBin_ = nullptr;
  std::vector<ArchiveEntry> normalIndex_;
  std::map<std::string, int> normalNames_, scenedatNames_, soundNames_, systemVoices_;
  std::vector<std::string> movies_;
};

// Parses a *.LST name table: "[NAME]" header, one archive path per line, "[END]";
// "//" lines are comments, not entries. Keys are upper-case base names without extension.
std::map<std::string, int> ParseNameList(const std::vector<uint8_t> &text);

} // namespace ZeroNoTsukaima
