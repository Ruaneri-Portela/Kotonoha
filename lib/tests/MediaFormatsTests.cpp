#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
using namespace Kotonoha;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

template <typename F> void rejects(F operation, const char *message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}

class MemoryAssetProvider final : public AssetProvider {
public:
  std::map<std::string, std::vector<Uint8>> files;
  std::string name() const override { return "Memory"; }
  bool containsAsset(const std::string &id) const override {
    return files.count(id) != 0;
  }
  std::vector<Uint8> readAsset(const std::string &id) const override {
    const auto found = files.find(id);
    if (found == files.end())
      throw std::runtime_error("Missing " + id);
    return found->second;
  }
};

void encoding() {
  // "こんにちは" + NEC "①" + CP932 wave dash "～" + half-width "ｱ".
  const std::string sjis =
      "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd\x87\x40\x81\x60\xb1"
      "A";
  require(ShiftJisToUtf8(sjis) ==
              "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF"
              "\xE2\x91\xA0\xEF\xBD\x9E\xEF\xBD\xB1"
              "A",
          "CP932 to UTF-8");
  // An unmapped pair becomes one '?', and the trail byte is not reused.
  require(ShiftJisToUtf8("a\x85\x40"
                         "z") == "a?z",
          "Invalid pair");
  require(ShiftJisToUtf8("") == "", "Empty text");
  require(ConvertEncoding("caf\xe9", "LATIN1") == "caf\xC3\xA9",
          "Generic conversion");
  rejects([] { ConvertEncoding("x", "NOT-AN-ENCODING"); },
          "Reject unknown encoding");
}

// ISO9660 image with an optional Joliet volume and a multi-sector movie.
std::vector<Uint8> iso(bool joliet = false, Uint32 movieSize = 3000) {
  std::vector<Uint8> disc((21 + (movieSize + 2047) / 2048) * 2048, 0);
  auto u32 = [&](std::size_t at, Uint32 v) {
    for (int i = 0; i < 4; ++i)
      disc[at + i] = Uint8(v >> (8 * i));
  };
  auto record = [&](std::size_t &at, std::string name, Uint32 lba, Uint32 size,
                    bool dir) {
    if (joliet && name.size() > 1) {
      std::string utf16;
      for (char c : name) {
        utf16 += '\0';
        utf16 += c;
      }
      name = std::move(utf16);
    }
    const std::size_t length = (33 + name.size() + 1) & ~std::size_t(1);
    disc[at] = Uint8(length);
    u32(at + 2, lba);
    u32(at + 10, size);
    disc[at + 25] = dir ? 2 : 0;
    disc[at + 32] = Uint8(name.size());
    std::memcpy(&disc[at + 33], name.data(), name.size());
    at += length;
  };
  const std::size_t pvd = 16 * 2048;
  disc[pvd] = 1;
  std::memcpy(&disc[pvd + 1], "CD001", 5);
  disc[pvd + 6] = 1;
  std::memset(&disc[pvd + 40], ' ', 32);
  std::memcpy(&disc[pvd + 40], "KOTONOHA_TEST", 13);
  std::size_t at = pvd + 156;
  record(at, std::string(1, '\0'), 18, 2048, true);
  disc[17 * 2048] = 255;
  std::memcpy(&disc[17 * 2048 + 1], "CD001", 5);
  if (joliet) {
    std::memcpy(&disc[17 * 2048], &disc[pvd], 2048);
    disc[17 * 2048] = 2;
    std::memcpy(&disc[17 * 2048 + 88], "%/E", 3);
  }
  at = 18 * 2048;
  record(at, std::string(1, '\0'), 18, 2048, true);
  record(at, std::string(1, '\1'), 18, 2048, true);
  record(at, "MOVIE", 19, 2048, true);
  record(at, joliet ? "Read Me.txt;1" : "README.TXT;1", 20, 5, false);
  if (joliet) {
    record(at, "Empty.dat;1", 20, 0, false);
    record(at, "EmptyDir", 20, 0, true);
  }
  at = 19 * 2048;
  record(at, std::string(1, '\0'), 19, 2048, true);
  record(at, std::string(1, '\1'), 18, 2048, true);
  record(at, "OPEN.PSS;1", 21, movieSize, false);
  std::memcpy(&disc[20 * 2048], "hello", 5);
  for (std::size_t i = 0; i < movieSize; ++i)
    disc[21 * 2048 + i] = Uint8(i % 251);
  return disc;
}

