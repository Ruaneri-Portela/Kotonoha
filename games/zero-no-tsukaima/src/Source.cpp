#include <ZeroNoTsukaima/Lz.hpp>
#include <ZeroNoTsukaima/Scene.hpp>
#include <ZeroNoTsukaima/Source.hpp>

#include <tuple>

namespace ZeroNoTsukaima {

bool SceneSource::ReadEntry(const Disc::Entry &bin, const std::vector<ArchiveEntry> &index, size_t i,
                            std::vector<uint8_t> *out) const {
  if (i >= index.size()) return false;
  std::vector<uint8_t> blob;
  if (!disc_.ReadRange(bin, index[i].offset, index[i].size, &blob)) return false;
  std::vector<uint8_t> unpacked;
  if (Unpack(blob.data(), blob.size(), &unpacked)) *out = std::move(unpacked);
  else *out = std::move(blob);
  return true;
}

bool SceneSource::Open(const std::string &discPath, std::string *error) {
  if (!disc_.Open(discPath, error)) return false;
  const Disc::Entry *sysBin = disc_.Find("SYSTEM.BIN");
  const Disc::Entry *sysHd = disc_.Find("SYSTEM.HD");
  sceneBin_ = disc_.Find("SCENE_ID.BIN");
  const Disc::Entry *sceneHd = disc_.Find("SCENE_ID.HD");
  if (!sysBin || !sysHd || !sceneBin_ || !sceneHd) {
    if (error) *error = "SYSTEM / SCENE_ID archives not found on the disc";
    return false;
  }
  std::vector<uint8_t> hd;
  disc_.Read(*sceneHd, &hd);
  sceneIndex_ = ParseArchiveIndex(hd);
  pictureBin_ = disc_.Find("SCENEDAT.BIN");
  if (const Disc::Entry *h = disc_.Find("SCENEDAT.HD")) {
    std::vector<uint8_t> raw;
    if (pictureBin_ && disc_.Read(*h, &raw)) pictureIndex_ = ParseArchiveIndex(raw);
  }
  voiceBin_ = disc_.Find("VOICE_ID.BIN");
  soundBin_ = disc_.Find("SOUND_ID.BIN");
  for (auto [bin, name, index] : {std::tuple{voiceBin_, "VOICE_ID.HD", &voiceIndex_},
                                  std::tuple{soundBin_, "SOUND_ID.HD", &soundIndex_}}) {
    const Disc::Entry *h = disc_.Find(name);
    std::vector<uint8_t> raw;
    if (bin && h && disc_.Read(*h, &raw)) *index = ParseArchiveIndex(raw);
  }
  hd.clear();
  disc_.Read(*sysHd, &hd);
  const auto sysIndex = ParseArchiveIndex(hd);
  std::vector<uint8_t> nut;
  if (!ReadEntry(*sysBin, sysIndex, 32, &nut) || !ParseSceneNames(nut, &names_)) {
    if (error) *error = "cannot read the scene name table (SYSTEM #32)";
    return false;
  }
  std::vector<uint8_t> env, main;
  if (ReadEntry(*sysBin, sysIndex, 31, &env) && ReadEntry(*sysBin, sysIndex, 35, &main)) {
    std::vector<uint32_t> sizes;
    for (const ArchiveEntry &e : pictureIndex_) sizes.push_back(e.size);
    catalog_.Build(env, main, sizes);
  }
  return true;
}

bool SceneSource::LoadPicture(int index, Image *out) const {
  if (!pictureBin_ || index < 0) return false;
  std::vector<uint8_t> data;
  if (!ReadEntry(*pictureBin_, pictureIndex_, size_t(index), &data)) return false;
  return DecodeTim2(data.data(), data.size(), out);
}

bool SceneSource::LoadScene(size_t index, std::vector<uint8_t> *out) const {
  return sceneBin_ != nullptr && ReadEntry(*sceneBin_, sceneIndex_, index, out);
}

bool SceneSource::LoadVoice(size_t index, std::vector<uint8_t> *out) const {
  if (!voiceBin_ || index >= voiceIndex_.size()) return false;
  return disc_.ReadRange(*voiceBin_, voiceIndex_[index].offset, voiceIndex_[index].size, out);
}

bool SceneSource::LoadSound(size_t index, std::vector<uint8_t> *out) const {
  if (!soundBin_ || index >= soundIndex_.size()) return false;
  return disc_.ReadRange(*soundBin_, soundIndex_[index].offset, soundIndex_[index].size, out);
}

} // namespace ZeroNoTsukaima
