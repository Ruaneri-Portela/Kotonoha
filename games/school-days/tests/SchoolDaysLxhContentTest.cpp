#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
  try {
    using Content = Kotonoha::SchoolDaysLxhContent;
    constexpr std::array<std::string_view, 7> auditedCodes{{
        "05-A2-F00", "05-A2-Z00", "05-B0-F00", "05-G0-L03",
        "05-KC-F00", "05-SB-E01", "05-SC-L03"}};
    Require(Content::Endings == auditedCodes,
            "Permanent seven-ending LxH manifest changed");
    const auto paths = Content::PackEntries();
    Require(paths.size() == 28u,
            "Each of seven integrated endings must have four pack entries");

    std::unordered_set<std::string> unique;
    for (std::size_t index = 0; index < auditedCodes.size(); ++index) {
      const std::string code(auditedCodes[index]);
      const std::array<std::string, 4> expected{{
          "Script/English/05/" + code + ".ENG.ORS",
          "LxH/" + code + "-LXH.png",
          "LxH/Media/" + code + "-LXH.wmv",
          "LxH/Media/" + code + "-LXH.ogg",
      }};
      for (std::size_t kind = 0; kind < expected.size(); ++kind) {
        const auto &path = paths[index * expected.size() + kind];
        Require(path == expected[kind],
                "Integrated GPK entry path/order differs from audited manifest");
        Require(unique.insert(path).second,
                "Integrated GPK contains duplicate logical asset path");
        Require(Content::IsPackResource(path),
                "LxH integrated path is missing from content classifier");
        std::string variant = path;
        for (auto &ch : variant) {
          if (ch == '/') ch = '\\';
          else if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        Require(Content::IsPackResource(variant),
                "LxH pack classifier lost case/Windows separator normalization");
      }
    }
    Require(unique.size() == 28u, "Permanent manifest is not unique");
    for (const auto &nonLxh : {
             "Script/English/04/04-A0-A00.ENG.ORS",
             "Script/English/05/05-AD-F00.ENG.ORS",
             "System/Option/Option_Display.png",
             "LxH/Media/05-A2-F00-LXH.mp4",
             "LxH/Media/05-A2-F00-LXH.ogg.bak",
             "Script/English/05/../05-A2-F00.ENG.ORS",
             "LxH.mode",
             "LxH.overlay"}) {
      Require(!Content::IsPackResource(nonLxh),
              std::string("Non-integrated resource was claimed: ") + nonLxh);
    }

    std::cout << "SchoolDaysLxhContentTest PASS: seven endings / 28 "
                 "permanent seekable-pack resources\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysLxhContentTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
