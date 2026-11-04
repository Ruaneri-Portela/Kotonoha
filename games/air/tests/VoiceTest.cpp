#include <Air/Voice.hpp>
#include <cassert>
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
int main(int argc, char **argv) {
  const auto config =
      Air::parseAudioConfig("#DSTRACK = 000 - 99999999 - 1335 = \"BGM00\" = "
                            "\"BGM00\"\n#SE.003=\"click\",2\n");
  assert(config.tracks.at("bgm00").loop == 1335);
  assert(config.soundEffects.at(3).channel == 2);
  bool rejected = false;
  try {
    Air::nwaToWav({});
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  const Air::VoiceIndex assets(std::vector<std::string>{
      "KOE/000/Z000000123.OGG", "KOE/000/z000000456.ogg", "KOE/other.txt"});
  assert(assets.size() == 2 &&
         assets.find(123)->generic_string() == "KOE/000/Z000000123.OGG" &&
         !assets.find(789));
  if (argc < 2)
    return 0;
  const std::filesystem::path root(argv[1]);
  Air::VoiceIndex index(root);
  assert(index.size() == 24900);
  std::size_t oggs = 0, songs = 0;
  for (const auto &e :
       std::filesystem::recursive_directory_iterator(root / "KOE")) {
    if (e.path().extension() != ".ogg")
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
  for (const auto &e : std::filesystem::directory_iterator(root / "bgm")) {
    if (e.path().extension() != ".nwa")
      continue;
    auto b = read(e.path());
    auto wav = Air::nwaToWav(b);
    assert(wav.size() == b.size());
    assert(std::equal(b.begin() + 44, b.end(), wav.begin() + 44));
    ++songs;
  }
  auto ini = read(root / "Gameexe.ini");
  auto tracks = Air::parseAudioConfig(
      std::string_view(reinterpret_cast<const char *>(ini.data()), ini.size()));
  assert(tracks.tracks.size() == 27);
  std::cout << oggs << " loose Ogg voices; " << songs << " NWA PCM tracks; "
            << tracks.tracks.size() << " track mappings verified\n";
}
