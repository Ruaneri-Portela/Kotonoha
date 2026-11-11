#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

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

// A provider without files, like an archive or disc image.
class MemoryAssetProvider final : public AssetProvider {
public:
  std::map<std::string, std::vector<Uint8>> files;
  mutable int reads = 0;

  std::string name() const override { return "Memory"; }
  bool containsAsset(const std::string &id) const override {
    return files.count(id) != 0;
  }
  std::vector<Uint8> readAsset(const std::string &id) const override {
    ++reads;
    const auto found = files.find(id);
    if (found == files.end())
      throw std::runtime_error("Missing " + id);
    return found->second;
  }
  std::vector<std::string> listAsset(const std::string &prefix) const override {
    std::vector<std::string> result;
    for (const auto &[id, bytes] : files)
      if (id.compare(0, prefix.size(), prefix) == 0)
        result.push_back(id);
    return result;
  }
};

// A custom decoder: "solid:RRGGBB" pictures are generated, not stored.
class SolidColorProvider final : public AssetProvider {
public:
  std::string name() const override { return "Solid"; }
  bool containsAsset(const std::string &id) const override {
    return id.rfind("solid:", 0) == 0;
  }
  std::vector<Uint8> readAsset(const std::string &id) const override {
    return {id.begin(), id.end()};
  }
  SurfacePtr loadSurfaceAsset(const std::string &id) const override {
    const auto color = std::stoul(id.substr(6), nullptr, 16);
    SurfacePtr surface(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32));
    require(surface != nullptr, "Create solid surface");
    require(SDL_FillSurfaceRect(
                surface.get(), nullptr,
                SDL_MapSurfaceRGBA(surface.get(), (color >> 16) & 255,
                                   (color >> 8) & 255, color & 255, 255)),
            "Fill solid surface");
    return surface;
  }
};

class VectorAssetStream final : public AssetStream {
  std::vector<Uint8> bytes;
  std::size_t offset = 0;

public:
  explicit VectorAssetStream(std::vector<Uint8> data)
      : bytes(std::move(data)) {}
  std::uint64_t size() const override { return bytes.size(); }
  std::uint64_t tell() const override { return offset; }
  std::size_t read(Uint8 *buffer, std::size_t count) override {
    const auto available = bytes.size() - offset;
    count = std::min(count, available);
    std::copy_n(bytes.data() + offset, count, buffer);
    offset += count;
    return count;
  }
  bool seek(std::uint64_t position) override {
    if (position > bytes.size())
      return false;
    offset = static_cast<std::size_t>(position);
    return true;
  }
};

class StreamingAssetProvider final : public AssetProvider {
public:
  std::vector<Uint8> bytes;
  mutable int reads = 0, streams = 0;

  std::string name() const override { return "Streaming"; }
  bool containsAsset(const std::string &id) const override {
    return id == "image.bmp";
  }
  std::vector<Uint8> readAsset(const std::string &id) const override {
    ++reads;
    if (id != "image.bmp")
      throw std::runtime_error("Missing " + id);
    return bytes;
  }
  std::unique_ptr<AssetStream>
  openStream(const std::string &id) const override {
    if (id != "image.bmp")
      return {};
    ++streams;
    return std::make_unique<VectorAssetStream>(bytes);
  }
};

std::vector<Uint8> bitmap() {
  SurfacePtr surface(SDL_CreateSurface(3, 2, SDL_PIXELFORMAT_RGBA32));
  require(surface != nullptr, "Create bitmap");
  require(
      SDL_FillSurfaceRect(surface.get(), nullptr,
                          SDL_MapSurfaceRGBA(surface.get(), 10, 20, 30, 255)),
      "Fill bitmap");
  SDL_IOStream *stream = SDL_IOFromDynamicMem();
  require(stream != nullptr, "Open bitmap stream");
  require(SDL_SaveBMP_IO(surface.get(), stream, false), "Encode bitmap");
  const auto size = SDL_TellIO(stream);
  std::vector<Uint8> bytes(static_cast<std::size_t>(size));
  SDL_SeekIO(stream, 0, SDL_IO_SEEK_SET);
  require(SDL_ReadIO(stream, bytes.data(), bytes.size()) == bytes.size(),
          "Read bitmap");
  SDL_CloseIO(stream);
  return bytes;
}

std::vector<Uint8> wave() {
  std::vector<Uint8> bytes;
  const auto u32 = [&](Uint32 v) {
    for (int i = 0; i < 4; ++i)
      bytes.push_back(static_cast<Uint8>(v >> (8 * i)));
  };
  const auto u16 = [&](Uint16 v) {
    bytes.push_back(static_cast<Uint8>(v));
    bytes.push_back(static_cast<Uint8>(v >> 8));
  };
  const auto tag = [&](const char *t) { bytes.insert(bytes.end(), t, t + 4); };
  tag("RIFF");
  u32(36 + 8);
  tag("WAVE");
  tag("fmt ");
  u32(16);
  u16(1);
  u16(1);
  u32(8000);
  u32(16000);
  u16(2);
  u16(16);
  tag("data");
  u32(8);
  for (Uint16 sample : {0, 1000, 0xffff, 42})
    u16(sample);
  return bytes;
}

