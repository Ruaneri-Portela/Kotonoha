#include <Kotonoha/specials/Upscaling.h>

#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void TestTextureCreationAndDynamicSettings() {
  SDL_Surface *surface =
      SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
  Check(surface != nullptr, "could not create software renderer surface");
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  Check(renderer != nullptr, "could not create software renderer");

  SDL_Texture *source =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                        SDL_TEXTUREACCESS_STREAMING, 2, 2);
  Check(source != nullptr, "could not create source image texture");
  const Uint8 pixels[] = {
      255, 0,   0,   255, 0,   255, 0,   255,
      0,   0,   255, 255, 255, 255, 255, 255,
  };
  Check(SDL_UpdateTexture(source, nullptr, pixels, 8),
        "could not upload source texture");

  Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_NONE);
  KotonohaUpscalingSetEnabled(true);
  KotonohaUpscalingSetFactor(2.0f);

  Kotonoha_UpscaledTexture cached{};
  bool textureChanged = false;
  Check(KotonohaUpscalingUpdateTexture(renderer, source, 1, &cached,
                                       &textureChanged),
        "SDL fallback did not produce an upscaled texture");
  Check(textureChanged, "new source texture was not processed");
  Check(cached.output != nullptr, "upscaled output texture is missing");

  float outputWidth = 0.0f;
  float outputHeight = 0.0f;
  Check(SDL_GetTextureSize(cached.output, &outputWidth, &outputHeight),
        "could not query output texture size");
  Check(outputWidth == 4.0f && outputHeight == 4.0f,
        "factor 2 did not create a 4x4 texture from a 2x2 source");

  textureChanged = true;
  Check(KotonohaUpscalingUpdateTexture(renderer, source, 1, &cached,
                                       &textureChanged),
        "cached output was not reused");
  Check(!textureChanged, "unchanged texture was upscaled again");

  const Uint64 beforePreset = KotonohaUpscalingGetRevision();
  KotonohaUpscalingSetPreset(KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16);
  Check(KotonohaUpscalingGetRevision() != beforePreset,
        "preset change did not advance upscaling revision");
  Check(KotonohaUpscalingUpdateTexture(renderer, source, 1, &cached,
                                       &textureChanged),
        "preset change did not rebuild cached texture");
  Check(textureChanged,
        "static texture cache retained output from the previous preset");

  const Uint64 samePreset = KotonohaUpscalingGetRevision();
  KotonohaUpscalingSetPreset(KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16);
  Check(KotonohaUpscalingGetRevision() == samePreset,
        "reselecting the same preset invalidated caches unnecessarily");

  KotonohaUpscalingSetFactor(3.0f);
  Check(KotonohaUpscalingUpdateTexture(renderer, source, 1, &cached,
                                       &textureChanged),
        "changing factor did not regenerate the output texture");
  Check(textureChanged, "factor change did not invalidate the output cache");
  Check(SDL_GetTextureSize(cached.output, &outputWidth, &outputHeight),
        "could not query regenerated texture size");
  Check(outputWidth == 6.0f && outputHeight == 6.0f,
        "factor 3 did not regenerate a 6x6 texture");

  KotonohaUpscalingSetEnabled(false);
  textureChanged = false;
  Check(!KotonohaUpscalingUpdateTexture(renderer, source, 1, &cached,
                                        &textureChanged),
        "disabled upscaling still returned a scaled texture");
  Check(textureChanged && cached.output == nullptr,
        "disabling upscaling did not release its cached output");
  Check(KotonohaUpscalingGetFactor() == 3.0f,
        "backend state reset the configured scale factor");

  KotonohaUpscalingReleaseTexture(&cached);
  SDL_DestroyTexture(source);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
}

} // namespace

int main() {
  try {
    TestTextureCreationAndDynamicSettings();
    std::cout << "UpscalingTextureLifecycleTest passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "UpscalingTextureLifecycleTest: " << error.what() << '\n';
    return 1;
  }
}
