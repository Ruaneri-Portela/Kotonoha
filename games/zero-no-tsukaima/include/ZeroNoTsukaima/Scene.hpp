#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// One KAG-style script command of a compiled scene: {tagname: ..., attr: ...}.
// Every value is stored as text (UTF-8); integers are written in decimal.
struct SceneTag {
  std::string name;
  std::map<std::string, std::string> attrs;

  const std::string *Get(const std::string &key) const {
    auto it = attrs.find(key);
    return it == attrs.end() ? nullptr : &it->second;
  }
};

// Literal table of a compiled Squirrel 2.x function ("SQIR" file, header
// FA FA 'RIQS'): only the part needed to read scene data and name tables.
struct NutLiteral {
  enum Kind { String, Integer, Other } kind = Other;
  std::string text; // Shift-JIS converted to UTF-8
  int32_t value = 0;
};

struct NutInstruction {
  int32_t arg1 = 0;
  uint8_t op = 0, arg0 = 0, arg2 = 0, arg3 = 0;
};

struct NutFunction {
  std::vector<NutLiteral> literals;
  std::vector<NutInstruction> code;
};

bool ParseNut(const std::vector<uint8_t> &data, NutFunction *out);

// Value built by a data-only Squirrel function (ENVINIT.NUT): nested tables,
// arrays, strings and integers.
struct NutValue {
  enum Kind { Null, String, Integer, Table, Array } kind = Null;
  std::string text;
  int32_t value = 0;
  std::vector<std::pair<std::string, std::shared_ptr<NutValue>>> fields; // Table (keys as text)
  std::vector<std::shared_ptr<NutValue>> items;                          // Array

  const NutValue *Get(const std::string &key) const {
    for (const auto &[k, v] : fields)
      if (k == key) return v.get();
    return nullptr;
  }
  std::string Text() const { return kind == Integer ? std::to_string(value) : text; }
};

// Runs the table-building opcodes of the main function and returns the table
// left in register 1 (the "return { ... }" value), or nullptr.
std::shared_ptr<NutValue> EvalNutTable(const std::vector<uint8_t> &unpacked);

// Rebuilds the sequence of tag tables a scene function creates.
bool ParseSceneTags(const std::vector<uint8_t> &unpacked, std::vector<SceneTag> *out);

// SCENES.NUT (SYSTEM entry 32): script names ("ST01_00.KS", ...), where the
// position in the list is the SCENE_ID archive index.
bool ParseSceneNames(const std::vector<uint8_t> &unpacked, std::vector<std::string> *out);

} // namespace ZeroNoTsukaima
