#include <Air/Config.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace Air {

std::string canonical(std::string s) {
  for (auto &c : s) {
    if (c == '\\')
      c = '/';
    if (c >= 'a' && c <= 'z')
      c -= 32;
  }
  return s;
}

Config::Config(const std::string &source) {
  std::istringstream input(source);
  std::string line;
  while (std::getline(input, line)) {
    auto start = line.find('#');
    if (start == line.npos)
      continue;
    auto eq = line.find('=', start);
    if (eq == line.npos)
      continue;
    auto key = line.substr(start + 1, eq - start - 1);
    auto end = key.find_last_not_of(" \t\r");
    if (end == key.npos)
      continue;
    key.resize(end + 1);
    std::vector<std::string> tokens;
    for (size_t i = eq + 1; i < line.size();) {
      unsigned char c = line[i];
      if (c == ';')
        break;
      if (c == '"') {
        size_t first = ++i;
        while (i < line.size() && line[i] != '"')
          ++i;
        if (i == line.size())
          throw std::runtime_error("Unterminated Gameexe.ini string: " + key);
        tokens.push_back(line.substr(first, i - first));
        ++i;
      } else if ((c >= '0' && c <= '9') || c == '-') {
        size_t first = i++;
        while (i < line.size() &&
               ((line[i] >= '0' && line[i] <= '9') || line[i] == 'x' ||
                (line[i] >= 'a' && line[i] <= 'f') ||
                (line[i] >= 'A' && line[i] <= 'F')))
          ++i;
        tokens.push_back(line.substr(first, i - first));
      } else
        ++i;
    }
    values[canonical(key)] = std::move(tokens);
  }
}

const std::vector<std::string> &Config::get(const std::string &key) const {
  static const std::vector<std::string> empty;
  auto i = values.find(canonical(key));
  return i == values.end() ? empty : i->second;
}

int Config::integer(const std::string &key, int fallback, size_t item) const {
  const auto &v = get(key);
  if (item >= v.size())
    return fallback;
  try {
    return std::stoi(v[item], nullptr, v[item].find("0x") == 0 ? 16 : 10);
  } catch (...) {
    return fallback;
  }
}

std::string Config::text(const std::string &key, const std::string &fallback,
                         size_t item) const {
  const auto &v = get(key);
  return item < v.size() ? v[item] : fallback;
}

namespace {
// rlBabel's Cp1252::JisDecode: undoes the reversible western glyph mapping.
// Italic glyphs are stored 0x200 above the normal code, so the mapped value
// has to be decoded again.
bool westernGlyph(unsigned c, unsigned &decoded) {
  if (c <= 0x7f) {
    decoded = c;
    return true;
  }
  if (c >= 0xa1 && c <= 0xdf) {
    decoded = c + 0x1f;
    return true;
  }
  if (c >= 0x8980 && c <= 0x89bf) {
    decoded = c & 0xff;
    return true;
  }
  if (c == 0x89c0) {
    decoded = 0xff;
    return true;
  }
  if (c > 0x8300 && c < 0x8900) {
    const unsigned base =
        c > 0x8700 ? c + 0x200 : c - (c >= 0x8380 ? 0x8320 : 0x831f);
    return westernGlyph(base & 0xffff, decoded);
  }
  return false;
}
} // namespace

std::string decodeText(const std::string &input, int encoding) {
  if (encoding == 0)
    return Kotonoha::ShiftJisToUtf8(input);

  if (encoding != 2)
    throw std::runtime_error("Unsupported AIR script encoding: " +
                             std::to_string(encoding));

  // rlBabel's reversible western character mapping, decoded before CP1252.
  std::string western;
  for (size_t i = 0; i < input.size(); ++i) {
    unsigned c = uint8_t(input[i]);
    if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xef)) {
      if (++i == input.size())
        throw std::runtime_error("Truncated AIR western glyph");
      c = (c << 8) | uint8_t(input[i]);
    }
    unsigned decoded = 0;
    if (!westernGlyph(c, decoded))
      throw std::runtime_error("Unknown AIR western glyph: " +
                               std::to_string(c));
    western += char(decoded);
  }

  // Utiliza a interface unificada do Kotonoha para conversão de CP1252 para
  // UTF-8
  return Kotonoha::ConvertEncoding(western, "CP1252", "UTF-8");
}

} // namespace Air