// The same image as raw 2352-byte sectors (MODE1: 16-byte sync/header).
std::vector<Uint8> raw(const std::vector<Uint8> &cooked) {
  std::vector<Uint8> out;
  for (std::size_t s = 0; s < cooked.size() / 2048; ++s) {
    std::vector<Uint8> sector(2352, 0);
    sector[15] = 1;
    std::memcpy(&sector[16], &cooked[s * 2048], 2048);
    out.insert(out.end(), sector.begin(), sector.end());
  }
  return out;
}

void write(const std::filesystem::path &path, const std::vector<Uint8> &data) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            std::streamsize(data.size()));
  require(bool(out), "Write temporary file");
}

void discImage(const std::filesystem::path &directory) {
  const auto cooked = iso();
  write(directory / "test.iso", cooked);
  write(directory / "test.bin", raw(cooked));
  write(directory / "broken.iso", std::vector<Uint8>(40000, 7));
  for (const char *name : {"test.iso", "test.bin"}) {
    DiscImage disc(directory / name);
    require(disc.isOpen() && disc.volumeLabel() == "KOTONOHA_TEST",
            "Disc label");
    require(disc.sectorSize() == (name[5] == 'i' ? 2048u : 2352u),
            "Detect sector layout");
    require(disc.files().size() == 2, "List disc files");
    const auto *readme = disc.find("readme.txt");
    require(readme && readme->path == "README.TXT" && readme->size == 5,
            "Case-insensitive find");
    require(disc.find("/README.TXT;1") == readme, "Version and slash");
    const auto text = disc.read(*readme);
    require(std::string(text.begin(), text.end()) == "hello", "Read file");
    const auto *movie = disc.find("movie/open.pss");
    require(movie && movie->path == "MOVIE/OPEN.PSS", "Subdirectory path");
    const auto range = disc.read(*movie, 2040, 20);
    bool same = range.size() == 20;
    for (std::size_t i = 0; same && i < 20; ++i)
      same = range[i] == Uint8((2040 + i) % 251);
    require(same, "Range across sectors");
    std::vector<Uint8> out;
    require(!disc.readInto(*movie, 2990, 11, out), "Reject range past end");
    rejects([&] { disc.read("MISSING"); }, "Reject missing disc file");
  }
  DiscImage closed;
  std::string error;
  require(!closed.tryOpen(directory / "broken.iso", &error) && !error.empty(),
          "Reject non-ISO image");
  rejects([&] { DiscImage(directory / "missing.iso"); },
          "Reject missing image");

  DiscImageAssetProvider provider(directory / "test.bin");
  AssetProvider &assets = provider;
  require(assets.contains("MOVIE/OPEN.PSS") && !assets.contains("NOPE"),
          "Provider contains");
  require(assets.list("movie/") == std::vector<std::string>{"MOVIE/OPEN.PSS"},
          "Provider list");
  require(assets.read("README.TXT").size() == 5, "Provider read");

  const auto imagePath = directory / "Game With Spaces.ISO";
  constexpr Uint32 movieSize = 1024 * 1024 + 57;
  const auto sourceBytes = iso(true, movieSize);
  write(imagePath, sourceBytes);
  const auto directValidator = [](const std::string &path, std::string &info) {
    DiscImage disc(path);
    info = "Direct ISO";
    return disc.find("Read Me.txt") != nullptr;
  };
  require(GameLauncher::findGamePath("iso-tests", {imagePath.string()},
                                     directValidator, {""},
                                     true) == imagePath.string(),
          "Direct discovery passes the original ISO to its validator");
  int rejectedCalls = 0;
  rejects(
      [&] {
        GameLauncher::findGamePath(
            "iso-tests", {imagePath.string()},
            [&](const auto &, auto &info) {
              ++rejectedCalls;
              info = "Wrong game";
              return false;
            },
            {""}, true);
      },
      "Direct ISO discovery rejects wrong games without extraction fallback");
  require(rejectedCalls == 1,
          "Direct validation must not retry with an extracted directory");
  {
    FolderGameLauncher launcher("Direct ISO test", {}, imagePath.string(),
                                directValidator, {}, true);
    require(
        launcher.pathValid() && launcher.path() == imagePath.string(),
        "Direct folder launchers validate the ISO without materializing it");
    launcher.setPath((directory / "broken.iso").string());
    require(!launcher.pathValid() && !launcher.pathInfo().empty(),
            "Direct ISO validation errors remain visible");
  }
  DiscImage joliet(imagePath);
  require(joliet.find("Read Me.txt") &&
              joliet.find("Read Me.txt")->path == "Read Me.txt" &&
              joliet.read("movie/open.pss").size() == movieSize,
          "Joliet names and subdirectories are preserved");
  const auto mounted = gameDataDirectory(imagePath);
  require(std::filesystem::is_regular_file(mounted / "Read Me.txt") &&
              std::filesystem::file_size(mounted / "MOVIE/OPEN.PSS") ==
                  movieSize &&
              std::filesystem::file_size(mounted / "Empty.dat") == 0 &&
              std::filesystem::is_directory(mounted / "EmptyDir"),
          "ISO materialization exposes native filesystem paths");
  require(gameDataDirectory(imagePath) == mounted &&
              gameDataDirectory(std::filesystem::canonical(imagePath)) ==
                  mounted,
          "The same image is mounted only once per process");
  require(gameDataDirectory(directory) == directory,
          "Directory game sources remain unchanged");
  const auto changedPath = directory / "changed.iso";
  auto changed = cooked;
  write(changedPath, changed);
  const auto oldMount = gameDataDirectory(changedPath);
  changed[20 * 2048] = 'H';
  changed.resize(changed.size() + 2048);
  write(changedPath, changed);
  const auto newMount = gameDataDirectory(changedPath);
  require(
      newMount != oldMount &&
          FileSystemAssetProvider(oldMount).read("README.TXT").front() == 'h' &&
          FileSystemAssetProvider(newMount).read("README.TXT").front() == 'H',
      "Changed images remount without invalidating existing consumers");
  FileSystemAssetProvider files(imagePath);
  IndexedFileSystemAssetProvider indexed(imagePath);
  require(files.getRoot() == mounted &&
              indexed.read("read me.TXT") == files.read("Read Me.txt"),
          "Both filesystem asset providers accept an ISO directly");
  require(
      indexed.read("MOVIE/OPEN.PSS") == joliet.read("movie/open.pss"),
      "Chunked materialization preserves every byte across chunk boundaries");
  const auto textBytes = files.read("Read Me.txt");
  require(std::string(textBytes.begin(), textBytes.end()) == "hello",
          "Mounted file contents are unchanged");
  const auto validator = [](const std::string &path, std::string &info) {
    const bool found = std::filesystem::is_regular_file(
        std::filesystem::path(path) / "Read Me.txt");
    info = found ? "Found data" : "Missing data";
    return found;
  };
  require(
      GameLauncher::findGamePath("iso-tests", {imagePath.string()},
                                 validator) == imagePath.string(),
      "Shared discovery validates ISO contents but retains the source path");
  {
    struct RestoreDirectory {
      std::filesystem::path path = std::filesystem::current_path();
      ~RestoreDirectory() { std::filesystem::current_path(path); }
    } restore;
    std::filesystem::current_path(directory);
    require(std::filesystem::equivalent(
                GameLauncher::findGamePath(
                    "iso-tests", {imagePath.filename().string()}, validator),
                imagePath),
            "Relative ISO references search all standard roots before failing");
  }
  rejects(
      [&] {
        GameLauncher::findGamePath(
            "iso-tests",
            {(directory / "broken.iso").string(), mounted.string()}, validator);
      },
      "An explicitly selected invalid ISO must not silently launch another "
      "source");
  rejects(
      [&] {
        GameLauncher::findGamePath("iso-tests", {imagePath.string()},
                                   [](const auto &, auto &) { return false; });
      },
      "An ISO for the wrong game must not silently fall back");
  require(GameLauncher::findGamePath("iso-tests", {"", imagePath.string()},
                                     [](const auto &path, auto &) {
                                       return std::filesystem::is_regular_file(
                                           path);
                                     }) == imagePath.string(),
          "Disc-native ports retain the original image instead of folder "
          "validation");
  {
    const auto settings = (directory / "iso-settings.json").string();
    FolderGameLauncher launcher("ISO test", settings, imagePath.string(),
                                validator);
    require(
        launcher.pathValid() && launcher.path() == imagePath.string(),
        "Folder launchers accept ISO selections without persisting temp paths");
    require(launcher.launch(), "An ISO selection passes Play validation");
    const auto remembered = GameLauncher::rememberedPath(settings, "dir.txt");
    require(remembered == imagePath.string(),
            "Saved launcher preferences remember the ISO rather than extracted "
            "data");
    FolderGameLauncher reopened("ISO test", settings, {}, validator);
    require(reopened.pathValid() && reopened.path() == imagePath.string(),
            "An ISO outside standard data roots can be reopened from saved "
            "settings");
    launcher.setPath((directory / "broken.iso").string());
    require(!launcher.pathValid() && !launcher.pathInfo().empty(),
            "Invalid ISO errors are visible in the launcher");
  }
  rejects([&] { gameDataDirectory(directory / "broken.iso"); },
          "Reject invalid ISO mounts");
  rejects([&] { gameDataDirectory(directory / "missing.iso"); },
          "Reject missing ISO mounts");
  auto unsafe = cooked;
  const std::string before = "README.TXT;1", after = "../BAD.TXT;1";
  auto at =
      std::search(unsafe.begin(), unsafe.end(), before.begin(), before.end());
  require(at != unsafe.end(), "Locate unsafe-name fixture");
  std::copy(after.begin(), after.end(), at);
  write(directory / "unsafe.iso", unsafe);
  rejects([&] { gameDataDirectory(directory / "unsafe.iso"); },
          "ISO paths must not escape the temporary mount");
  auto truncated = cooked;
  truncated.resize(21 * 2048);
  write(directory / "truncated.iso", truncated);
  rejects([&] { gameDataDirectory(directory / "truncated.iso"); },
          "An incomplete ISO must not produce a successful mount");
  auto duplicate = cooked;
  const auto recordStart =
      std::size_t(std::search(duplicate.begin(), duplicate.end(),
                              before.begin(), before.end()) -
                  duplicate.begin()) -
      33;
  const auto recordSize = duplicate[recordStart];
  std::copy_n(duplicate.begin() + recordStart, recordSize,
              duplicate.begin() + recordStart + recordSize);
  write(directory / "duplicate.iso", duplicate);
  rejects([&] { gameDataDirectory(directory / "duplicate.iso"); },
          "Duplicate normalized paths must not overwrite data");

  auto unicode = iso(true);
  const std::string mixedCase = "Read Me.txt;1";
  std::vector<Uint8> utf16;
  for (char c : mixedCase) {
    utf16.push_back(0);
    utf16.push_back(Uint8(c));
  }
  auto name =
      std::search(unicode.begin(), unicode.end(), utf16.begin(), utf16.end());
  require(name != unicode.end(), "Locate Joliet filename fixture");
  name[10] = 0x96;
  name[11] = 0xe8;
  write(directory / "unicode.iso", unicode);
  const auto unicodeRoot = gameDataDirectory(directory / "unicode.iso");
  require(std::filesystem::is_regular_file(
              unicodeRoot / std::filesystem::u8path(u8"Read \u96e8e.txt")),
          "Joliet UTF-16 names materialize as native Unicode paths");
  name[10] = name[11] = 0;
  write(directory / "nul.iso", unicode);
  rejects([&] { gameDataDirectory(directory / "nul.iso"); },
          "Embedded Joliet NUL must not truncate a filename");
  std::ifstream original(imagePath, std::ios::binary);
  require(std::vector<Uint8>(std::istreambuf_iterator<char>(original), {}) ==
              sourceBytes,
          "Internal ISO mounting leaves every source byte unchanged");
  const auto patch = directory / "overlay";
  std::filesystem::create_directories(patch / "movie");
  write(patch / "readme.txt", {'p', 'a', 't', 'c', 'h'});
  write(patch / "movie/open.pss", {9, 8, 7});
  write(patch / "added.txt", {6});
  FileSystemAssetProvider folder(gameDataDirectory(directory / "test.iso"));
  IndexedFileSystemAssetProvider mountedISO(directory / "test.iso");
  DiscImageAssetProvider directDisc(directory / "test.bin");
  FileSystemAssetProvider directFiles;
  for (AssetProvider *source : {static_cast<AssetProvider *>(&folder),
                                static_cast<AssetProvider *>(&mountedISO),
                                static_cast<AssetProvider *>(&directDisc),
                                static_cast<AssetProvider *>(&directFiles)}) {
    source->setOverlay(patch);
    require(
        source->read("README.TXT") ==
                std::vector<Uint8>({'p', 'a', 't', 'c', 'h'}) &&
            source->read("MOVIE/OPEN.PSS") == std::vector<Uint8>({9, 8, 7}) &&
            source->contains("ADDED.TXT") &&
            std::filesystem::equivalent(source->localPath("README.TXT"),
                                        patch / "readme.txt"),
        "The same overlay patches folders, internally mounted ISO, direct disc "
        "resources and loose file providers, including streaming paths");
    const auto ids = source->list("movie/");
    require(ids.size() == 1,
            "Overlay listings replace rather than duplicate resources");
    source->setOverlay({});
  }
  require(folder.read("README.TXT") ==
                  std::vector<Uint8>({'h', 'e', 'l', 'l', 'o'}) &&
              mountedISO.read("readme.txt") == folder.read("README.TXT") &&
              directDisc.read("README.TXT") == folder.read("README.TXT"),
          "Removing a patch restores all original source types without "
          "modification");
}

