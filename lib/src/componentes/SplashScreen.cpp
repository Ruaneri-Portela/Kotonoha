#include <Kotonoha/componentes/SplashScreen.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Kotonoha {

SplashScreen::~SplashScreen() = default;
namespace {

bool isPictureExtension(const std::filesystem::path &path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return extension == ".apng" || extension == ".avif" || extension == ".bmp" ||
         extension == ".gif" || extension == ".jpeg" || extension == ".jpg" ||
         extension == ".png" || extension == ".tga" || extension == ".tif" ||
         extension == ".tiff" || extension == ".webp";
}

} // namespace

SplashScreen::SplashScreen(Engine &engine,
                           std::vector<std::filesystem::path> media)
    : Scene(engine), media(std::move(media)) {
  restart();
}

void SplashScreen::restart() {
  current = 0;
  texture.reset();
  video.reset();
  loaded = false;
  startedNs = SDL_GetTicksNS();
}

void SplashScreen::next() {
  ++current;
  texture.reset();
  video.reset();
  loaded = false;
  startedNs = SDL_GetTicksNS();
}

bool SplashScreen::currentIsVideo() const {
  return !finished() && !isPictureExtension(media[current]);
}

double SplashScreen::elapsed() const {
  return static_cast<double>(SDL_GetTicksNS() - startedNs) / SDL_NS_PER_SECOND;
}

bool SplashScreen::handleEvent(EventRef rawEvent) {
  const auto event = MapControllerEvent(rawEvent);
  if (finished())
    return false;
  // Touches also arrive as synthetic mouse events; count them once.
  const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.which != SDL_TOUCH_MOUSEID;
  const bool confirm =
      event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
      (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE);
  if (click || event.type == SDL_EVENT_FINGER_DOWN || confirm)
    next();
  return true;
}

void SplashScreen::update() {
  if (!loaded || finished())
    return;
  if (video) {
    if (video->finished())
      next();
  } else if (elapsed() >= displaySeconds) {
    next();
  }
}

void SplashScreen::load(RendererRef renderer) {
  while (!finished() && !loaded) {
    if (currentIsVideo()) {
      try {
        video = std::make_unique<MediaPlayer>(engine.GetAudio());
        video->open(media[current].string(), false, "splash");
        loaded = true;
        startedNs = SDL_GetTicksNS();
        return;
      } catch (const std::exception &error) {
        SDL_Log("Splash %s: %s", media[current].string().c_str(), error.what());
        video.reset();
        next();
        continue;
      }
    }

    std::ifstream file(media[current], std::ios::binary);
    std::vector<Uint8> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    SurfacePtr surface;
    if (!bytes.empty()) {
      try {
        surface = MediaDecoder::decodePicture(bytes);
      } catch (const std::exception &error) {
        SDL_Log("Splash %s: %s", media[current].string().c_str(), error.what());
      }
    }
    if (surface)
      texture.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
    if (!texture) {
      next();
      continue;
    }
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
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

  if (video) {
    try {
      video->update(renderer);
      video->render(renderer, screen);
    } catch (const std::exception &error) {
      SDL_Log("Splash %s: %s", media[current].string().c_str(), error.what());
      next();
    }
    return;
  }

  if (!texture)
    return;
  // Engine/brand splash images are UI presentation, not game artwork.
  // Keep their original linear sampling and authored fades even when an
  // external MPV/FSRCNNX shader is enabled for gameplay textures.  Never
  // prime the expensive shader pipeline while the initial logos are fading.
  auto *display = texture.get();
  float textureWidth = 0, textureHeight = 0;
  SDL_GetTextureSize(texture.get(), &textureWidth, &textureHeight);
  const float fit = std::min(screen.w / textureWidth, screen.h / textureHeight);
  const SDL_FRect target{(screen.w - textureWidth * fit) / 2,
                         (screen.h - textureHeight * fit) / 2,
                         textureWidth * fit, textureHeight * fit};
  const double time = elapsed();
  const double alpha = std::clamp(
      std::min(time, displaySeconds - time) / fadeSeconds, 0.0, 1.0);
  if (!SDL_SetTextureAlphaModFloat(display, static_cast<float>(alpha)) ||
      !SDL_RenderTexture(&renderer, display, nullptr, &target))
    throw std::runtime_error(SDL_GetError());
  if (!SDL_SetTextureAlphaModFloat(display, 1.0f))
    throw std::runtime_error(SDL_GetError());
}

} // namespace Kotonoha
