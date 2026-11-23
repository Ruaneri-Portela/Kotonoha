#include <Kotonoha/media/SaveImage.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool result, const char *reason) {
  if (!result)
    throw std::runtime_error(reason);
}

void checkFile(const std::filesystem::path &path,
               const std::vector<std::uint8_t> &magic) {
  require(std::filesystem::exists(path), "encoded image file is missing");
  require(std::filesystem::file_size(path) > magic.size(),
          "encoded image file is empty or truncated");
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "cannot open encoded image file");
  std::vector<std::uint8_t> header(magic.size());
  input.read(reinterpret_cast<char *>(header.data()),
             static_cast<std::streamsize>(header.size()));
  require(input.gcount() == static_cast<std::streamsize>(magic.size()) &&
              header == magic,
          "encoded image has the wrong magic bytes");
}

} // namespace

int main() {
  try {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("kotonoha-saveimage-" + std::to_string(SDL_GetTicks()));
    std::filesystem::create_directories(directory);

    SDL_Surface *raw = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    require(raw != nullptr, SDL_GetError());
    struct SurfaceGuard {
      SDL_Surface *surface;
      ~SurfaceGuard() { SDL_DestroySurface(surface); }
    } guard{raw};
    require(SDL_FillSurfaceRect(raw, nullptr,
                                SDL_MapSurfaceRGBA(raw, 179, 101, 37, 255)),
            SDL_GetError());

    const auto png = directory / "sample.png";
    const auto jpeg = directory / "sample.jpg";
    require(Kotonoha::SaveImage(raw, png.string().c_str(),
                                Kotonoha::ImageFormat::Png),
            "FFmpeg PNG encoding failed");
    require(Kotonoha::SaveImage(raw, jpeg.string().c_str(),
                                Kotonoha::ImageFormat::Jpeg),
            "FFmpeg JPEG encoding failed");

    checkFile(png, {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a});
    checkFile(jpeg, {0xff, 0xd8, 0xff});

    std::filesystem::remove_all(directory);
    std::cout << "SaveImageTests PASS\n";
  } catch (const std::exception &error) {
    std::cerr << "SaveImageTests FAIL: " << error.what() << '\n';
    return 1;
  }
}
