#pragma once

// Text opcode name catalogs used by LuckSystem (data/<game>.txt and
// data/<game>/OPCODE.txt). Entries are ordered by their bytecode index;
// never skip blank lines, which would silently remap all later opcodes.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace LucaSystem {

class OpcodeTable {
public:
  static OpcodeTable parse(std::string_view text) {
    OpcodeTable table;
    if (text.size() >= 3 &&
        text.substr(0, 3) == std::string_view("\xef\xbb\xbf", 3))
      text.remove_prefix(3); // Windows/UTF-8 BOM from some LuckSystem tables

    while (!text.empty()) {
      const auto end = text.find('\n');
      auto line = text.substr(0, end);
      const auto begin = line.find_first_not_of(" \t\r");
      if (begin == std::string_view::npos)
        throw std::invalid_argument("Empty LucaSystem opcode at index " +
                                    std::to_string(table.names_.size()));
      const auto last = line.find_last_not_of(" \t\r");
      line = line.substr(begin, last - begin + 1);
      if (line.find_first_of(" \t\r\n") != std::string_view::npos)
        throw std::invalid_argument("Whitespace inside LucaSystem opcode name");
      if (table.names_.size() == 256)
        throw std::invalid_argument("LucaSystem opcode table exceeds 256 names");
      table.names_.emplace_back(line);
      if (end == std::string_view::npos)
        break;
      text.remove_prefix(end + 1);
    }
    if (table.names_.empty())
      throw std::invalid_argument("Empty LucaSystem opcode table");
    return table;
  }

  std::size_t size() const noexcept { return names_.size(); }
  std::string name(std::uint8_t opcode) const {
    if (opcode < names_.size())
      return names_[opcode];
    return "OP_" + std::to_string(unsigned(opcode));
  }

private:
  std::vector<std::string> names_;
};

} // namespace LucaSystem
