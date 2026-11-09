#include <RealLive/Voice.hpp>
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
  if (argc < 2)
    return 0;
  const std::filesystem::path root(argv[1]);
  RealLive::VoiceIndex index(root);
  std::size_t oggs = 0, songs = 0;
  const auto koe = directory(root, "koe");
  assert(!koe.empty());
  for (const auto &e : std::filesystem::recursive_directory_iterator(koe)) {
    auto extension = e.path().extension().string();
    for (auto &c : extension)
      c = char(std::tolower(static_cast<unsigned char>(c)));
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
  assert(index.size() == oggs);
  const auto bgm = directory(root, "bgm");
  assert(!bgm.empty());
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
  auto ini = read(root / "Gameexe.ini");
  auto tracks = RealLive::parseAudioConfig(
      std::string_view(reinterpret_cast<const char *>(ini.data()), ini.size()));
  assert(!tracks.tracks.empty());
  std::cout << oggs << " loose Ogg voices; " << songs << " NWA PCM tracks; "
            << tracks.tracks.size() << " track mappings verified\n";
}