void fileSystemProvider() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("kotonoha-assets-" + std::to_string(SDL_GetPerformanceCounter()));
  std::filesystem::create_directories(root / "pictures");
  writeTextFileAtomically((root / "pictures/a.txt").string(), "hello");
  const auto outside = std::filesystem::path(root.string() + "-outside");
  std::filesystem::create_directories(outside);
  try {
    FileSystemAssetProvider assets(root);
    require(assets.contains("pictures/a.txt"), "Contains rooted asset");
    require(!assets.contains("pictures/missing.txt"), "Missing asset");
    const auto bytes = assets.read("pictures/./a.txt");
    require(std::string(bytes.begin(), bytes.end()) == "hello", "Read asset");
    require(assets.list("pictures/") ==
                std::vector<std::string>{"pictures/a.txt"},
            "List assets");
    require(std::filesystem::path(assets.localPath("pictures/a.txt")) ==
                root / "pictures/a.txt",
            "Local path");
    require(!assets.contains("../outside"), "Escape is not contained");
    rejects([&] { assets.read("../x"); }, "Reject parent escape");
    rejects([&] { assets.read("pictures/../../x"); }, "Reject nested escape");
    rejects([&] { assets.read((root / "pictures/a.txt").string()); },
            "Reject absolute path");
    rejects([&] { assets.read(""); }, "Reject empty identifier");

    IndexedFileSystemAssetProvider indexed(root);
    require(indexed.contains("PICTURES\\A.TXT"),
            "Indexed data normalizes case and Windows separators");
    require(indexed.read("PICTURES/A.TXT") == bytes,
            "Indexed data reads the actual case-preserved path");
    require(indexed.list("PICTURES/") ==
                std::vector<std::string>{"pictures/a.txt"},
            "Indexed data enumerates normalized identifiers");
    require(!indexed.contains("../outside"), "Indexed data rejects escapes");
    rejects([&] { indexed.read("../outside"); }, "Indexed read rejects escape");
    std::error_code linkError;
    std::filesystem::create_directory_symlink(
        outside, root / "external-directory", linkError);
#ifdef _WIN32
    if (linkError.value() == 1314) { // ERROR_PRIVILEGE_NOT_HELD
      std::cout << "SKIP: symlink fixture requires Windows Developer Mode\n";
    } else
#endif
    {
      if (linkError)
        throw std::system_error(linkError);
      writeTextFileAtomically((outside / "leak.txt").string(), "outside");
      std::filesystem::create_symlink(outside / "leak.txt",
                                      root / "external.txt");
      rejects([&] { IndexedFileSystemAssetProvider escaping(root); },
              "Indexed data rejects symlink escapes");
      std::filesystem::remove(root / "external.txt");
      std::filesystem::remove(root / "external-directory");
    }

    FileSystemAssetProvider plain;
    require(plain.localPath("pictures/a.txt") == "pictures/a.txt",
            "Unrooted identifiers are paths");
  } catch (...) {
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside);
    throw;
  }
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside);
}

void decoders() {
  MemoryAssetProvider assets;
  assets.files["ui/frame.bmp"] = bitmap();
  assets.files["se/click.wav"] = wave();
  assets.files["data/broken.bmp"] = {1, 2, 3};
  AssetProvider &base = assets;
  require(base.name() == "Memory", "Virtual name");
  require(base.localPath("ui/frame.bmp").empty(), "No local path");
  require(base.list("ui/") == std::vector<std::string>{"ui/frame.bmp"},
          "Virtual list");

  const SurfacePtr surface = base.loadSurface("ui/frame.bmp");
  require(surface->w == 3 && surface->h == 2, "Decode bitmap");
  Uint8 r = 0, g = 0, b = 0, a = 0;
  require(SDL_ReadSurfacePixel(surface.get(), 1, 1, &r, &g, &b, &a),
          "Read bitmap pixel");
  require(r == 10 && g == 20 && b == 30, "Bitmap colour");
  rejects([&] { base.loadSurface("data/broken.bmp"); }, "Reject bad image");
  rejects([&] { base.loadSurface("missing.bmp"); }, "Reject missing image");

  const AudioClip clip = base.loadAudio("se/click.wav");
  require(clip.spec.freq == 8000 && clip.spec.channels == 1 &&
              clip.spec.format == SDL_AUDIO_S16 && clip.data.size() == 8,
          "Decode wave");
  rejects([&] { base.loadAudio("ui/frame.bmp"); }, "Reject non-wave audio");

  SurfacePtr target(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32));
  RendererPtr renderer(SDL_CreateSoftwareRenderer(target.get()));
  require(renderer != nullptr, "Create renderer");
  SDL_Texture &texture = base.texture(*renderer, "ui/frame.bmp");
  const int reads = assets.reads;
  require(&base.texture(*renderer, "ui/frame.bmp") == &texture &&
              assets.reads == reads && base.cachedTextures() == 1,
          "Texture cache");
  base.clearCache();
  require(base.cachedTextures() == 0, "Clear texture cache");

  SolidColorProvider solid;
  SDL_Texture &red = solid.texture(*renderer, "solid:ff0000");
  float w = 0, h = 0;
  require(SDL_GetTextureSize(&red, &w, &h) && w == 2 && h == 2,
          "Custom decoder texture");
  solid.clearCache();
}

