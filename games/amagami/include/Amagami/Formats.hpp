#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Amagami {
using Bytes = std::vector<uint8_t>;
struct ArchiveEntry {
  uint32_t flags = 0, hash = 0, offset = 0, size = 0;
};
// Header/index only: payloads remain on disc. Throws on malformed input.
std::vector<ArchiveEntry> ParseArc(const Bytes &header, uint64_t fileSize);
std::vector<ArchiveEntry> ParseAfs(const Bytes &header, uint64_t fileSize);
Bytes Unpack(const Bytes &packed, size_t limit = 64 * 1024 * 1024);

struct Value {
  enum Type {
    Nil = 0,
    Integer = 1,
    False = 2,
    Float = 3,
    True = 4,
    String = 5,
    Class = 6,
    Symbol = 7,
    Array = 8
  };
  uint8_t type = Nil;
  uint32_t bits = 0;
  std::string text; // CP932 converted to UTF-8
  std::vector<Value> elements;
};
struct Variable {
  uint8_t slot;
  std::string name;
};
struct Method {
  std::string name;
  uint16_t arguments = 0;
  bool classMethod = false;
  Bytes code;
};
struct Script {
  uint8_t flags = 0;
  std::string name, parent;
  std::vector<Variable> variables, classVariables;
  std::vector<Method> methods;
  std::vector<Value> constants;
  std::vector<std::string> texts() const;
};
Script ParseScf(const Bytes &bytes);
struct ClassFile {
  std::string name;
  Bytes bytes;
};
std::vector<ClassFile> ParseClassImage(const Bytes &unpacked);
} // namespace Amagami
