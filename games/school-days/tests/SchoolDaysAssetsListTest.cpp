#include "SchoolDaysAssets.hpp"

#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

void put(const fs::path &path) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << 'x';
  require(bool(file), "fixture create failed");
}

bool fakeIsReference(const std::string &reference) {
  return Kotonoha::IsSchoolDaysGpkUri(reference);
}

bool fakeRead(const std::string &, std::vector<std::uint8_t> *, std::string *) {
  return false;
}

bool fakeList(const std::string &reference, std::vector<std::string> *children,
              std::string *) {
  children->clear();
  if (reference == "sdgpk://Event00/Scene") {
    *children = {"sdgpk://Event00/Scene/face.png",
                 "sdgpk://Event00/Scene/nested"};
    return true;
  }
  if (reference == "sdgpk://Event00/Scene/nested") {
    *children = {"sdgpk://Event00/Scene/nested/mouth.png"};
    return true;
  }
  return false;
}

} // namespace

int main() {
  const fs::path root =
      fs::temp_directory_path() / "kotonoha-school-days-list-test";
  try {
    std::error_code ec;
    fs::remove_all(root, ec);
    put(root / "Script/English/00/00-00-A00.ENG.ORS");
    put(root / "Event00/Scene/face.png");
    put(root / "Event00/Scene/nested/mouth.png");
    put(root / "Event01/other.png");
    put(root / "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG");

    SchoolDays::Assets assets(root);
    const auto mistypedVoice = "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG";
    require(fs::path(assets.resolve(mistypedVoice)) ==
                root / "Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG",
            "retail ORS voice typo did not resolve to the existing asset");
    put(root / "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG");
    require(
        fs::path(assets.resolve(mistypedVoice)) ==
            root / "Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG",
        "exact scripted asset did not take priority over voice typo fallback");
    const auto loose = assets.listAsset("Event00/Scene/");
    require(loose.size() == 2, "loose recursive enumeration count drifted");
    require(fs::path(loose[0]) == root / "Event00" / "Scene" / "face.png" &&
                fs::path(loose[1]) ==
                    root / "Event00" / "Scene" / "nested" / "mouth.png",
            "loose enumeration did not preserve readable physical references");

    Kotonoha::InstallSchoolDaysVirtualAssetIo(&fakeIsReference, &fakeRead,
                                              &fakeList);
    const auto packed = assets.listAsset("sdgpk://Event00/Scene/");
    Kotonoha::InstallSchoolDaysVirtualAssetIo(nullptr, nullptr, nullptr);
    require(
        packed ==
            std::vector<std::string>{"sdgpk://Event00/Scene/face.png",
                                     "sdgpk://Event00/Scene/nested/mouth.png"},
        "GPK directory-reference enumeration drifted");

    // The user's revised General and Sound artwork has the exact four retail
    // logical names, but must be read from the standalone display-options
    // overlay instead of the original System.GPK (or loose retail copies).
    // The original four CMAPs intentionally remain in their existing mount.
    const fs::path optionOverlay = root / "external-display-options";
    const std::vector<std::string> optionArtwork{
        "Option_Def.png", "Option_Def_Chip.png", "Option_Sound.png",
        "Option_Sound_Chip.png"};
    for (const auto &name : optionArtwork) {
      put(root / "System/Option" / name);
      put(optionOverlay / "System/Option" / name);
    }
    std::string overlayError;
    require(Kotonoha::SetSchoolDaysAssetOverlay(optionOverlay, &overlayError),
            "external option overlay could not be mounted");
    Kotonoha::InstallMountedSchoolDaysAssetResolver();
    for (const auto &name : optionArtwork) {
      const auto logical = "System/Option/" + name;
      const auto replacement =
          Kotonoha::ResolveSchoolDaysAssetPath(root.string().c_str(), logical);
      require(replacement.present && !replacement.collision &&
                  fs::equivalent(replacement.physical,
                                 optionOverlay / "System/Option" / name),
              ("General/Sound PNG did not resolve to the external override: " +
               logical + " -> " + replacement.physical + " (status " +
               std::to_string(static_cast<int>(replacement.status)) + ")")
                  .c_str());
    }
    Kotonoha::UninstallMountedSchoolDaysAssetResolver();
    require(Kotonoha::SetSchoolDaysAssetOverlay({}, &overlayError),
            "option overlay could not be unmounted");
    for (const auto &name : optionArtwork) {
      const auto fallback = Kotonoha::ResolveSchoolDaysAssetPath(
          root.string().c_str(), "System/Option/" + name);
      require(fallback.present && fs::equivalent(fallback.physical,
                                                 root / "System/Option" / name),
              "removing the overlay did not restore the original asset path");
    }

    fs::remove_all(root, ec);
    std::cout << "SchoolDaysAssetsListTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    Kotonoha::InstallSchoolDaysVirtualAssetIo(nullptr, nullptr, nullptr);
    Kotonoha::UninstallMountedSchoolDaysAssetResolver();
    (void)Kotonoha::SetSchoolDaysAssetOverlay({});
    std::error_code ec;
    fs::remove_all(root, ec);
    std::cerr << "SchoolDaysAssetsListTest FAIL: " << error.what() << '\n';
    return 1;
  }
}
