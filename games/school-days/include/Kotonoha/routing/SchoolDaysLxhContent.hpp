#pragma once

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Kotonoha {

// The seven authored HQ endings always include their LxH epilogues.
// This describes immutable content, NOT a DLC enable/disable preference.
class SchoolDaysLxhContent final {
public:
  static constexpr std::array<std::string_view, 7> Endings{{
      "05-A2-F00", "05-A2-Z00", "05-B0-F00", "05-G0-L03",
      "05-KC-F00", "05-SB-E01", "05-SC-L03"}};

  static std::vector<std::string> PackEntries() {
    std::vector<std::string> paths;
    paths.reserve(Endings.size() * 4u);
    for (const auto ending : Endings) {
      const std::string code(ending);
      paths.push_back("Script/English/05/" + code + ".ENG.ORS");
      paths.push_back("LxH/" + code + "-LXH.png");
      paths.push_back("LxH/Media/" + code + "-LXH.wmv");
      paths.push_back("LxH/Media/" + code + "-LXH.ogg");
    }
    return paths;
  }

  static bool IsPackResource(std::string_view logical) {
    std::string folded;
    folded.reserve(logical.size());
    for (unsigned char ch : logical)
      folded.push_back(ch == '\\' ? '/' :
          static_cast<char>(std::tolower(ch)));
    static constexpr std::array<std::string_view, 7> lowerCodes{{
        "05-a2-f00", "05-a2-z00", "05-b0-f00", "05-g0-l03",
        "05-kc-f00", "05-sb-e01", "05-sc-l03"}};
    std::string_view tail;
    enum class Kind { None, Script, Card, Media } kind = Kind::None;
    if (folded.compare(0u, 18u, "script/english/05/") == 0) {
      tail = std::string_view(folded).substr(18u);
      kind = Kind::Script;
    } else if (folded.compare(0u, 10u, "lxh/media/") == 0) {
      tail = std::string_view(folded).substr(10u);
      kind = Kind::Media;
    } else if (folded.compare(0u, 4u, "lxh/") == 0) {
      tail = std::string_view(folded).substr(4u);
      kind = Kind::Card;
    }
    for (const auto code : lowerCodes) {
      if (kind == Kind::Script && tail.size() == code.size() + 8u &&
          tail.substr(0u, code.size()) == code &&
          tail.substr(code.size()) == ".eng.ors") return true;
      if (kind == Kind::Card && tail.size() == code.size() + 8u &&
          tail.substr(0u, code.size()) == code &&
          tail.substr(code.size()) == "-lxh.png") return true;
      if (kind == Kind::Media && tail.size() == code.size() + 8u &&
          tail.substr(0u, code.size()) == code &&
          (tail.substr(code.size()) == "-lxh.wmv" ||
           tail.substr(code.size()) == "-lxh.ogg")) return true;
    }
    return false;
  }
};

} // namespace Kotonoha