std::vector<Uint8> bitmap(int w, int h, SDL_Color color) {
  SurfacePtr surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
  require(surface != nullptr, "Create bitmap");
  require(SDL_FillSurfaceRect(surface.get(), nullptr,
                              SDL_MapSurfaceRGBA(surface.get(), color.r,
                                                 color.g, color.b, 255)),
          "Fill bitmap");
  SDL_IOStream *stream = SDL_IOFromDynamicMem();
  require(stream && SDL_SaveBMP_IO(surface.get(), stream, false),
          "Encode bitmap");
  std::vector<Uint8> bytes(std::size_t(SDL_TellIO(stream)));
  SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
  require(SDL_ReadIO(stream, bytes.data(), bytes.size()) == bytes.size(),
          "Read bitmap");
  SDL_CloseIO(stream);
  return bytes;
}

std::vector<Uint8> wave(int samples) {
  std::vector<Uint8> bytes;
  auto u32 = [&](Uint32 v) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(Uint8(v >> (8 * i)));
  };
  auto u16 = [&](Uint16 v) {
    bytes.push_back(Uint8(v));
    bytes.push_back(Uint8(v >> 8));
  };
  auto tag = [&](const char *t) { bytes.insert(bytes.end(), t, t + 4); };
  tag("RIFF");
  u32(36 + Uint32(samples) * 4);
  tag("WAVE");
  tag("fmt ");
  u32(16);
  u16(1);
  u16(2);
  u32(8000);
  u32(32000);
  u16(4);
  u16(16);
  tag("data");
  u32(Uint32(samples) * 4);
  for (int i = 0; i < samples; ++i) {
    u16(16384);
    u16(Uint16(-16384));
  }
  return bytes;
}

