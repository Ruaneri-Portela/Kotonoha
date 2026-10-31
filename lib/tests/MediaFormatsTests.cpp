#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

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
  bool contains(const std::string &id) const override {
    return files.count(id) != 0;
  }
  std::vector<Uint8> read(const std::string &id) const override {
    const auto found = files.find(id);
    if (found == files.end())
      throw std::runtime_error("Missing " + id);
    return found->second;
  }
};

void encoding() {
  // "こんにちは" + NEC "①" + CP932 wave dash "～" + half-width "ｱ".
  const std::string sjis =
      "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd\x87\x40\x81\x60\xb1" "A";
  require(ShiftJisToUtf8(sjis) ==
              "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF"
              "\xE2\x91\xA0\xEF\xBD\x9E\xEF\xBD\xB1"
              "A",
          "CP932 to UTF-8");
  // An unmapped pair becomes one '?', and the trail byte is not reused.
  require(ShiftJisToUtf8("a\x85\x40" "z") == "a?z", "Invalid pair");
  require(ShiftJisToUtf8("") == "", "Empty text");
  require(ConvertEncoding("caf\xe9", "LATIN1") == "caf\xC3\xA9",
          "Generic conversion");
  rejects([] { ConvertEncoding("x", "NOT-AN-ENCODING"); },
          "Reject unknown encoding");
}

// ISO9660 image: README.TXT and MOVIE/OPEN.PSS (3000 bytes over 2 sectors).
std::vector<Uint8> iso() {
  std::vector<Uint8> disc(23 * 2048, 0);
  auto u32 = [&](std::size_t at, Uint32 v) {
    for (int i = 0; i < 4; ++i)
      disc[at + i] = Uint8(v >> (8 * i));
  };
  auto record = [&](std::size_t &at, const std::string &name, Uint32 lba,
                    Uint32 size, bool dir) {
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
  at = 18 * 2048;
  record(at, std::string(1, '\0'), 18, 2048, true);
  record(at, std::string(1, '\1'), 18, 2048, true);
  record(at, "MOVIE", 19, 2048, true);
  record(at, "README.TXT;1", 20, 5, false);
  at = 19 * 2048;
  record(at, std::string(1, '\0'), 19, 2048, true);
  record(at, std::string(1, '\1'), 18, 2048, true);
  record(at, "OPEN.PSS;1", 21, 3000, false);
  std::memcpy(&disc[20 * 2048], "hello", 5);
  for (std::size_t i = 0; i < 3000; ++i)
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
  require(linear.w == 10 && linear.h == 5, "Linear scale size");
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
  upscaling.configure({UpscaleBackend::Auto, UpscalePreset::Anime4K, 1.25f, {}});
  require(upscaling.activeBackend() == UpscaleBackend::Linear &&
              upscaling.resolve(UpscaleBackend::Auto) == UpscaleBackend::Linear,
          "Auto resolves to Linear on the software renderer");
  rejects([&] {
    upscaling.configure({UpscaleBackend::Auto, UpscalePreset::Anime4K, 0.5f, {}});
  }, "Reject factor below 1.0");
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
  require(SDL_ReadSurfacePixel(target.get(), 1, 4, &r, &g, &b, &a) &&
              r == 255,
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
