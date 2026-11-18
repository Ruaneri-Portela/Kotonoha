#pragma once
// Clean-room standard PSB v2 ("PSB\0", ver=2, enc=0) and MDF ("mdf\0") parser.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Kotonoha::Providers {

struct PsbChunkRef {
  std::uint32_t index = 0;
  std::uint32_t offset = 0;
  std::uint32_t length = 0;

  bool operator==(const PsbChunkRef &other) const noexcept {
    return index == other.index && offset == other.offset &&
           length == other.length;
  }
};

class PsbValue {
public:
  enum class Type {
    Null,
    Bool,
    Int,
    UIntArray,
    Float,
    Double,
    String,
    Chunk,
    List,
    Dict
  };

  using List = std::vector<PsbValue>;
  using Dict = std::map<std::string, PsbValue>;

  PsbValue() = default;

  static PsbValue makeNull();
  static PsbValue makeBool(bool value);
  static PsbValue makeInt(std::int64_t value);
  static PsbValue makeUIntArray(std::vector<std::uint64_t> values);
  static PsbValue makeFloat(float value);
  static PsbValue makeDouble(double value);
  static PsbValue makeString(std::string value);
  static PsbValue makeChunk(PsbChunkRef chunk);
  static PsbValue makeList(List values);
  static PsbValue makeDict(Dict entries);

  static const PsbValue &nullValue();

  Type type() const noexcept { return type_; }
  bool isNull() const noexcept { return type_ == Type::Null; }
  bool isBool() const noexcept { return type_ == Type::Bool; }
  bool isInt() const noexcept { return type_ == Type::Int; }
  bool isUIntArray() const noexcept { return type_ == Type::UIntArray; }
  bool isFloat() const noexcept { return type_ == Type::Float; }
  bool isDouble() const noexcept { return type_ == Type::Double; }
  bool isNumber() const noexcept {
    return type_ == Type::Int || type_ == Type::Float || type_ == Type::Double;
  }
  bool isString() const noexcept { return type_ == Type::String; }
  bool isChunk() const noexcept { return type_ == Type::Chunk; }
  bool isList() const noexcept { return type_ == Type::List; }
  bool isDict() const noexcept { return type_ == Type::Dict; }

  bool asBool(bool fallback = false) const noexcept;
  std::int64_t asInt(std::int64_t fallback = 0) const noexcept;
  double asDouble(double fallback = 0.0) const noexcept;
  float asFloat(float fallback = 0.0f) const noexcept;
  std::int64_t toInt(std::int64_t fallback = 0) const noexcept;
  double toDouble(double fallback = 0.0) const noexcept;
  const std::string &asString() const noexcept;
  std::string toString(const std::string &fallback = {}) const;
  const std::vector<std::uint64_t> &asUIntArray() const noexcept;
  PsbChunkRef asChunk() const noexcept { return chunkVal_; }
  const List &asList() const noexcept;
  const Dict &asDict() const noexcept;

  std::size_t size() const noexcept;
  bool has(const std::string &key) const noexcept;
  const PsbValue &operator[](std::size_t index) const noexcept;
  const PsbValue &operator[](const std::string &key) const noexcept;

  // Deterministic JSON serialization used for savepoint snapshot persistence.
  std::string toJson() const;
  static PsbValue fromJson(const std::string &json);

private:
  Type type_ = Type::Null;
  bool boolVal_ = false;
  std::int64_t intVal_ = 0;
  double doubleVal_ = 0.0;
  std::vector<std::uint64_t> uintArrayVal_;
  std::string stringVal_;
  PsbChunkRef chunkVal_;
  List listVal_;
  Dict dictVal_;
};

struct PsbHeader {
  std::uint16_t version = 0;
  std::uint16_t encrypt = 0;
  std::uint32_t offEncrypt = 0;
  std::uint32_t offNames = 0;
  std::uint32_t offStrings = 0;
  std::uint32_t offStringsData = 0;
  std::uint32_t offChunkOffsets = 0;
  std::uint32_t offChunkLengths = 0;
  std::uint32_t offChunkData = 0;
  std::uint32_t offEntries = 0;
};

class PsbFile {
public:
  PsbFile() = default;

  static bool isMdfBytes(const std::uint8_t *data, std::size_t size) noexcept;
  static bool isPsbBytes(const std::uint8_t *data, std::size_t size) noexcept;

  static std::vector<std::uint8_t> decompressMdf(const std::uint8_t *data,
                                                 std::size_t size);
  static std::vector<std::uint8_t> wrapMdf(const std::vector<std::uint8_t> &psb);

  static PsbFile parse(const std::uint8_t *data, std::size_t size);
  static PsbFile parse(const std::vector<std::uint8_t> &bytes);

  // Encodes a PsbValue DOM (with optional raw binary chunks) as canonical
  // PSB v2 (enc=0) bytes, optionally wrapped in an MDF ("mdf\0") container.
  static std::vector<std::uint8_t>
  serializeV2(const PsbValue &root,
              const std::vector<std::vector<std::uint8_t>> &chunks = {},
              bool wrapInMdf = false);

  const PsbHeader &header() const noexcept { return header_; }
  const std::vector<std::string> &names() const noexcept { return names_; }
  const PsbValue &root() const noexcept { return root_; }
  std::size_t chunkCount() const noexcept { return chunks_.size(); }
  std::vector<std::uint8_t> chunkBytes(std::uint32_t index) const;

private:
  PsbHeader header_{};
  std::vector<std::string> names_;
  std::vector<PsbChunkRef> chunks_;
  std::vector<std::uint8_t> raw_;
  PsbValue root_;
};

} // namespace Kotonoha::Providers
