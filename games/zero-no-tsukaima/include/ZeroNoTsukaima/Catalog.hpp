#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// Character definitions of ENVINIT.NUT (KAGEX style): every character has poses,
// each pose maps dress / face names to the codes of its image name
// "TCGnn_DRESSFACELEVEL" (e.g. ルイズ 学生服 怒り -> TCG01_1D3).
struct CharacterPose {
  std::string prefix; // "TCG01"
  std::map<std::string, std::string> dresses, faces;
  std::string defaultDress, defaultFace;
};
struct CharacterInfo {
  std::string defaultPose;
  std::map<std::string, CharacterPose> poses;
};
// Position words of a character tag (左, 右奥, 右移動, 出, 消去 ...).
struct CharacterPosition {
  bool hasX = false, relative = false;
  int xpos = 0; // offset of the sprite centre from the screen centre
  int disp = 0; // 1 show, 2 standing, 3 face only, 5 erase, 6 none
};
struct CharacterTable {
  std::map<std::string, CharacterInfo> characters;
  std::map<std::string, CharacterPosition> positions;
};

// Maps the names used by the scene scripts (stage + time of day, event CG,
// character sprite) to SCENEDAT entry indices. The disc archives have no file
// names: entries are stored in sorted-name order, so the order is rebuilt from
// the SYSTEM tables.
class ImageCatalog {
public:
  // systemEnv: SYSTEM entry 31 (ENVINIT.NUT, stage + character tables); systemMain: SYSTEM entry
  // 35 (CG list); pictureSizes: byte size of every SCENEDAT entry (sprite groups start with an
  // empty entry).
  bool Build(const std::vector<uint8_t> &systemEnv, const std::vector<uint8_t> &systemMain,
             const std::vector<uint32_t> &pictureSizes = {});

  const CharacterTable &Characters() const { return characters_; }
  // Sprite picture of a character, e.g. "TCG01_1D3". Returns -1 when unknown.
  int ResolveSprite(const std::string &image) const;

  // `stage` is the space-separated attribute list of a stage tag, e.g. "昼 ルイズの部屋".
  // Returns -1 when no background applies.
  int ResolveStage(const std::string &stage) const;
  // Event CG name (cg05a, ECG48B, ...). Returns -1 when unknown.
  int ResolveEvent(const std::string &name) const;

private:
  void BuildCharacters(const std::vector<uint8_t> &systemEnv, const std::vector<uint32_t> &pictureSizes,
                       int firstSprite);

  struct Stage {
    int first = -1; // index of the daytime picture
    bool timed = false;
  };
  std::map<std::string, Stage> stages_;
  std::map<std::string, int> events_;
  CharacterTable characters_;
  std::map<std::string, std::pair<int, int>> spriteGroups_; // "TCG01_1D" -> (empty entry, face count)
};

} // namespace ZeroNoTsukaima