class TestRuntime final : public Runtime {
public:
  TestRuntime() : Runtime("AssetTests", "1", "asset-tests") {}
};

void overlays() {
  struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("kotonoha-overlay-" + std::to_string(SDL_GetTicksNS()));
    Directory() { std::filesystem::create_directories(path); }
    ~Directory() {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  } dir;
  std::filesystem::create_directories(dir.path / "Images");
  const auto bmp = bitmap();
  {
    std::ofstream file(dir.path / "Images" / "Scene.BMP", std::ios::binary);
    file.write(reinterpret_cast<const char *>(bmp.data()), bmp.size());
  }
  {
    std::ofstream file(dir.path / "script.ks");
    file << "translated";
  }
  auto dubbed = wave();
  dubbed.back() = 99;
  {
    std::ofstream file(dir.path / "voice.wav", std::ios::binary);
    file.write(reinterpret_cast<const char *>(dubbed.data()), dubbed.size());
  }
  MemoryAssetProvider provider;
  provider.files["script.ks"] = {'b', 'a', 's', 'e'};
  provider.files["untouched"] = {'o', 'k'};
  provider.files["voice.wav"] = wave();
  provider.setOverlay(dir.path);
  require(provider.contains("images/scene.bmp"),
          "Overlay-only asset is visible");
  auto text = provider.read("SCRIPT.KS");
  require(std::string(text.begin(), text.end()) == "translated",
          "Overlay precedes archive and ignores case");
  require(provider.read("untouched") == std::vector<Uint8>({'o', 'k'}),
          "Missing overlay falls back to archive");
  require(provider.read("script.ks", AssetSource::Original) ==
              std::vector<Uint8>({'b', 'a', 's', 'e'}),
          "Standard original source bypasses the overlay");
  require(!provider.localPath("Images/Scene.bmp").empty(),
          "Streaming opens the overlay file");
  require(provider.hasOverlay("voice.wav"),
          "Provider reports an effective audio overlay");
  const auto effectiveVoice = provider.loadAudio("voice.wav");
  // Simulate a cache warmed by a runtime which supplied already-resolved PCM.
  // forceOriginal must still decode the provider/archive source.
  provider.noteAudio("voice.wav", effectiveVoice);
  const auto originalVoice = provider.loadAudio("voice.wav", true);
  require(effectiveVoice.data != originalVoice.data &&
              originalVoice.data ==
                  provider.loadOriginalAudio("voice.wav").data,
          "Forced-original audio bypasses the overlay");
  require(provider.discardOverlay("voice.wav") &&
              !provider.hasOverlay("voice.wav") &&
              provider.loadAudio("voice.wav").data == originalVoice.data,
          "Discarding an overlay immediately restores original audio");
  require(provider.loadSurface("images/scene.bmp")->w == 3,
          "Overlay image decoder");
  require(provider.list().size() == 4,
          "Overlay and archive listings merge without duplicates");
  require(provider.overlayPath("../script.ks").empty() &&
              provider.overlayPath((dir.path / "script.ks").string()).empty(),
          "Overlay IDs stay relative");
  rejects([&] { provider.setOverlay(dir.path / "missing"); },
          "Reject missing overlay directory");
  require(provider.read("script.ks") == text,
          "Failed mount preserves active overlay");
  provider.setOverlay({});
  require(provider.read("script.ks") ==
              std::vector<Uint8>({'b', 'a', 's', 'e'}),
          "Disabling overlay restores native assets");
}

