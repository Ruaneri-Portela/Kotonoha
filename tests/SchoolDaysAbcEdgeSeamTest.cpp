#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/SchoolDaysAbcTexture.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr int kSourceW = 800, kSourceH = 452;
constexpr int kOutputW = 1280, kOutputH = 720;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Raster {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    Raster(int width = kOutputW, int height = kOutputH) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
        Check(surface && renderer, "SDL software raster");
    }
    ~Raster() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (surface) SDL_DestroySurface(surface);
    }
    std::vector<Uint8> Capture() {
        Check(SDL_RenderPresent(renderer), "software present");
        Check(SDL_LockSurface(surface), "lock output surface");
        const auto* begin = static_cast<const Uint8*>(surface->pixels);
        std::vector<Uint8> pixels(begin, begin + surface->pitch * surface->h);
        SDL_UnlockSurface(surface);
        return pixels;
    }
    void Clear() {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        Check(SDL_RenderClear(renderer), "clear test target");
    }
    void Save(const fs::path& path) {
        Check(SDL_SaveBMP(surface, path.string().c_str()), "save local BMP");
    }
};

SDL_Surface* Decode(const fs::path& path) {
    SDL_Surface* surface = Kotonoha_imageCreateSurface(path.string().c_str(), -1, -1);
    Check(surface && surface->w == kSourceW && surface->h == kSourceH,
          "decode real source PNG");
    return surface;
}

std::vector<Uint8> CpuStraightAlpha(SDL_Surface* base, SDL_Surface* overlay) {
    Check(SDL_LockSurface(base) && SDL_LockSurface(overlay), "lock source PNGs");
    const auto* background = static_cast<const Uint8*>(base->pixels);
    const auto* foreground = static_cast<const Uint8*>(overlay->pixels);
    std::vector<Uint8> output(kSourceW * kSourceH * 4);
    for (int y = 0; y < kSourceH; ++y) {
        for (int x = 0; x < kSourceW; ++x) {
            const size_t b = static_cast<size_t>(y) * base->pitch + x * 4;
            const size_t s = static_cast<size_t>(y) * overlay->pitch + x * 4;
            const size_t d = (static_cast<size_t>(y) * kSourceW + x) * 4;
            const double sa = foreground[s + 3] / 255.0;
            const double da = background[b + 3] / 255.0;
            const double oa = sa + da * (1.0 - sa);
            for (int channel = 0; channel < 3; ++channel) {
                const double value = oa == 0.0 ? 0.0 :
                    (foreground[s + channel] * sa +
                     background[b + channel] * da * (1.0 - sa)) / oa;
                output[d + channel] = static_cast<Uint8>(std::lround(value));
            }
            output[d + 3] = static_cast<Uint8>(std::lround(oa * 255.0));
        }
    }
    SDL_UnlockSurface(overlay);
    SDL_UnlockSurface(base);
    return output;
}

SDL_Texture* TextureFromPixels(SDL_Renderer* renderer,
                               const std::vector<Uint8>& pixels) {
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STATIC,
                                            kSourceW, kSourceH);
    Check(texture && SDL_UpdateTexture(texture, nullptr, pixels.data(),
                                       kSourceW * 4), "CPU reference upload");
    Check(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) &&
          SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR),
          "CPU reference texture settings");
    return texture;
}

struct EdgeMetrics {
    double meanRgbError = 0.0;
    double meanDarkDeficit = 0.0;
    size_t darkPixels = 0, pixels = 0;
};

struct RenderResult {
    EdgeMetrics edge;
    std::vector<Uint8> pixels;
};

std::string RgbAt(const std::vector<Uint8>& pixels, int pitch, int x, int y) {
    const size_t p = static_cast<size_t>(y) * pitch + x * 4;
    return std::to_string(pixels[p]) + ',' + std::to_string(pixels[p + 1]) +
           ',' + std::to_string(pixels[p + 2]);
}

int BrightnessAt(const std::vector<Uint8>& pixels, int pitch, int x, int y) {
    const size_t p = static_cast<size_t>(y) * pitch + x * 4;
    return int(pixels[p]) + int(pixels[p + 1]) + int(pixels[p + 2]);
}

