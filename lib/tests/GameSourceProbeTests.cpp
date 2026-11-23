#include <Kotonoha/Filesystem/EngineDetector.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Kotonoha::Filesystem::Filesystem;
using Kotonoha::Filesystem::findRequiredFilesPrefix;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

// Emulates a manifest-free HTTP source: HEAD/stat of known names succeeds,
// but a directory listing returns no entries at all.
class UnindexedHttpFixture final : public Kotonoha::Filesystem::IMount {
  std::set<std::string> files_;

public:
  explicit UnindexedHttpFixture(std::initializer_list<std::string> files)
      : files_(files) {}

  bool exists(std::string_view path) const override {
    return path.empty() || files_.count(std::string(path)) != 0;
  }
  Kotonoha::Filesystem::FileInfo stat(std::string_view path) const override {
    if (path.empty())
      return {"", Kotonoha::Filesystem::FileType::Directory, 0};
    if (!exists(path))
      throw Kotonoha::Filesystem::FilesystemError(
          Kotonoha::Filesystem::ErrorCode::NotFound, "HTTP 404");
    return {std::string(path), Kotonoha::Filesystem::FileType::Regular, 42};
  }
  std::vector<Kotonoha::Filesystem::FileInfo>
  list(std::string_view) const override {
    return {};
  }
  std::unique_ptr<Kotonoha::Filesystem::FileHandle>
  open(std::string_view) const override {
    return nullptr;
  }
};

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
    const auto rootFiles =
        Kotonoha::Filesystem::listGameSourceFiles(*source, 0);
    const auto includesFile =
        [](const std::vector<Kotonoha::Filesystem::FileInfo> &files,
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

    const auto ponscripter =
        Filesystem::mount(std::make_shared<UnindexedHttpFixture>(
            std::initializer_list<std::string>{"00.utf", "arc.nsa"}));
    require(Kotonoha::Filesystem::listGameSourceFiles(*ponscripter).empty(),
            "HTTP fixture must not enumerate any directory");
    require(
        Kotonoha::Filesystem::isGameSourceRegularFile(*ponscripter, "00.utf"),
        "PONscripter script must be found without a directory listing");
    require(Kotonoha::Filesystem::detectEngine(*ponscripter).kind ==
                Kotonoha::Filesystem::EngineKind::ONScripter,
            "PONscripter detection must work on unindexed HTTP mounts");

    const auto kirikiri =
        Filesystem::mount(std::make_shared<UnindexedHttpFixture>(
            std::initializer_list<std::string>{"data.xp3"}));
    require(Kotonoha::Filesystem::listGameSourceFiles(*kirikiri).empty(),
            "Kirikiri HTTP fixture must not enumerate remote files");
    require(
        Kotonoha::Filesystem::isGameSourceRegularFile(*kirikiri, "data.xp3"),
        "Kirikiri XP3 must be found without a directory listing");
    require(Kotonoha::Filesystem::detectEngine(*kirikiri).kind ==
                Kotonoha::Filesystem::EngineKind::Kirikiri,
            "Kirikiri detection must work on unindexed HTTP mounts");

    // A size-aware HTTP manifest makes arbitrary directory names visible
    // without a network directory listing, even before any GET/HEAD requests.
    auto indexed = Kotonoha::Filesystem::FetchAssetProvider::fromManifest(
        "https://example.invalid/game-assets/",
        R"({"entries":[{"path":"data.xp3","size":1024},{"path":"misc/voice.xp3","size":2048}]})");
    require(indexed->hasManifestIndex() && indexed->fileCount() == 2 &&
                !indexed->exists("unlisted.xp3") &&
                indexed->totalRequests() == 0,
            "Manifest must index files and reject unknown HTTP paths without "
            "requests");
    auto indexedSource = Filesystem::mount(
        std::static_pointer_cast<Kotonoha::Filesystem::IMount>(indexed));
    require(Kotonoha::Filesystem::isGameSourceRegularFile(*indexedSource,
                                                          "data.xp3"),
            "Manifest must reveal archive metadata without network requests");
    const auto manifestRoot = indexedSource->list("");
    const auto manifestNested = indexedSource->list("misc");
    require(manifestRoot.size() == 2 && manifestNested.size() == 1 &&
                manifestNested.front().path == "misc/voice.xp3",
            "Manifest must enumerate indexed root and nested directories");

    std::cout << "Virtual game source compatibility probes passed\n";
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