void engineProvider() {
  TestRuntime runtime;
  {
    Engine defaults(runtime);
    require(dynamic_cast<FileSystemAssetProvider *>(&defaults.GetAssets()),
            "Default file provider");
  }
  auto memory = std::make_shared<MemoryAssetProvider>();
  memory->files["a"] = {1};
  EngineOptions options;
  options.assets = memory;
  Engine engine(runtime, options);
  require(&engine.GetAssets() == memory.get(), "Engine uses option provider");
  engine.SetAssetProvider(std::make_shared<FileSystemAssetProvider>());
  require(engine.GetAssets().name() == "Files", "Replace provider");
  rejects([&] { engine.SetAssetProvider(nullptr); }, "Reject null provider");
  require(engine.GetAssets().name() == "Files", "Keep provider on failure");
}

void seekableMediaProvider() {
  StreamingAssetProvider assets;
  assets.bytes = bitmap();
  auto stream = assets.openStream("image.bmp");
  require(stream && stream->size() == assets.bytes.size(),
          "Open seekable asset");
  Uint8 probe[4]{};
  require(stream->read(probe, sizeof(probe)) == sizeof(probe) &&
              stream->tell() == sizeof(probe),
          "Read seekable asset");
  require(stream->seek(8) && stream->tell() == 8 &&
              !stream->seek(stream->size() + 1),
          "Seek asset range");

  AudioMixer mixer;
  MediaPlayer player(mixer);
  player.open(assets, "image.bmp", false, "stream");
  require(player.isOpen() && assets.streams == 2 && assets.reads == 0,
          "MediaPlayer did not prefer provider stream");
  player.stop();

  MemoryAssetProvider fallback;
  fallback.files["image.bmp"] = assets.bytes;
  player.open(fallback, "image.bmp", false, "memory");
  require(player.isOpen() && fallback.reads == 1,
          "MediaPlayer memory fallback changed");
  player.stop();
}

void textApi() {
  MemoryAssetProvider assets;
  assets.files["sjis"] = {0x82, 0xa0};
  assets.files["utf16"] = {0xff, 0xfe, 0x42, 0x30, 0x3d, 0xd8, 0x00, 0xde};
  assets.files["utf32"] = {0xff, 0xfe, 0, 0, 0x42, 0x30, 0, 0};
  assets.files["utf16be"] = {0xfe, 0xff, 0x30, 0x42};
  assets.files["bom"] = {0xef, 0xbb, 0xbf, 'O', 'K'};
  assets.files["western"] = {0xe7};
  assets.files["empty"] = {};
  require(assets.loadTextUtf8("sjis", "CP932") == "あ", "Decode CP932 text");
  require(assets.loadTextUtf8("utf16") == "あ😀", "Decode UTF-16 surrogate pair");
  require(assets.loadTextUtf8("utf32") == "あ", "Distinguish UTF-32 BOM");
  require(assets.loadTextUtf8("utf16be") == "あ", "Decode big-endian UTF-16");
  require(assets.loadTextUtf8("bom", "CP932") == "OK", "BOM overrides legacy encoding");
  require(assets.loadTextUtf8("western", "CP1252") == "ç", "Decode Portuguese text");
  require(assets.loadTextUtf8("empty").empty(), "Empty text");
  require(assets.read("sjis") == std::vector<Uint8>({0x82, 0xa0}), "Raw bytes preserved");
  rejects([&] { assets.loadTextUtf8("missing"); }, "Missing text is an error");
  rejects([&] { assets.loadTextUtf8("empty", nullptr); }, "Reject null encoding");
}

void memoryMedia(const char *path) {
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "Open test media");
  MemoryAssetProvider assets;
  assets.files["movie"] = {std::istreambuf_iterator<char>(input), {}};
  SurfacePtr target(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32));
  RendererPtr renderer(SDL_CreateSoftwareRenderer(target.get()));
  require(renderer != nullptr, "Create media renderer");
  AudioMixer mixer;
  MediaPlayer player(mixer);
  player.open(assets, "movie", false, "movie");
  require(player.getChannel() == mixer.channel("movie"), "Media channel");
  for (int i = 0; i < 30 && !player.hasVideoFrame(); ++i)
    player.update(*renderer, i / 30.0);
  require(player.hasVideoFrame(), "Decode media from memory");
  player.stop();
  rejects([&] { player.open(assets, "missing", false); },
          "Reject missing media asset");
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO), "Initialize SDL");
    {
      EngineOptions options;
      CommandLine cli("test", options);
      char a0[] = "test", a1[] = "--dump-translation", a2[] = "texts.json",
           a3[] = "--launcher";
      char *args[] = {a0, a1, a2, a3};
      cli.parse(4, args);
      require(options.dumpTranslationPath == "texts.json" && !options.launcher,
              "Engine export bypasses the launcher regardless of option order");
    }
    fileSystemProvider();
    decoders();
    textApi();
    overlays();
    if (argc > 1)
      memoryMedia(argv[1]);
    seekableMediaProvider();
    // Engines own the SDL session and quit SDL when destroyed.
    engineProvider();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Asset provider tests passed\n";
  return 0;
}
