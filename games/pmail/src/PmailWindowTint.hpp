#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace Kotonoha::PmailPort {

// The original OF_ADV2.INI settings (MesWin_R/G/B) modify WAKUH.CHAR via
// locpu.dll AddRgbChr, while preserving its alpha.
struct PmailMessageTint {
  int red = 0, green = 0, blue = 150;
};

inline PmailMessageTint ParsePmailMessageTint(
    std::string_view ini, PmailMessageTint defaults = {}) {
  PmailMessageTint result = defaults;
  size_t pos = 0;
  while (pos < ini.size()) {
    const auto end = ini.find_first_of("\r\n", pos);
    const auto line = ini.substr(pos, end == std::string_view::npos
                                         ? ini.size() - pos : end - pos);
    const auto equal = line.find('=');
    if (equal != std::string_view::npos) {
      const auto key = line.substr(0, equal);
      const auto value = line.substr(equal + 1);
      const auto trim = [](std::string_view s) {
        const auto begin = s.find_first_not_of(" \t");
        if (begin == std::string_view::npos)
          return std::string_view{};
        return s.substr(begin, s.find_last_not_of(" \t") - begin + 1);
      };
      const auto k = trim(key), v = trim(value);
      int *out = nullptr;
      if (k == "MesWin_R") out = &result.red;
      else if (k == "MesWin_G") out = &result.green;
      else if (k == "MesWin_B") out = &result.blue;
      if (out && !v.empty()) {
        int valueNumber = 0;
        bool valid = true;
        for (auto digit : v) {
          if (digit < '0' || digit > '9') { valid = false; break; }
          valueNumber = valueNumber * 10 + (digit - '0');
          if (valueNumber > 256) { valid = false; break; }
        }
        if (valid)
          *out = valueNumber;
      }
    }
    pos = end == std::string_view::npos ? ini.size() : end + 1;
    if (pos < ini.size() && ini[pos] == '\n')
      ++pos;
  }
  return result;
}

inline unsigned char PmailAddRgbChr(unsigned char channel, int adjustment) {
  return static_cast<unsigned char>(
      channel + ((255 - channel) * std::clamp(adjustment, 0, 256) >> 8));
}

} // namespace Kotonoha::PmailPort
