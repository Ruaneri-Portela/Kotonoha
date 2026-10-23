// SnowExtract <PACK.DAT> <outdir> [--png]
// Lists a Snow Radish pack and writes each (decompressed) entry; supported pictures,
// animation frames, and alpha masks can also be written as PNG.
#include "SnowRadish/Pack.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <algorithm>
#include <cctype>
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
        std::string ext = fs::path(e.name).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (png && (ext == ".evd" || ext == ".img") && SnowRadish::decodeEvd(data, img)) {
            SDL_Surface* s = SDL_CreateSurfaceFrom(static_cast<int>(img.width), static_cast<int>(img.height),
                                                   SDL_PIXELFORMAT_RGB24, img.rgb.data(), static_cast<int>(img.width * 3));
            if (s) {
                SDL_SavePNG(s, (fs::path(argv[2]) / (e.name + ".png")).string().c_str());
                SDL_DestroySurface(s);
                ++images;
            }
        } else if (png && ext == ".chd") {
            SnowRadish::Sprite sp;
            if (SnowRadish::decodeChd(data, sp)) {
                // Placement on the 800x600 canvas, for laying out the original UI.
                std::printf("%s at %d,%d size %ux%u\n", e.name.c_str(), sp.x, sp.y, sp.width, sp.height);
                SDL_Surface* s = SDL_CreateSurfaceFrom(static_cast<int>(sp.width), static_cast<int>(sp.height),
                                                       SDL_PIXELFORMAT_RGBA32, sp.rgba.data(), static_cast<int>(sp.width * 4));
                if (s) {
                    SDL_SavePNG(s, (fs::path(argv[2]) / (e.name + ".png")).string().c_str());
                    SDL_DestroySurface(s);
                    ++images;
                }
            }
        } else if (png && ext == ".anm") {
            SnowRadish::Animation animation;
            if (SnowRadish::decodeAnm(data, animation)) {
                bool saved = true;
                for (size_t i = 0; i < animation.frames.size(); ++i) {
                    auto& frame = animation.frames[i];
                    SDL_Surface* s = SDL_CreateSurfaceFrom(static_cast<int>(frame.width), static_cast<int>(frame.height),
                                                           SDL_PIXELFORMAT_RGBA32, frame.rgba.data(),
                                                           static_cast<int>(frame.width * 4));
                    if (!s) { saved = false; break; }
                    const std::string name = e.name + ".frame" + (i < 10 ? "0" : "") + std::to_string(i) + ".png";
                    const bool ok = SDL_SavePNG(s, (fs::path(argv[2]) / name).string().c_str());
                    SDL_DestroySurface(s);
                    if (!ok) { saved = false; break; }
                }
                if (saved) ++images;
            }
        } else if (png && (ext == ".alpf" || ext == ".alpa")) {
            SnowRadish::AlphaMask mask;
            if (SnowRadish::decodeAlpha(data, ext == ".alpa", mask)) {
                std::vector<uint8_t> rgb(mask.alpha.size() * 3);
                for (size_t i = 0; i < mask.alpha.size(); ++i)
                    rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = mask.alpha[i];
                SDL_Surface* s = SDL_CreateSurfaceFrom(static_cast<int>(mask.width), static_cast<int>(mask.height),
                                                       SDL_PIXELFORMAT_RGB24, rgb.data(), static_cast<int>(mask.width * 3));
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
