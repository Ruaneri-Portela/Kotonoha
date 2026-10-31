#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
  bool contains(const std::string &id) const override {
    return files.count(id) != 0;
  }
  std::vector<Uint8> read(const std::string &id) const override {
    ++reads;
    const auto found = files.find(id);
    if (found == files.end())
      throw std::runtime_error("Missing " + id);
    return found->second;
  }
  std::vector<std::string> list(const std::string &prefix) const override {
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
  bool contains(const std::string &id) const override {
    return id.rfind("solid:", 0) == 0;
  }
  std::vector<Uint8> read(const std::string &id) const override {
    return {id.begin(), id.end()};
  }
  SurfacePtr loadSurface(const std::string &id) const override {
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

std::vector<Uint8> bitmap() {
  SurfacePtr surface(SDL_CreateSurface(3, 2, SDL_PIXELFORMAT_RGBA32));
  require(surface != nullptr, "Create bitmap");
  require(SDL_FillSurfaceRect(surface.get(), nullptr,
                              SDL_MapSurfaceRGBA(surface.get(), 10, 20, 30,
                                                 255)),
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
  const auto root = std::filesystem::temp_directory_path() /
                    ("kotonoha-assets-" +
                     std::to_string(SDL_GetPerformanceCounter()));
  std::filesystem::create_directories(root / "pictures");
  writeTextFileAtomically((root / "pictures/a.txt").string(), "hello");
  std::filesystem::create_directories(root.parent_path() / "outside");
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

    FileSystemAssetProvider plain;
    require(plain.localPath("pictures/a.txt") == "pictures/a.txt",
            "Unrooted identifiers are paths");
  } catch (...) {
    std::filesystem::remove_all(root);
    throw;
  }
  std::filesystem::remove_all(root);
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
    fileSystemProvider();
    decoders();
    if (argc > 1)
      memoryMedia(argv[1]);
    // Engines own the SDL session and quit SDL when destroyed.
    engineProvider();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Asset provider tests passed\n";
  return 0;
}