void decoder() {
  const auto picture =
      MediaDecoder::decodePicture(bitmap(5, 3, {200, 100, 50, 255}));
  require(picture->w == 5 && picture->h == 3 &&
              picture->format == SDL_PIXELFORMAT_RGBA32,
          "FFmpeg picture");
  Uint8 r, g, b, a;
  require(SDL_ReadSurfacePixel(picture.get(), 2, 1, &r, &g, &b, &a) &&
              r == 200 && g == 100 && b == 50 && a == 255,
          "FFmpeg picture colour");
  const auto clip = MediaDecoder::decodeAudio(wave(800));
  require(clip.spec.format == SDL_AUDIO_F32 && clip.spec.channels == 2 &&
              clip.spec.freq == 8000 &&
              clip.data.size() == 800 * 2 * sizeof(float),
          "FFmpeg audio");
  float left = 0, right = 0;
  std::memcpy(&left, clip.data.data(), 4);
  std::memcpy(&right, clip.data.data() + 4, 4);
  require(std::fabs(left - 0.5f) < 1e-3 && std::fabs(right + 0.5f) < 1e-3,
          "FFmpeg audio samples");
  require(std::fabs(MediaDecoder::duration(wave(800)) - 0.1) < 1e-3,
          "Media duration");
  rejects([] { MediaDecoder::decodePicture({1, 2, 3}); },
          "Reject undecodable picture");
  rejects([&] { MediaDecoder::decodeAudio(bitmap(2, 2, {})); },
          "Reject audio without a stream");
}

