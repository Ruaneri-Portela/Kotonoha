#pragma once
#include <ZeroNoTsukaima/Catalog.hpp>
#include <ZeroNoTsukaima/Disc.hpp>
#include <ZeroNoTsukaima/Tim2.hpp>

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

private:
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
};

} // namespace ZeroNoTsukaima
