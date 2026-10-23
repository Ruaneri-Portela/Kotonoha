// SnowExtract <PACK.DAT> <outdir> [--png]
// Lists a Snow Radish pack and writes each (decompressed) entry; .EVD images
// can also be written as PNG.
#include "SnowRadish/Pack.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <PACK.DAT> <outdir> [--png]\n", argv[0]);
        return 2;
    }
    const bool png = argc > 3 && std::strcmp(argv[3], "--png") == 0;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> pack((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto entries = SnowRadish::readDirectory(pack);
    if (entries.empty()) {
        std::fprintf(stderr, "no directory found\n");
        return 1;
    }
    fs::create_directories(argv[2]);
    int images = 0;
    for (const auto& e : entries) {
        auto data = SnowRadish::extractEntry(pack, e);
        std::ofstream(fs::path(argv[2]) / e.name, std::ios::binary)
            .write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        SnowRadish::Image img;
        if (png && SnowRadish::decodeEvd(data, img)) {
            SDL_Surface* s = SDL_CreateSurfaceFrom(static_cast<int>(img.width), static_cast<int>(img.height),
                                                   SDL_PIXELFORMAT_RGB24, img.rgb.data(), static_cast<int>(img.width * 3));
            if (s) {
                SDL_SavePNG(s, (fs::path(argv[2]) / (e.name + ".png")).string().c_str());
                SDL_DestroySurface(s);
                ++images;
            }
        } else if (png) {
            SnowRadish::Sprite sp;
            if (SnowRadish::decodeChd(data, sp)) {
                SDL_Surface* s = SDL_CreateSurfaceFrom(static_cast<int>(sp.width), static_cast<int>(sp.height),
                                                       SDL_PIXELFORMAT_RGBA32, sp.rgba.data(), static_cast<int>(sp.width * 4));
                if (s) {
                    SDL_SavePNG(s, (fs::path(argv[2]) / (e.name + ".png")).string().c_str());
                    SDL_DestroySurface(s);
                    ++images;
                }
            }
        }
    }
    std::printf("%zu entries, %d PNG\n", entries.size(), images);
    return 0;
}
