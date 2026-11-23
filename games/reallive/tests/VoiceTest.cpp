#include <Kotonoha/core/AssetProvider.hpp>
#include <RealLive/Voice.hpp>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static std::vector<std::uint8_t> read(const std::filesystem::path &p) {
  std::ifstream in(p, std::ios::binary | std::ios::ate);
  const auto size = in.tellg();
  assert(size >= 0);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  in.seekg(0);
  in.read(reinterpret_cast<char *>(bytes.data()), size);
  assert(in.good());
  return bytes;
}
static std::filesystem::path directory(const std::filesystem::path &root,
                                       std::string name) {
  for (auto &c : name)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    auto candidate = entry.path().filename().string();
    for (auto &c : candidate)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    if (entry.is_directory() && candidate == name)
      return entry.path();
  }
  return {};
}
static std::filesystem::path findFile(const std::filesystem::path &root,
                                      std::string name) {
  for (auto &c : name)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    auto candidate = entry.path().filename().string();
    for (auto &c : candidate)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    if (entry.is_regular_file() && candidate == name)
      return entry.path();
  }
  return {};
}
int main(int argc, char **argv) {
  const auto config = RealLive::parseAudioConfig(
      "#DSTRACK = 000 - 99999999 - 1335 = \"BGM00\" = "
      "\"BGM00\"\n#SE.003=\"click\",2\n");
  assert(config.tracks.at("bgm00").loop == 1335);
  assert(config.soundEffects.at(3).channel == 2);
  bool rejected = false;
  try {
    RealLive::nwaToWav({});
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  const RealLive::VoiceIndex assets(std::vector<std::string>{
      "KOE/000/Z000000123.OGG", "KOE/000/z000000456.ogg", "KOE/other.txt"});
  assert(assets.size() == 2 &&
         assets.find(123)->generic_string() == "KOE/000/Z000000123.OGG" &&
         !assets.find(789));
  // Mounted sources (ISO images and the Web game-assets mount) have no local
  // path. Voice reads must fall back to the provider reader instead of
  // returning an empty buffer that later reaches the NWA decoder.
  {
    std::string requested;
    const std::vector<std::uint8_t> ogg{'O', 'g', 'g', 'S', 1, 2, 3};
    const RealLive::VoiceIndex mounted(
        std::vector<std::string>{"KOE/0000/Z000000009.ogg"},
        [&](const std::string &id) {
          requested = id;
          return ogg;
        },
        [](const std::string &) { return std::string(); });
    const auto bytes = mounted.read(9);
    assert(requested == "KOE/0000/Z000000009.ogg");
    assert(bytes && *bytes == ogg);
  }
  {
    // An NWK slice keeps its offset/size when the archive is virtual.
    const std::vector<std::uint8_t> archive{
        1, 0, 0, 0, 4, 0, 0, 0, 16, 0, 0, 0, 9, 0, 0, 0, 'O', 'g', 'g', 'S'};
    const RealLive::VoiceIndex mounted(
        std::vector<std::string>{"KOE/z0001.nwk"},
        [&](const std::string &) { return archive; },
        [](const std::string &) { return std::string(); });
    const auto bytes = mounted.read(100009);
    assert(bytes && std::string(bytes->begin(), bytes->end()) == "OggS");
    assert(!mounted.read(9));
  }
  {
    // An OVK archive uses 16-byte table entries (size, offset, id, samples).
    const std::vector<std::uint8_t> ovkArchive{1,  0, 0, 0, 4,   0,   0,   0,
                                               20, 0, 0, 0, 7,   0,   0,   0,
                                               0,  0, 0, 0, 'O', 'g', 'g', 'S'};
    const RealLive::VoiceIndex mountedOvk(
        std::vector<std::string>{"KOE/z0002.ovk"},
        [&](const std::string &) { return ovkArchive; },
        [](const std::string &) { return std::string(); });
    const auto bytes = mountedOvk.read(200007);
    assert(bytes && std::string(bytes->begin(), bytes->end()) == "OggS");
    assert(!mountedOvk.read(7));
  }
  {
    // Streaming VoiceIndex via openStream must read only header + clip slice
    // and never call the full-file reader.
    struct MemoryStream : Kotonoha::AssetStream {
      const std::vector<std::uint8_t> &data;
      std::uint64_t pos = 0;
      explicit MemoryStream(const std::vector<std::uint8_t> &d) : data(d) {}
      std::uint64_t size() const override { return data.size(); }
      std::uint64_t tell() const override { return pos; }
      std::size_t read(Uint8 *buffer, std::size_t n) override {
        const std::size_t avail =
            pos < data.size() ? std::size_t(data.size() - pos) : 0;
        const std::size_t take = std::min(n, avail);
        std::copy_n(data.data() + pos, take, buffer);
        pos += take;
        return take;
      }
      bool seek(std::uint64_t p) override {
        if (p > data.size())
          return false;
        pos = p;
        return true;
      }
    };
    const std::vector<std::uint8_t> ovkArchive{1,  0, 0, 0, 4,   0,   0,   0,
                                               20, 0, 0, 0, 7,   0,   0,   0,
                                               0,  0, 0, 0, 'O', 'g', 'g', 'S'};
    bool fullReaderCalled = false;
    const RealLive::VoiceIndex streamedOvk(
        std::vector<std::string>{"KOE/z0002.ovk"},
        [&](const std::string &) -> std::vector<std::uint8_t> {
          fullReaderCalled = true;
          return ovkArchive;
        },
        [](const std::string &) { return std::string(); },
        [&](const std::string &) -> std::unique_ptr<Kotonoha::AssetStream> {
          return std::make_unique<MemoryStream>(ovkArchive);
        });
    assert(!fullReaderCalled);
    const auto bytes = streamedOvk.read(200007);
    assert(!fullReaderCalled);
    assert(bytes && std::string(bytes->begin(), bytes->end()) == "OggS");
  }
  if (argc < 2)
    return 0;
  const std::filesystem::path root(argv[1]);
  RealLive::VoiceIndex index(root);
  std::size_t oggs = 0, archives = 0, songs = 0;
  const auto koe = directory(root, "koe");
  assert(!koe.empty());
  for (const auto &e : std::filesystem::recursive_directory_iterator(koe)) {
    if (!e.is_regular_file())
      continue;
    auto extension = e.path().extension().string();
    for (auto &c : extension)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    if (extension == ".ovk" || extension == ".nwk" || extension == ".nwa") {
      ++archives;
      continue;
    }
    if (extension != ".ogg")
      continue;
    const auto name = e.path().stem().string();
    const auto id = std::stoi(name.substr(1));
    assert(index.find(id) == e.path());
    std::ifstream in(e.path(), std::ios::binary);
    char magic[4];
    in.read(magic, 4);
    assert(std::string(magic, 4) == "OggS");
    ++oggs;
  }
  if (archives == 0) {
    assert(index.size() == oggs);
  } else {
    assert(index.size() > oggs);
  }
  const auto bgm = directory(root, "bgm");
  if (!bgm.empty()) {
    for (const auto &e : std::filesystem::directory_iterator(bgm)) {
      auto extension = e.path().extension().string();
      for (auto &c : extension)
        c = char(std::tolower(static_cast<unsigned char>(c)));
      if (extension != ".nwa")
        continue;
      auto b = read(e.path());
      auto wav = RealLive::nwaToWav(b);
      assert(wav.size() == b.size());
      assert(std::equal(b.begin() + 44, b.end(), wav.begin() + 44));
      ++songs;
    }
  }
  const auto iniPath = findFile(root, "gameexe.ini");
  assert(!iniPath.empty());
  auto ini = read(iniPath);
  auto tracks = RealLive::parseAudioConfig(
      std::string_view(reinterpret_cast<const char *>(ini.data()), ini.size()));
  assert(!tracks.tracks.empty());
  std::cout << oggs << " loose Ogg voices; " << archives
            << " packed voice archives (" << index.size() << " indexed); "
            << songs << " NWA PCM tracks; " << tracks.tracks.size()
            << " track mappings verified\n";
}
