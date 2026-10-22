#include <Kotonoha/speciais/Anime4K.h>
#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void DrawFixture(SDL_Renderer *renderer, SDL_Texture *texture) {
  Check(SDL_SetRenderTarget(renderer, texture), "set input target");
  Check(SDL_SetRenderDrawColor(renderer, 12, 18, 30, 255), "set clear color");
  Check(SDL_RenderClear(renderer), "clear input texture");

  const SDL_FRect left{4.0f, 4.0f, 24.0f, 40.0f};
  const SDL_FRect right{34.0f, 8.0f, 26.0f, 18.0f};
  const SDL_FRect lower{28.0f, 30.0f, 30.0f, 14.0f};
  Check(SDL_SetRenderDrawColor(renderer, 240, 32, 48, 255), "set red");
  Check(SDL_RenderFillRect(renderer, &left), "draw red fixture");
  Check(SDL_SetRenderDrawColor(renderer, 32, 224, 64, 255), "set green");
  Check(SDL_RenderFillRect(renderer, &right), "draw green fixture");
  Check(SDL_SetRenderDrawColor(renderer, 40, 72, 244, 255), "set blue");
  Check(SDL_RenderFillRect(renderer, &lower), "draw blue fixture");
  Check(SDL_SetRenderTarget(renderer, nullptr), "restore default target");
}

SDL_Surface *Readback(SDL_Renderer *renderer, SDL_Texture *texture,
                      const char *label) {
  Check(SDL_SetRenderTarget(renderer, texture), "set readback target");
  SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
  Check(surface != nullptr, label);
  Check(SDL_SetRenderTarget(renderer, nullptr), "restore target after readback");
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  Check(rgba != nullptr, "convert readback to RGBA32");
  return rgba;
}

void ValidateOutput(SDL_Renderer *renderer, SDL_Texture *output,
                    SDL_Texture *linearReference) {
  SDL_Surface *rgba = Readback(renderer, output, "read Anime4K output");
  SDL_Surface *linear =
      Readback(renderer, linearReference, "read linear reference");
  Check(rgba->w == linear->w && rgba->h == linear->h,
        "Anime4K/reference size mismatch");
  Check(SDL_LockSurface(rgba), "lock Anime4K output");
  Check(SDL_LockSurface(linear), "lock linear reference");

  int minValue = 255;
  int maxValue = 0;
  std::uint64_t rgbSum = 0;
  std::uint64_t alphaSum = 0;
  std::uint64_t absoluteDifference = 0;
  std::uint64_t differentPixels = 0;
  const auto *pixels = static_cast<const Uint8 *>(rgba->pixels);
  const auto *linearPixels = static_cast<const Uint8 *>(linear->pixels);
  for (int y = 0; y < rgba->h; ++y) {
    const Uint8 *row = pixels + static_cast<std::size_t>(y) * rgba->pitch;
    const Uint8 *linearRow =
        linearPixels + static_cast<std::size_t>(y) * linear->pitch;
    for (int x = 0; x < rgba->w; ++x) {
      const Uint8 *p = row + x * 4;
      const Uint8 *q = linearRow + x * 4;
      bool pixelDiffers = false;
      for (int channel = 0; channel < 3; ++channel) {
        minValue = std::min(minValue, static_cast<int>(p[channel]));
        maxValue = std::max(maxValue, static_cast<int>(p[channel]));
        rgbSum += p[channel];
        const int difference =
            std::abs(static_cast<int>(p[channel]) - static_cast<int>(q[channel]));
        absoluteDifference += static_cast<std::uint64_t>(difference);
        pixelDiffers |= difference != 0;
      }
      alphaSum += p[3];
      if (pixelDiffers)
        ++differentPixels;
    }
  }

  SDL_UnlockSurface(rgba);
  SDL_UnlockSurface(linear);
  const int width = rgba->w;
  const int height = rgba->h;
  SDL_DestroySurface(rgba);
  SDL_DestroySurface(linear);

  Check(width == 128 && height == 96, "unexpected Anime4K output size");
  Check(maxValue - minValue >= 80, "Anime4K output lost image contrast");
  Check(rgbSum > 0, "Anime4K output is black/empty");
  Check(alphaSum > 0, "Anime4K output has no alpha coverage");
  Check(differentPixels > 32 && absoluteDifference > 1000,
        "Anime4K output matches plain linear fallback");

  std::cout << "Anime4KRuntimeTest PASS size=" << width << 'x' << height
            << " range=" << minValue << ".." << maxValue
            << " rgb_sum=" << rgbSum << " alpha_sum=" << alphaSum
            << " different_pixels=" << differentPixels
            << " abs_diff=" << absoluteDifference << '\n';
}

} // namespace

int main() {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *input = nullptr;
  SDL_Texture *output = nullptr;
  SDL_Texture *linearReference = nullptr;

  try {
    Check(SDL_setenv_unsafe("KOTONOHA_ANIME4K_DEBUG", "1", 1) == 0,
          "set Anime4K debug environment");
    Check(SDL_setenv_unsafe("KOTONOHA_ANIME4K_PRESET", "L", 1) == 0,
          "set Anime4K preset environment");
    KotonohaLogInit();
    Check(SDL_Init(SDL_INIT_VIDEO), "SDL video init");

    window = SDL_CreateWindow("Anime4KRuntimeTest", 256, 192,
                              SDL_WINDOW_HIDDEN);
    Check(window != nullptr, "create hidden OpenGL test window");
    renderer = SDL_CreateRenderer(window, "opengl");
    Check(renderer != nullptr, "create OpenGL SDL renderer");

    input = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_TARGET, 64, 48);
    output = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_TARGET, 128, 96);
    linearReference = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 128, 96);
    Check(input != nullptr && output != nullptr && linearReference != nullptr,
          "create Anime4K test textures");
    Check(SDL_SetTextureBlendMode(input, SDL_BLENDMODE_NONE), "input blend mode");
    Check(SDL_SetTextureBlendMode(output, SDL_BLENDMODE_NONE), "output blend mode");
    Check(SDL_SetTextureBlendMode(linearReference, SDL_BLENDMODE_NONE),
          "linear reference blend mode");
    Check(SDL_SetTextureScaleMode(input, SDL_SCALEMODE_LINEAR),
          "input linear scale mode");

    DrawFixture(renderer, input);
    Check(SDL_SetRenderTarget(renderer, linearReference),
          "set linear reference target");
    Check(SDL_RenderTexture(renderer, input, nullptr, nullptr),
          "render linear reference");
    Check(SDL_SetRenderTarget(renderer, nullptr),
          "restore target after linear reference");
    Check(KotonohaAnime4KInit(renderer), "initialize Anime4K pipeline");
    Check(KotonohaAnime4KUpscaleSDLTextureInto(renderer, input, output),
          "execute Anime4K pipeline");
    ValidateOutput(renderer, output, linearReference);

    KotonohaAnime4KShutdown();
    SDL_DestroyTexture(linearReference);
    SDL_DestroyTexture(output);
    SDL_DestroyTexture(input);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Anime4KRuntimeTest FAIL: " << error.what() << " (SDL: "
              << SDL_GetError() << ")\n";
    KotonohaAnime4KShutdown();
    if (linearReference != nullptr)
      SDL_DestroyTexture(linearReference);
    if (output != nullptr)
      SDL_DestroyTexture(output);
    if (input != nullptr)
      SDL_DestroyTexture(input);
    if (renderer != nullptr)
      SDL_DestroyRenderer(renderer);
    if (window != nullptr)
      SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
}
