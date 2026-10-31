#include <Kotonoha/componentes/SplashScreen.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace Kotonoha {

SplashScreen::SplashScreen(Engine &engine,
                           std::vector<std::filesystem::path> pictures)
    : Scene(engine), pictures(std::move(pictures)) {
  restart();
}

void SplashScreen::restart() {
  current = 0;
  texture.reset();
  loaded = false;
  startedNs = SDL_GetTicksNS();
}

void SplashScreen::next() {
  ++current;
  texture.reset();
  loaded = false;
  startedNs = SDL_GetTicksNS();
}

double SplashScreen::elapsed() const {
  return static_cast<double>(SDL_GetTicksNS() - startedNs) / SDL_NS_PER_SECOND;
}

bool SplashScreen::handleEvent(EventRef event) {
  if (finished())
    return false;
  // Touches also arrive as synthetic mouse events; count them once.
  const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.which != SDL_TOUCH_MOUSEID;
  if (click || event.type == SDL_EVENT_FINGER_DOWN)
    next();
  return true;
}

void SplashScreen::update() {
  if (loaded && !finished() && elapsed() >= displaySeconds)
    next();
}

void SplashScreen::load(RendererRef renderer) {
  while (!finished() && !loaded) {
    std::ifstream file(pictures[current], std::ios::binary);
    std::vector<Uint8> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    SurfacePtr surface;
    if (!bytes.empty()) {
      try {
        surface = MediaDecoder::decodePicture(bytes);
      } catch (const std::exception &error) {
        SDL_Log("Splash %s: %s", pictures[current].string().c_str(),
                error.what());
      }
    }
    if (surface)
      texture.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
    if (!texture) {
      next();
      continue;
    }
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    loaded = true;
    startedNs = SDL_GetTicksNS();
  }
}

void SplashScreen::render(RendererRef renderer, float) {
  if (finished())
    return;
  load(renderer);
  int width = 0, height = 0;
  SDL_RendererLogicalPresentation mode;
  SDL_GetRenderLogicalPresentation(&renderer, &width, &height, &mode);
  if (width <= 0 || height <= 0)
    SDL_GetCurrentRenderOutputSize(&renderer, &width, &height);
  const SDL_FRect screen{0, 0, float(width), float(height)};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(&renderer, &screen);
  if (!texture)
    return;

  float textureWidth = 0, textureHeight = 0;
  SDL_GetTextureSize(texture.get(), &textureWidth, &textureHeight);
  const float fit = std::min(screen.w / textureWidth, screen.h / textureHeight);
  const SDL_FRect target{(screen.w - textureWidth * fit) / 2,
                         (screen.h - textureHeight * fit) / 2,
                         textureWidth * fit, textureHeight * fit};
  const double alpha = std::clamp(elapsed() / fadeSeconds, 0.0, 1.0);
  SDL_SetTextureAlphaModFloat(texture.get(), static_cast<float>(alpha));
  SDL_RenderTexture(&renderer, texture.get(), nullptr, &target);
}

} // namespace Kotonoha