void TestOnePixelDilation() {
    std::vector<Uint8> rgba(5 * 5 * 4, 0);
    const size_t center = (2 * 5 + 2) * 4;
    rgba[center] = 90;
    rgba[center + 1] = 120;
    rgba[center + 2] = 150;
    rgba[center + 3] = 128;
    const auto original = rgba;
    Check(Kotonoha::PrepareStraightAlphaForLinearFiltering(
              rgba.data(), 5, 5, 5 * 4) == 8,
          "one-pixel margin must fill exactly eight neighboring texels");
    auto repeated = original;
    Kotonoha::PrepareStraightAlphaForLinearFiltering(
        repeated.data(), 5, 5, 5 * 4);
    Check(repeated == rgba, "one-pixel dilation is not deterministic");
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const size_t p = (y * 5 + x) * 4;
            const bool neighbor = x >= 1 && x <= 3 && y >= 1 && y <= 3 &&
                                  !(x == 2 && y == 2);
            Check(rgba[p + 3] == original[p + 3], "dilation changed alpha");
            if (x == 2 && y == 2)
                Check(std::equal(rgba.begin() + p, rgba.begin() + p + 4,
                                 original.begin() + p),
                      "dilation changed a visible texel");
            else if (neighbor)
                Check(rgba[p] == 90 && rgba[p + 1] == 120 && rgba[p + 2] == 150,
                      "dilation did not copy the nearby visible RGB");
            else
                Check(rgba[p] == 0 && rgba[p + 1] == 0 && rgba[p + 2] == 0,
                      "dilation extended beyond one pixel");
        }
}

EdgeMetrics CompareEdge(const std::vector<Uint8>& candidate,
                        const std::vector<Uint8>& reference,
                        int pitch, int x0, int y0, int x1, int y1) {
    EdgeMetrics result;
    const double sx = static_cast<double>(kOutputW) / kSourceW;
    const double sy = static_cast<double>(kOutputH) / kSourceH;
    const int left = std::lround(x0 * sx), right = std::lround(x1 * sx);
    const int top = std::lround(y0 * sy), bottom = std::lround(y1 * sy);
    for (int y = std::max(0, top - 4); y < std::min(kOutputH, bottom + 4); ++y) {
        for (int x = std::max(0, left - 4); x < std::min(kOutputW, right + 4); ++x) {
            if (std::min(std::abs(x - left), std::abs(x - right)) > 3 &&
                std::min(std::abs(y - top), std::abs(y - bottom)) > 3)
                continue;
            const size_t p = static_cast<size_t>(y) * pitch + x * 4;
            double luminanceDifference = 0.0;
            for (int channel = 0; channel < 3; ++channel) {
                result.meanRgbError += std::abs(int(candidate[p + channel]) -
                                                int(reference[p + channel]));
                luminanceDifference += int(reference[p + channel]) -
                                       int(candidate[p + channel]);
            }
            luminanceDifference /= 3.0;
            if (luminanceDifference > 5.0) ++result.darkPixels;
            result.meanDarkDeficit += std::max(0.0, luminanceDifference);
            ++result.pixels;
        }
    }
    Check(result.pixels != 0, "empty edge comparison");
    result.meanRgbError /= (result.pixels * 3);
    result.meanDarkDeficit /= result.pixels;
    return result;
}