void shaderPresets() {
  const std::vector<std::vector<std::string>> modes{
      {"Restore_CNN_VL", "Upscale_CNN_x2_VL", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Upscale_CNN_x2_M"},
      {"Restore_CNN_Soft_VL", "Upscale_CNN_x2_VL", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Upscale_CNN_x2_M"},
      {"Upscale_Denoise_CNN_x2_VL", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Upscale_CNN_x2_M"},
      {"Restore_CNN_VL", "Upscale_CNN_x2_VL", "Restore_CNN_M",
       "AutoDownscalePre_x2", "AutoDownscalePre_x4", "Upscale_CNN_x2_M"},
      {"Restore_CNN_Soft_VL", "Upscale_CNN_x2_VL", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Restore_CNN_Soft_M", "Upscale_CNN_x2_M"},
      {"Upscale_Denoise_CNN_x2_VL", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Restore_CNN_M", "Upscale_CNN_x2_M"},
      {"Restore_CNN_M", "Upscale_CNN_x2_M", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Upscale_CNN_x2_S"},
      {"Restore_CNN_Soft_M", "Upscale_CNN_x2_M", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Upscale_CNN_x2_S"},
      {"Upscale_Denoise_CNN_x2_M", "AutoDownscalePre_x2", "AutoDownscalePre_x4",
       "Upscale_CNN_x2_S"},
      {"Restore_CNN_M", "Upscale_CNN_x2_M", "Restore_CNN_S",
       "AutoDownscalePre_x2", "AutoDownscalePre_x4", "Upscale_CNN_x2_S"},
      {"Restore_CNN_Soft_M", "Upscale_CNN_x2_M", "AutoDownscalePre_x2",
       "AutoDownscalePre_x4", "Restore_CNN_Soft_S", "Upscale_CNN_x2_S"},
      {"Upscale_Denoise_CNN_x2_M", "AutoDownscalePre_x2", "AutoDownscalePre_x4",
       "Restore_CNN_S", "Upscale_CNN_x2_S"}};
  for (std::size_t i = 0; i < modes.size(); ++i) {
    std::vector<std::string> expected{"Anime4K_Clamp_Highlights.glsl"};
    for (const auto &name : modes[i])
      expected.push_back("Anime4K_" + name + ".glsl");
    const auto preset = UpscalePresets()[i];
    require(UpscalePresetIsAnime4K(preset), "Anime4K preset classification");
    require(UpscalePresetShaderFiles(preset) == expected,
            "Anime4K chain matches requested sequence");
    UpscaleSettings settings{UpscaleBackend::MPV, preset, 2, {}};
    settings.validate();
  }
  require(static_cast<int>(UpscalePreset::ArtCNN) == 1 &&
              static_cast<int>(UpscalePreset::ArtCNNLarge) == 3,
          "Existing ArtCNN settings remain compatible");
}

void scaling() {
  MemoryAssetProvider assets;
  assets.files["pic"] = bitmap(4, 2, {0, 128, 255, 255});
  assets.files["song.wav"] = wave(400);
  SurfacePtr target(SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32));
  RendererPtr renderer(SDL_CreateSoftwareRenderer(target.get()));
  require(renderer != nullptr, "Create renderer");
  Upscaling upscaling(*renderer);
  require(upscaling.getSettings().backend == UpscaleBackend::Auto &&
              !upscaling.getSettings().enabled() &&
              upscaling.activeBackend() == UpscaleBackend::None,
          "Upscaler defaults to Auto, off at 1.0x");

  SDL_Texture &linear = assets.texture(*renderer, "pic", 2.5f, &upscaling);
  require(linear.w == 4 && linear.h == 2,
          "Disabled pipeline returns original size");
  require(&assets.texture(*renderer, "pic", 2.5f, &upscaling) == &linear,
          "Scaled texture cache");
  require(&assets.texture(*renderer, "pic", 1, &upscaling) !=
              &assets.texture(*renderer, "pic", 0.5f, nullptr),
          "Scale keys");
  require(assets.texture(*renderer, "pic", 0.5f, nullptr).w == 2,
          "Downscale without upscaler");

  // The factor is the switch: 1.0 keeps the backend off even if one is chosen.
  upscaling.configure({UpscaleBackend::Linear, UpscalePreset::Anime4K, 1, {}});
  require(upscaling.activeBackend() == UpscaleBackend::None &&
              !upscaling.getImplementation(),
          "Factor 1.0 disables the backend");
  upscaling.configure(
      {UpscaleBackend::Auto, UpscalePreset::Anime4K, 1.25f, {}});
  require(upscaling.activeBackend() == UpscaleBackend::Linear &&
              upscaling.resolve(UpscaleBackend::Auto) == UpscaleBackend::Linear,
          "Auto resolves to Linear on the software renderer");
  rejects(
      [&] {
        upscaling.configure(
            {UpscaleBackend::Auto, UpscalePreset::Anime4K, 0.5f, {}});
      },
      "Reject factor below 1.0");
  upscaling.configure({UpscaleBackend::Linear, UpscalePreset::Anime4K, 2, {}});
  const SurfacePtr upscaled = assets.surface(*renderer, "pic", 3, &upscaling);
  require(upscaled->w == 12 && upscaled->h == 6, "Upscaled surface size");
  Uint8 r, g, b, a;
  require(SDL_ReadSurfacePixel(upscaled.get(), 6, 3, &r, &g, &b, &a) &&
              r == 0 && g == 128 && b == 255,
          "Upscaled surface colour");
  require(upscaling.getSettings().factor == 2,
          "One-shot scale restores the configured factor");
  const SurfacePtr big = assets.surface(*renderer, "pic", 6, &upscaling);
  require(big->w == 24 && big->h == 12, "Scale above the backend limit");
  require(assets.surface(*renderer, "pic", 2, nullptr)->w == 8,
          "CPU scale without upscaler");
  rejects([&] { assets.texture(*renderer, "pic", 0, &upscaling); },
          "Reject zero scale");
  require(Upscaling::forRenderer(*renderer) == &upscaling,
          "Renderer exposes its configured engine pipeline");
  auto &original = assets.texture(*renderer, "pic");
  SDL_FRect crop{1, 0, 2, 2}, destination{4, 4, 16, 16};
  require(RenderUpscaledTexture(renderer.get(), &original, &crop, &destination),
          "Game images automatically use the renderer pipeline");
  require(SDL_FlushRenderer(renderer.get()), "Flush automatic upscale draw");
  require(SDL_ReadSurfacePixel(target.get(), 10, 10, &r, &g, &b, &a) &&
              r == 0 && g == 128 && b == 255,
          "Automatic upscale preserves crop and destination");
  assets.clearCache();
}

void player() {
  MemoryAssetProvider assets;
  assets.files["still"] = bitmap(4, 4, {255, 0, 0, 255});
  assets.files["track.wav"] = wave(800);
  SurfacePtr target(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32));
  RendererPtr renderer(SDL_CreateSoftwareRenderer(target.get()));
  require(renderer != nullptr, "Create renderer");
  AudioMixer mixer;
  MediaPlayer media(mixer);
  require(!media.isOpen() && media.finished() && media.duration() < 0,
          "Idle player");
  rejects([&] { media.openSoundtrack(assets, "track.wav"); },
          "Soundtrack needs media");
  media.open(assets, "still", false, "movie");
  media.openSoundtrack(assets, "track.wav");
  require(media.isOpen() && !media.finished(), "Media with soundtrack");
  media.setPaused(true);
  const double paused = media.elapsed();
  SDL_Delay(30);
  require(media.elapsed() == paused, "Clock stops while paused");
  media.setPaused(false);
  const Uint64 start = SDL_GetTicks();
  while (!media.finished() && SDL_GetTicks() - start < 3000) {
    media.update(*renderer);
    SDL_Delay(10);
  }
  require(media.hasVideoFrame(), "Self-timed video frame");
  require(media.finished(), "Video and soundtrack finish");
  require(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) &&
              SDL_RenderClear(renderer.get()),
          "Clear");
  media.render(*renderer, SDL_FRect{0, 0, 16, 8}, nullptr);
  require(SDL_RenderPresent(renderer.get()), "Present");
  Uint8 r, g, b, a;
  require(SDL_ReadSurfacePixel(target.get(), 8, 4, &r, &g, &b, &a) &&
              r == 255 && g == 0,
          "Letterboxed frame centre");
  require(SDL_ReadSurfacePixel(target.get(), 1, 4, &r, &g, &b, &a) && r == 0,
          "Letterbox bars");
  media.render(*renderer, SDL_FRect{0, 0, 16, 8}, nullptr, true);
  require(SDL_RenderPresent(renderer.get()), "Present stretched");
  require(SDL_ReadSurfacePixel(target.get(), 1, 4, &r, &g, &b, &a) && r == 255,
          "Stretched frame");
  media.stop();
  require(media.finished(), "Stopped player is finished");
}

} // namespace

int main() {
  try {
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "Initialize SDL");
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("kotonoha-formats-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(directory);
    try {
      encoding();
      discImage(directory);
      decoder();
      shaderPresets();
      scaling();
      player();
    } catch (...) {
      std::filesystem::remove_all(directory);
      throw;
    }
    std::filesystem::remove_all(directory);
    SDL_Quit();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Encoding, disc image and media decoding tests passed\n";
  return 0;
}
