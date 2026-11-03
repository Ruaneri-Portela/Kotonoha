#include "SchoolDaysAssets.hpp"
#include "SchoolDaysScenes.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Kotonoha;
class LogoRuntime : public Runtime {
public:
  LogoRuntime() : Runtime("Logo regression", "1", "logo-test") {}
};
int main(int argc, char **argv) {
  const auto overlay =
      std::filesystem::temp_directory_path() /
      ("school-logo-" + std::to_string(SDL_GetPerformanceCounter()));
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } cleanup{overlay};
  try {
    if (argc != 2)
      throw std::runtime_error("Expected fixture data directory");
    auto assets = std::make_shared<SchoolDays::Assets>(argv[1]);
    LogoRuntime runtime;
    EngineOptions options;
    options.assets = assets;
    options.rendererOverride = "software";
    Engine engine(runtime, options);
    SurfacePtr surface(SDL_CreateSurface(64, 48, SDL_PIXELFORMAT_RGBA32));
    SDL_FillSurfaceRect(surface.get(), nullptr,
                        SDL_MapSurfaceRGBA(surface.get(), 0, 0, 0, 255));
    SDL_Rect ink{24, 20, 16, 8};
    SDL_FillSurfaceRect(surface.get(), &ink,
                        SDL_MapSurfaceRGBA(surface.get(), 0, 128, 255, 255));
    std::filesystem::create_directories(overlay / "System/SYSTEM");
    if (!SDL_SavePNG(surface.get(),
                     (overlay / "System/SYSTEM/LOGO.png").string().c_str()))
      throw std::runtime_error(SDL_GetError());
    assets->setOverlay(overlay);
    SchoolDays::LogoSplash logo(engine, *assets);
    auto &renderer = engine.GetRenderer();
    TexturePtr target(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 320, 180));
    SDL_SetRenderTarget(&renderer, target.get());
    SDL_SetRenderLogicalPresentation(&renderer, 320, 180,
                                     SDL_LOGICAL_PRESENTATION_STRETCH);
    logo.render(renderer, 1);
    SDL_Delay(850);
    logo.render(renderer, 1);
    SurfacePtr frame(SDL_RenderReadPixels(&renderer, nullptr));
    SDL_SetRenderTarget(&renderer, nullptr);
    if (!frame)
      throw std::runtime_error(SDL_GetError());
    for (const int x : {5, 160, 314}) {
      Uint8 red = 0, green = 0, blue = 0, alpha = 0;
      if (!SDL_ReadSurfacePixel(frame.get(), x, 90, &red, &green, &blue,
                                &alpha))
        throw std::runtime_error(SDL_GetError());
      if (x == 160 ? blue < 200 || green < 100 : blue > 8 || green > 8)
        throw std::runtime_error(
            "School Days logo must retain its original padding");
    }
    std::cout
        << "Overflow logo stays centered at its original padded proportions\n";

  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