void TestCase(const fs::path& assets, const fs::path& output,
              const fs::path& relativeFolder, const std::string& stem,
              const std::string& key,
              char state) {
    const auto folder = assets / relativeFolder;
    const std::string scene = relativeFolder.filename().string();
    const auto basePath = folder / (stem + ".PNG");
    const auto overlayPath = folder / (stem + key + "." + state + ".PNG");
    Check(fs::is_regular_file(basePath) && fs::is_regular_file(overlayPath),
          "read-only seam assets missing");
    SDL_Surface* baseSurface = Decode(basePath);
    SDL_Surface* overlaySurface = Decode(overlayPath);
    const auto referencePixels = CpuStraightAlpha(baseSurface, overlaySurface);

    Check(SDL_LockSurface(overlaySurface), "lock alpha mask");
    const auto* mask = static_cast<const Uint8*>(overlaySurface->pixels);
    const size_t stagingBytes = static_cast<size_t>(overlaySurface->pitch) * kSourceH;
    std::vector<Uint8> processed(mask, mask + stagingBytes);
    const size_t dilated = Kotonoha::PrepareStraightAlphaForLinearFiltering(
        processed.data(), kSourceW, kSourceH, overlaySurface->pitch);
    Check(dilated > 0, "real overlay has no dilated edge pixels");
    int x0 = kSourceW, y0 = kSourceH, x1 = 0, y1 = 0;
    size_t alphaZero = 0, alphaMid = 0, alphaOpaque = 0;
    size_t changedHiddenRgb = 0;
    for (int y = 0; y < kSourceH; ++y)
        for (int x = 0; x < kSourceW; ++x) {
            const size_t p = static_cast<size_t>(y) * overlaySurface->pitch + x * 4;
            const Uint8 alpha = mask[p + 3];
            Check(processed[p + 3] == alpha, "real overlay alpha changed");
            if (alpha == 0) {
                ++alphaZero;
                Check(mask[p] == 0 && mask[p + 1] == 0 && mask[p + 2] == 0,
                      "transparent RGB is not black as audited");
                if (processed[p] != mask[p] || processed[p + 1] != mask[p + 1] ||
                    processed[p + 2] != mask[p + 2]) {
                    ++changedHiddenRgb;
                    bool adjacentVisible = false;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if (nx >= 0 && nx < kSourceW && ny >= 0 && ny < kSourceH &&
                                mask[static_cast<size_t>(ny) * overlaySurface->pitch +
                                     nx * 4 + 3] > 0)
                                adjacentVisible = true;
                        }
                    Check(adjacentVisible,
                          "hidden RGB changed beyond the one-pixel margin");
                }
            } else {
                Check(std::equal(processed.begin() + p, processed.begin() + p + 4,
                                 mask + p), "visible RGBA changed");
                if (alpha == 255) ++alphaOpaque; else ++alphaMid;
                x0 = std::min(x0, x); y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1);
            }
        }
    SDL_UnlockSurface(overlaySurface);
    Check(alphaZero > 0 && alphaOpaque > 0 && alphaMid == 0 &&
          changedHiddenRgb > 0,
          "unexpected ABC alpha domain");

    if (state == 'A' && (scene == "00-00-A01" || scene == "00-00-A02")) {
        Raster native(kSourceW, kSourceH);
        SDL_Texture* nativeBase = Kotonoha_imageCreateTexture(
            native.renderer, basePath.string().c_str(), -1, -1);
        SDL_Texture* nativeOverlay = Kotonoha_imageCreateTexture(
            native.renderer, overlayPath.string().c_str(), -1, -1);
        Check(nativeBase && nativeOverlay &&
              SDL_SetTextureBlendMode(nativeBase, SDL_BLENDMODE_BLEND) &&
              SDL_SetTextureBlendMode(nativeOverlay, SDL_BLENDMODE_BLEND) &&
              SDL_SetTextureScaleMode(nativeOverlay, SDL_SCALEMODE_NEAREST),
              "native 1:1 straight-alpha setup");
        native.Clear();
        Check(SDL_RenderTexture(native.renderer, nativeBase, nullptr, nullptr) &&
              SDL_RenderTexture(native.renderer, nativeOverlay, nullptr, nullptr),
              "native 1:1 composite");
        const auto nativePixels = native.Capture();
        const std::string nativeLabel = scene + "_" + stem + key + "_A";
        native.Save(output / ("g2_4_1b_" + nativeLabel + "_native_1to1.bmp"));
        double edgeError = 0.0;
        size_t edgeSamples = 0;
        for (int y = std::max(0, y0 - 2); y < std::min(kSourceH, y1 + 2); ++y)
            for (int x = std::max(0, x0 - 2); x < std::min(kSourceW, x1 + 2); ++x) {
                if (x > x0 + 2 && x < x1 - 2 && y > y0 + 2 && y < y1 - 2)
                    continue;
                const size_t got = static_cast<size_t>(y) * native.surface->pitch + x * 4;
                const size_t expected = (static_cast<size_t>(y) * kSourceW + x) * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    edgeError += std::abs(int(nativePixels[got + channel]) -
                                          int(referencePixels[expected + channel]));
                    ++edgeSamples;
                }
            }
        edgeError /= edgeSamples;
        std::cout << nativeLabel << " native_1to1_edge_error=" << edgeError << '\n';
        Check(edgeError < 2.0, "1:1 straight-alpha composite diverges from CPU reference");
        SDL_DestroyTexture(nativeOverlay);
        SDL_DestroyTexture(nativeBase);
    }

    Raster raster;
    SDL_Texture* baseTexture = Kotonoha_imageCreateTexture(
        raster.renderer, basePath.string().c_str(), -1, -1);
    SDL_Texture* overlayTexture = Kotonoha_imageCreateTexture(
        raster.renderer, overlayPath.string().c_str(), -1, -1);
    SDL_Texture* processedTexture = SDL_CreateTexture(raster.renderer,
        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kSourceW, kSourceH);
    Check(baseTexture && overlayTexture && processedTexture &&
          SDL_UpdateTexture(processedTexture, nullptr, processed.data(),
                            overlaySurface->pitch), "real SDL textures");
    SDL_ScaleMode defaultScale = SDL_SCALEMODE_INVALID;
    Check(SDL_GetTextureScaleMode(overlayTexture, &defaultScale),
          "query actual default scale mode");
    Check(SDL_SetTextureBlendMode(baseTexture, SDL_BLENDMODE_BLEND) &&
          SDL_SetTextureBlendMode(overlayTexture, SDL_BLENDMODE_BLEND) &&
          SDL_SetTextureBlendMode(processedTexture, SDL_BLENDMODE_BLEND),
          "straight-alpha SDL blend");
    SDL_Texture* referenceTexture = TextureFromPixels(raster.renderer, referencePixels);
    raster.Clear();
    Check(SDL_RenderTexture(raster.renderer, referenceTexture, nullptr, nullptr),
          "draw CPU reference");
    const auto reference = raster.Capture();
    const std::string label = scene + "_" + stem + key + "_" + state;
    raster.Save(output / ("g2_4_1b_" + label + "_cpu.bmp"));

    const auto renderMode = [&](SDL_Texture* selected, SDL_ScaleMode mode,
                                const char* name) {
        Check(SDL_SetTextureScaleMode(selected, mode), "set ABC scale mode");
        raster.Clear();
        Check(SDL_RenderTexture(raster.renderer, baseTexture, nullptr, nullptr) &&
              SDL_RenderTexture(raster.renderer, selected, nullptr, nullptr),
              "draw scaled base and ABC");
        auto pixels = raster.Capture();
        raster.Save(output / ("g2_4_1b_" + label + "_" + name + ".bmp"));
        if (scene == "00-00-A02" && state == 'A')
            raster.Save(output / (std::string("A02_") + name + ".bmp"));
        return RenderResult{CompareEdge(pixels, reference, raster.surface->pitch,
                                        x0, y0, x1, y1), std::move(pixels)};
    };
    const auto linear = renderMode(overlayTexture, SDL_SCALEMODE_LINEAR,
                                   "linear_original");
    const auto nearest = renderMode(overlayTexture, SDL_SCALEMODE_NEAREST,
                                    "nearest");
    const auto processedLinear = renderMode(processedTexture,
        SDL_SCALEMODE_LINEAR, "linear_dilated");
    std::cout << label << " default_scale=" << int(defaultScale)
              << " src=800x452 dst=1280x720 dst_rect=(0,0,1280,720)"
              << " src_rect=full blend=straight"
              << " alpha0=" << alphaZero << " alpha_mid=" << alphaMid
              << " alpha255=" << alphaOpaque
              << " dilated=" << dilated
              << " changed_hidden_rgb=" << changedHiddenRgb
              << " bbox=(" << x0 << ',' << y0 << ',' << x1 << ',' << y1 << ')'
              << " linear_error=" << linear.edge.meanRgbError
              << " linear_dark=" << linear.edge.darkPixels
              << " nearest_error=" << nearest.edge.meanRgbError
              << " nearest_dark=" << nearest.edge.darkPixels
              << " dilated_linear_error=" << processedLinear.edge.meanRgbError
              << " dilated_linear_dark=" << processedLinear.edge.darkPixels
              << '\n';
    Check(defaultScale == SDL_SCALEMODE_LINEAR,
          "default SDL filtering changed; reassess seam contract");
    Check(nearest.edge.darkPixels < linear.edge.darkPixels &&
          nearest.edge.meanRgbError < linear.edge.meanRgbError,
          "nearest did not reduce black edge compared with linear");
    Check(processedLinear.edge.darkPixels <= nearest.edge.darkPixels &&
          processedLinear.edge.meanRgbError < linear.edge.meanRgbError / 4.0,
          "one-pixel dilation did not remove the black linear fringe");
    size_t smoothPixels = 0;
    for (int y = std::max(0, int(std::floor(y0 * double(kOutputH) / kSourceH)));
         y < std::min(kOutputH, int(std::ceil(y1 * double(kOutputH) / kSourceH))); ++y)
        for (int x = std::max(0, int(std::floor(x0 * double(kOutputW) / kSourceW)));
             x < std::min(kOutputW, int(std::ceil(x1 * double(kOutputW) / kSourceW))); ++x) {
            const size_t p = static_cast<size_t>(y) * raster.surface->pitch + x * 4;
            for (int channel = 0; channel < 3; ++channel)
                if (std::abs(int(processedLinear.pixels[p + channel]) -
                             int(nearest.pixels[p + channel])) > 2) {
                    ++smoothPixels;
                    break;
                }
        }
    std::cout << label << " smooth_linear_pixels=" << smoothPixels << '\n';
    Check(smoothPixels > 10,
          "dilated ABC output is indistinguishable from nearest sampling");

    if (state == 'A' && (scene == "00-00-A01" || scene == "00-00-A02")) {
        // Representative points straddle a real opaque/transparent edge.
        const int y = scene == "00-00-A01" ? 319 : 288;
        const int outsideX = scene == "00-00-A01" ? 867 : 625;
        const int edgeX = scene == "00-00-A01" ? 868 : 627;
        const int insideX = scene == "00-00-A01" ? 870 : 628;
        for (int x : {outsideX, edgeX, insideX}) {
            std::cout << label << " edge_pixel=" << x << ',' << y
                      << " cpu=" << RgbAt(reference, raster.surface->pitch, x, y)
                      << " linear=" << RgbAt(linear.pixels, raster.surface->pitch, x, y)
                      << " nearest=" << RgbAt(nearest.pixels, raster.surface->pitch, x, y)
                      << " dilated_linear=" << RgbAt(processedLinear.pixels,
                          raster.surface->pitch, x, y)
                      << '\n';
        }
        Check(BrightnessAt(linear.pixels, raster.surface->pitch, edgeX, y) + 10 <
                  BrightnessAt(reference, raster.surface->pitch, edgeX, y) &&
              std::abs(BrightnessAt(nearest.pixels, raster.surface->pitch, edgeX, y) -
                       BrightnessAt(reference, raster.surface->pitch, edgeX, y)) <= 5,
              "real edge probe did not isolate dark linear halo");
        Check(std::abs(BrightnessAt(processedLinear.pixels,
                                   raster.surface->pitch, edgeX, y) -
                       BrightnessAt(reference, raster.surface->pitch, edgeX, y)) <= 5,
              "dilated linear edge probe still has a black fringe");
    }

    Kotonoha_time* clock = Kotonoha_timeNew(false);
    Check(clock != nullptr, "ABC fixture clock");
    {
        Kotonoha::Image image(clock);
        const Uint64 generation = image.Register(basePath.string().c_str(), 0, 5000, 0);
        Check(image.ActivateBase(generation), "activate measured base");
        auto* group = image.FindAbcGroup(key, 1000);
        Check(group && image.BindAbcGroup(group, generation, 0, 5000),
              "bind measured ABC group");
        image.SelectAbcState(group, static_cast<Uint8>(state - 'A'));
        Kotonoha_timeSet(clock, 1000);
        raster.Clear();
        Check(Kotonoha::Image::Render(nullptr, raster.renderer, nullptr,
              &image, nullptr) == KOTONOHA_SCENE_DRAW,
              "render actual ABC path");
        const auto actual = raster.Capture();
        raster.Save(output / ("g2_4_1b_" + label + "_actual.bmp"));
        SDL_ScaleMode actualScale = SDL_SCALEMODE_INVALID;
        Check(group->textures[state - 'A'] &&
              SDL_GetTextureScaleMode(group->textures[state - 'A'], &actualScale),
              "query actual ABC texture scale");
        SDL_BlendMode actualBlend = SDL_BLENDMODE_NONE;
        Uint8 red = 0, green = 0, blue = 0, alpha = 0;
        Check(SDL_GetTextureBlendMode(group->textures[state - 'A'], &actualBlend) &&
              SDL_GetTextureColorMod(group->textures[state - 'A'], &red, &green, &blue) &&
              SDL_GetTextureAlphaMod(group->textures[state - 'A'], &alpha),
              "query actual ABC blend and modulation");
        Check(actualBlend == SDL_BLENDMODE_BLEND && red == 255 &&
              green == 255 && blue == 255 && alpha == 255,
              "ABC texture altered straight-alpha blend or modulation");
        const auto actualMetrics = CompareEdge(actual, reference,
            raster.surface->pitch, x0, y0, x1, y1);
        std::cout << label << " actual_scale=" << int(actualScale)
                  << " texture_format="
                  << SDL_GetPixelFormatName(group->textures[state - 'A']->format)
                  << " actual_blend=" << actualBlend
                  << " color_mod=" << int(red) << ',' << int(green) << ',' << int(blue)
                  << " alpha_mod=" << int(alpha)
                  << " actual_error=" << actualMetrics.meanRgbError
                  << " actual_dark=" << actualMetrics.darkPixels << '\n';
        Check(actualScale == SDL_SCALEMODE_LINEAR,
              "ABC renderer did not restore smooth linear filtering");
        Check(actualMetrics.darkPixels <= processedLinear.edge.darkPixels + 2 &&
              std::abs(actualMetrics.meanRgbError -
                       processedLinear.edge.meanRgbError) < 0.1,
              "actual renderer differs from dilated linear control");
    }
    Kotonoha_timeDestroy(clock);
    SDL_DestroyTexture(referenceTexture);
    SDL_DestroyTexture(processedTexture);
    SDL_DestroyTexture(overlayTexture);
    SDL_DestroyTexture(baseTexture);
    SDL_DestroySurface(overlaySurface);
    SDL_DestroySurface(baseSurface);
}
} // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 3, "usage: SchoolDaysAbcEdgeSeamTest ASSETS_ROOT OUTPUT_DIR");
        SDL_SetHint("SDL_VIDEODRIVER", "dummy");
        Check(SDL_Init(SDL_INIT_VIDEO), "SDL video init");
        fs::create_directories(argv[2]);
        TestOnePixelDilation();
        for (char state : {'A', 'B', 'C'}) {
            TestCase(argv[1], argv[2], "Event00/00-00/00-00-A01",
                     "00-00-A01-004", "MAK", state);
            TestCase(argv[1], argv[2], "Event00/00-00/00-00-A02",
                     "00-00-A02-001B", "MAK", state);
            TestCase(argv[1], argv[2], "Event04/04-SB/04-SB-E00",
                     "04-SB-E00-020", "X03", state);
        }
        SDL_Quit();
        std::cout << "SchoolDaysAbcEdgeSeamTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit();
        std::cerr << "SchoolDaysAbcEdgeSeamTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
