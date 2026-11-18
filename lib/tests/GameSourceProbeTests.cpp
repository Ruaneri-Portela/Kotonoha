#include <Kotonoha/Filesystem/GameSourceProbe.hpp>

#include <chrono>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using Kotonoha::Filesystem::Filesystem;
using Kotonoha::Filesystem::findRequiredFilesPrefix;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

class Fixture {
public:
  fs::path root =
      fs::temp_directory_path() /
      ("kotonoha-game-probe-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Fixture() { fs::create_directories(root); }
  ~Fixture() {
    std::error_code error;
    fs::remove_all(root, error);
  }
  void touch(const fs::path &name) const {
    fs::create_directories((root / name).parent_path());
    std::ofstream out(root / name, std::ios::binary);
    if (!out)
      throw std::runtime_error("Failed to write synthetic game fixture");
    out << "fixture";
  }
};

} // namespace

int main() {
  try {
    Fixture fixture;
    fixture.touch("files/param.pak");
    fixture.touch("files/script.pak");
    fixture.touch("files/sysse.pak");
    fixture.touch("data/pack/script.dat");
    fixture.touch("data/pack/graphic.dat");
    fixture.touch("game/scripts.rpa");
    fixture.touch("game/images.rpa");
    fixture.touch("nss/0_boot.nss");
    fixture.touch("archives/sub/game.NPA");
    fixture.touch("archives/sub/voice.AFS");
    fixture.touch("Seen.TXT");
    fixture.touch("GameExe.INI");
    fs::create_directories(fixture.root / "fake.dat");

    const auto source = Filesystem::mount(fixture.root.string());
    const auto luca = findRequiredFilesPrefix(
        *source, {"PARAM.PAK", "SCRIPT.PAK", "SYSSE.PAK"}, {"", "files/"});
    require(luca && *luca == "files/",
            "LucaSystem: locate case-insensitive PAK group in files/");
    require(!findRequiredFilesPrefix(*source,
                                     {"PARAM.PAK", "SCRIPT.PAK", "MISSING.PAK"},
                                     {"", "files/"}),
            "LucaSystem: reject incomplete PAK group");
    const auto pmail = findRequiredFilesPrefix(
        *source, {"SCRIPT.DAT", "GRAPHIC.DAT"}, {"", "pack/", "data/pack/"});
    require(pmail && *pmail == "data/pack/",
            "Pure Mail: recognize nested script/graphics in data/pack/");
    const auto ddlc = findRequiredFilesPrefix(
        *source, {"scripts.rpa", "images.rpa"}, {"game/", ""});
    require(ddlc && *ddlc == "game/", "DDLC: recognize game/ archive pair");
    require(findRequiredFilesPrefix(*source, {"seen.txt", "gameexe.ini"}) ==
                std::optional<std::string>{""},
            "RealLive: recognize mixed-case VFS filenames");
    require(!findRequiredFilesPrefix(*source, {"fake.dat"}),
            "A directory cannot masquerade as a game archive");
    const auto allFiles = Kotonoha::Filesystem::listGameSourceFiles(*source);
    const auto rootFiles = Kotonoha::Filesystem::listGameSourceFiles(*source, 0);
    const auto includesFile = [](const std::vector<Kotonoha::Filesystem::FileInfo> &files,
                                 const std::string &id) {
      return std::any_of(files.begin(), files.end(),
                         [&](const auto &file) { return file.path == id; });
    };
    require(includesFile(allFiles, "nss/0_boot.nss") &&
                includesFile(allFiles, "archives/sub/game.NPA") &&
                includesFile(allFiles, "archives/sub/voice.AFS"),
            "N2System: find script and archive files nested below source root");
    require(!includesFile(rootFiles, "nss/0_boot.nss"),
            "Depth-zero traversal must not descend into nested folders");
    bool bounded = false;
    try {
      (void)Kotonoha::Filesystem::listGameSourceFiles(*source, 4, 1);
    } catch (const Kotonoha::Filesystem::FilesystemError &error) {
      bounded = error.code() == Kotonoha::Filesystem::ErrorCode::IOError;
    }
    require(bounded, "VFS recursion must reject exceeding the traversal limit");
    require(!findRequiredFilesPrefix(*source, {}),
            "An empty required-file set does not validate any source");

    std::cout << "Virtual game source compatibility probes passed\n";
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
