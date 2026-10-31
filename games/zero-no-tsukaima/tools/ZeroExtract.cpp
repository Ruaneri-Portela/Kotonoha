// Extracts a Zero no Tsukaima (PS2) disc image into plain files:
//   ZeroExtract <disc.bin|disc.iso> <outdir> [--png]
// Archives (NAME.HD + NAME.BIN) become <outdir>/NAME/NNNN.{tm2,txt,bin};
// with --png every TIM2 picture is also written as NNNN.png.
#include <Kotonoha/core/DiscImage.hpp>
#include <ZeroNoTsukaima/Archive.hpp>
#include <ZeroNoTsukaima/Lz.hpp>
#include <ZeroNoTsukaima/Tim2.hpp>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_surface.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace ZeroNoTsukaima;

namespace {

bool WriteFile(const fs::path &path, const std::vector<uint8_t> &data) {
  SDL_IOStream *io = SDL_IOFromFile(path.string().c_str(), "wb");
  if (io == nullptr) return false;
  const bool ok = data.empty() || SDL_WriteIO(io, data.data(), data.size()) == data.size();
  SDL_CloseIO(io);
  return ok;
}

bool LooksLikeText(const std::vector<uint8_t> &d) {
  return d.size() > 2 && ((d[0] == '/' && (d[1] == '/' || d[1] == '*')));
}

void SavePng(const fs::path &path, Image &img) {
  SDL_Surface *s = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32,
                                         img.rgba.data(), img.width * 4);
  if (s != nullptr) {
    SDL_SavePNG(s, path.string().c_str());
    SDL_DestroySurface(s);
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <disc.bin|iso> <outdir> [--png]\n", argv[0]);
    return 2;
  }
  const bool png = argc > 3 && std::strcmp(argv[3], "--png") == 0;
  Kotonoha::DiscImage disc;
  std::string error;
  if (!disc.tryOpen(argv[1], &error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  std::printf("volume %s, %zu files\n", disc.volumeLabel().c_str(), disc.files().size());
  const fs::path out = argv[2];
  fs::create_directories(out);

  for (const auto &entry : disc.files()) {
    const auto dot = entry.path.rfind('.');
    const std::string stem = entry.path.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : entry.path.substr(dot);
    if (ext == ".BIN" && disc.find(stem + ".HD") != nullptr) {
      std::vector<uint8_t> hd;
      hd = disc.read(*disc.find(stem + ".HD"));
      const auto index = ParseArchiveIndex(hd);
      const fs::path dir = out / stem;
      fs::create_directories(dir);
      int unpacked = 0, raw = 0, pictures = 0;
      for (size_t i = 0; i < index.size(); ++i) {
        std::vector<uint8_t> blob;
        if (!disc.readInto(entry, index[i].offset, index[i].size, blob)) {
          std::fprintf(stderr, "  %s #%zu: read failed\n", stem.c_str(), i);
          continue;
        }
        std::vector<uint8_t> data;
        const char *suffix = ".bin";
        if (LooksLikeText(blob)) {
          data = blob;
          suffix = ".txt";
          ++raw;
        } else if (Unpack(blob.data(), blob.size(), &data)) {
          ++unpacked;
          if (data.size() > 4 && std::memcmp(data.data(), "TIM2", 4) == 0) suffix = ".tm2";
        } else {
          data = blob;
          ++raw;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "%04zu", i);
        WriteFile(dir / (std::string(name) + suffix), data);
        if (png && std::strcmp(suffix, ".tm2") == 0) {
          Image img;
          if (DecodeTim2(data.data(), data.size(), &img)) {
            SavePng(dir / (std::string(name) + ".png"), img);
            ++pictures;
          }
        }
      }
      std::printf("%s: %zu entries (%d unpacked, %d stored, %d PNG)\n", stem.c_str(),
                  index.size(), unpacked, raw, pictures);
    } else if (ext != ".HD" && stem != "VOICE_ID" && stem != "SOUND_ID") {
      std::vector<uint8_t> data;
      if (disc.readInto(entry, 0, entry.size, data)) {
        fs::create_directories((out / entry.path).parent_path());
        WriteFile(out / entry.path, data);
      }
    }
  }
  return 0;
}
