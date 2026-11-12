#include "PmailRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <SDL3/SDL.h>

#include <fstream>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <utility>

namespace Kotonoha::PmailPort {

PmailRuntime::PmailRuntime(std::string gameDirectory)
    : Runtime("Pure Mail", "0.1.0", "me.hirameki.kotonoha.pmail"),
      gameDirectory_(std::move(gameDirectory)) {}

PmailRuntime::~PmailRuntime() {
  stopMovie();
  if (titleButton_)
    SDL_DestroyTexture(titleButton_);
  if (backgroundTexture_)
    SDL_DestroyTexture(backgroundTexture_);
  if (messageTexture_)
    SDL_DestroyTexture(messageTexture_);
  if (font_)
    TTF_CloseFont(font_);
}

bool PmailRuntime::loadScriptPack() {
  const std::vector<std::string> candidates = {
      gameDirectory_ + "/data/pack/script.dat",
      gameDirectory_ + "/pack/script.dat",
      gameDirectory_ + "/script.dat",
      gameDirectory_ + "/SCRIPT.DAT",
      gameDirectory_ + "/data/pack/SCRIPT.DAT",
  };
  for (const auto &path : candidates) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
      continue;
    const auto sz = in.tellg();
    if (sz <= 0)
      continue;
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char *>(data.data()), sz))
      continue;
    std::cout << "[PmailPort] Loaded script pack: " << path << " ("
              << data.size() << " bytes)" << std::endl;
    game_ = std::make_unique<PmailGame>(std::move(data), path);
    return true;
  }
  status_ = "script.dat not found under: " + gameDirectory_;
  std::cerr << "[PmailPort] " << status_ << std::endl;
  return false;
}

void PmailRuntime::initGlobal(Kotonoha::Engine &engine) {
  engine_ = &engine;
  if (game_)
    return;
  if (loadScriptPack()) {
    try {
      assets_ = std::make_unique<Kotonoha::Providers::SnowAssets>(gameDirectory_);
    } catch (const std::exception &error) {
      std::cerr << "[PmailPort] asset provider: " << error.what() << std::endl;
    }
    if (SDL_getenv("PMAIL_TRACE")) {
      game_->trace = [](const std::string &line) {
        static size_t count = 0;
        if (count++ < 1000)
          std::cerr << "[PmailTrace] " << line << std::endl;
      };
    }
    game_->sePlaying = [this] { return se_.Playing(); };
    game_->onNative = [this](int id, const std::vector<::Pmail::Arg> &args) {
      handleNative(id, args);
    };
    if (game_->init()) {
      running_ = true;
      status_ = "Running: " + game_->currentScript();
      startMovie();
    } else {
      status_ = "Failed to initialize PMAIL script VM";
    }
  }
}

namespace {
std::string shellQuote(const std::string &value) {
  std::string quoted = "'";
  for (char c : value) {
    if (c == '\'') quoted += "'\\''";
    else quoted += c;
  }
  return quoted + "'";
}
}

void PmailRuntime::startMovie() {
  if (!engine_ || movieFinished_) return;
  const std::string root = std::filesystem::is_directory(gameDirectory_ + "/data")
                               ? gameDirectory_ + "/data" : gameDirectory_;
  for (const char *name : {"PM01L_I5.avi", "PM01S_I5.avi", "PM01S_I3.avi"}) {
    const std::string path = root + "/movie/" + name;
    if (std::filesystem::is_regular_file(path)) { moviePath_ = path; break; }
  }
  if (moviePath_.empty()) { movieFinished_ = true; return; }
  const std::string quoted = shellQuote(moviePath_);
  const std::string audioCommand = "ffmpeg -v error -i " + quoted +
      " -map 0:a:0 -f s16le -acodec pcm_s16le -ac 2 -ar 44100 - 2>/dev/null";
  if (FILE *audio = popen(audioCommand.c_str(), "r")) {
    std::vector<int16_t> samples;
    int16_t chunk[8192];
    size_t n;
    while ((n = fread(chunk, sizeof(int16_t), 8192, audio)) > 0)
      samples.insert(samples.end(), chunk, chunk + n);
    pclose(audio);
    if (!samples.empty()) {
      Kotonoha::PcmPlayer::Clip clip;
      clip.samples = std::move(samples);
      clip.channels = 2;
      clip.rate = 44100;
      movieAudio_.Play(engine_->GetAudio().channel("bgm"), std::move(clip));
    }
  }
  const std::string videoCommand = "ffmpeg -v error -i " + quoted +
      " -map 0:v:0 -vf scale=800:600 -pix_fmt rgb24 -f rawvideo - 2>/dev/null";
  moviePipe_ = popen(videoCommand.c_str(), "r");
  movieStartedAt_ = SDL_GetTicks();
  movieFrame_ = -1;
  if (!moviePipe_) movieFinished_ = true;
}

void PmailRuntime::stopMovie() {
  movieAudio_.Stop();
  if (moviePipe_) { pclose(moviePipe_); moviePipe_ = nullptr; }
  if (movieTexture_) { SDL_DestroyTexture(movieTexture_); movieTexture_ = nullptr; }
  movieFinished_ = true;
}

void PmailRuntime::updateMovie() {
  if (!moviePipe_ || !engine_) return;
  const int target = int((SDL_GetTicks() - movieStartedAt_) * 15 / 1000);
  if (target <= movieFrame_) return;
  std::vector<uint8_t> pixels(800 * 600 * 3);
  while (movieFrame_ < target) {
    if (fread(pixels.data(), 1, pixels.size(), moviePipe_) != pixels.size()) {
      stopMovie();
      return;
    }
    ++movieFrame_;
  }
  if (!movieTexture_) {
    movieTexture_ = SDL_CreateTexture(&engine_->GetRenderer(),
        SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 800, 600);
  }
  if (movieTexture_)
    SDL_UpdateTexture(movieTexture_, nullptr, pixels.data(), 800 * 3);
}

bool PmailRuntime::playTrack(Kotonoha::PcmPlayer &player, const char *channel,
                             SnowRadish::AdpsTrack track, bool loop) {
  if (!engine_ || (track.intro.samples.empty() && track.loop.samples.empty()))
    return false;
  Kotonoha::PcmPlayer::Clip clip;
  clip.loopStart = track.intro.samples.size() / 2;
  clip.samples = std::move(track.intro.samples);
  clip.samples.insert(clip.samples.end(), track.loop.samples.begin(),
                      track.loop.samples.end());
  clip.channels = 2;
  clip.rate = 44100;
  clip.loop = loop && !track.loop.samples.empty();
  return player.Play(engine_->GetAudio().channel(channel), std::move(clip));
}

void PmailRuntime::handleNative(int id,
                                const std::vector<::Pmail::Arg> &args) {
  if (!assets_ || !game_ || !engine_)
    return;
  auto firstString = [&] {
    for (const auto &arg : args)
      if (arg.tag() == 2)
        return game_->vm().text(arg);
    return std::string();
  };
  auto firstInt = [&] {
    for (const auto &arg : args)
      if (arg.tag() == 0)
        return int(game_->vm().value(arg));
    return 0;
  };
  auto playPcm = [&](Kotonoha::PcmPlayer &player, const char *channel,
                     SnowRadish::Pcm pcm, bool loop) {
    if (pcm.samples.empty())
      return;
    Kotonoha::PcmPlayer::Clip clip;
    clip.channels = pcm.channels;
    clip.rate = 44100;
    clip.samples = std::move(pcm.samples);
    clip.loop = loop;
    player.Play(engine_->GetAudio().channel(channel), std::move(clip));
  };
  switch (id) {
  case 96:
  case 97: {
    const std::string name = firstString();
    if (name.empty())
      break;
    if (assets_->contains("voice/" + name))
      playPcm(se_, "se", assets_->Voice(name), firstInt() != 0);
    else if (assets_->contains("sound/" + name)) {
      // Pure Mail's sound pack mixes MM*.ADPS music and SE*/V*.ADP4
      // effects. ADP4 can pass the loose ADPS header check and hiss if
      // decoded through the wrong codec.
      playPcm(se_, "se",
              SnowRadish::decodeAdp4(assets_->read("sound/" + name)),
              firstInt() != 0);
    }
    break;
  }
  case 98:
  case 99:
  case 100:
    se_.Stop();
    break;
  case 112: {
    char name[16];
    std::snprintf(name, sizeof(name), "mm%03d", firstInt());
    SnowRadish::AdpsTrack track;
    if (assets_->Adps(name, track))
      playTrack(bgm_, "bgm", std::move(track), true);
    break;
  }
  case 115:
  case 116:
    bgm_.Stop();
    break;
  case 128:
    playPcm(voice_, "voice", assets_->Voice(firstString()), false);
    break;
  case 132:
    voice_.Stop();
    break;
  case 213: {
    SnowRadish::AdpsTrack track;
    if (assets_->Adps("mm001", track))
      playTrack(bgm_, "bgm", std::move(track), true);
    break;
  }
  default:
    break;
  }
}

void PmailRuntime::initSplash(Kotonoha::Engine &engine) {
  SetState(Kotonoha::GameState::Menu);
  initMenu(engine);
}

void PmailRuntime::initMenu(Kotonoha::Engine &) {
  // No separate menu yet; gameplay/runtime handles execution.
}

void PmailRuntime::initGameplay(Kotonoha::Engine &) {
  if (game_ && !running_) {
    if (game_->init())
      running_ = true;
  }
}

void PmailRuntime::render(Kotonoha::RendererRef renderer, float) {
  if (moviePipe_) {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    if (movieTexture_) {
      const SDL_FRect stage{0, 0, 800, 600};
      SDL_RenderTexture(&renderer, movieTexture_, nullptr, &stage);
    }
    return;
  }
  SDL_SetRenderDrawColor(&renderer, game_ && game_->atTitle() ? 255 : 0,
                         game_ && game_->atTitle() ? 255 : 0,
                         game_ && game_->atTitle() ? 255 : 0, 255);
  SDL_RenderClear(&renderer);
  if (!game_)
    return;

  const std::string picture = game_->atTitle() ? "TITL00a.evd"
                                                 : game_->background() + ".evd";
  if (assets_ && picture != renderedBackground_) {
    if (backgroundTexture_)
      SDL_DestroyTexture(backgroundTexture_);
    backgroundTexture_ = nullptr;
    renderedBackground_ = picture;
    if (!picture.empty() && assets_->HasGraphic(picture)) {
      try {
        auto surface = assets_->loadSurfaceAsset("graphic/" + picture);
        if (surface)
          backgroundTexture_ = SDL_CreateTextureFromSurface(&renderer, surface.get());
      } catch (const std::exception &error) {
        std::cerr << "[PmailPort] picture " << picture << ": " << error.what()
                  << std::endl;
      }
    }
  }
  if (backgroundTexture_) {
    const SDL_FRect stage{0, 0, 800, 600};
    SDL_RenderTexture(&renderer, backgroundTexture_, nullptr, &stage);
  }

  if (!font_) {
    const char *fonts[] = {
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    };
    for (const char *path : fonts)
      if ((font_ = TTF_OpenFont(path, 25)))
        break;
  }

  if (game_->atTitle()) {
    if (assets_ && !titleButtonTried_ &&
        assets_->HasGraphic("TITL01.IMG")) {
      titleButtonTried_ = true;
      try {
        auto surface = assets_->loadSurfaceAsset("graphic/TITL01.IMG");
        if (surface)
          titleButton_ = SDL_CreateTextureFromSurface(&renderer, surface.get());
      } catch (const std::exception &error) {
        std::cerr << "[PmailPort] title button: " << error.what() << std::endl;
      }
    }
    if (titleButton_) {
      float width = 0, height = 0;
      SDL_GetTextureSize(titleButton_, &width, &height);
      const SDL_FRect button{(800 - width) * 0.5f, 370, width, height};
      SDL_RenderTexture(&renderer, titleButton_, nullptr, &button);
    }
    if (!titleButton_) {
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
      SDL_RenderDebugText(&renderer, 365, 400, "START");
    }
    return;
  }

  const std::string &message = game_->message();
  if (font_ && !message.empty()) {
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(&renderer, 8, 16, 25, 204);
    const SDL_FRect box{40, 446, 720, 132};
    SDL_RenderFillRect(&renderer, &box);
    SDL_SetRenderDrawColor(&renderer, 225, 225, 235, 210);
    SDL_RenderRect(&renderer, &box);
    if (message != renderedMessage_) {
      if (messageTexture_)
        SDL_DestroyTexture(messageTexture_);
      messageTexture_ = nullptr;
      SDL_Surface *surface = TTF_RenderText_Blended_Wrapped(
          font_, message.c_str(), 0, {255, 255, 255, 255}, 680);
      if (surface) {
        messageTexture_ = SDL_CreateTextureFromSurface(&renderer, surface);
        SDL_DestroySurface(surface);
      }
      renderedMessage_ = message;
    }
    if (messageTexture_) {
      float width = 0, height = 0;
      SDL_GetTextureSize(messageTexture_, &width, &height);
      SDL_FRect destination{60, 462, width, height};
      SDL_RenderTexture(&renderer, messageTexture_, nullptr, &destination);
    }
  }
}

void PmailRuntime::update() {
  if (!game_ || !running_)
    return;
  if (moviePipe_) { updateMovie(); return; }
  game_->pump(50000);
  if (game_->atTitle() && !sawTitle_) {
    titleShownAt_ = SDL_GetTicks();
    sawTitle_ = true;
  } else if (!game_->atTitle()) {
    sawTitle_ = false;
  }
  if (!game_->message().empty()) {
    // Keep latest message in status for debugNode/overlay use.
    status_ = game_->currentScript();
  }
  if (game_->ended()) {
    status_ = "Script ended: " + game_->currentScript();
    running_ = false;
  }
}

bool PmailRuntime::onEvent(Kotonoha::EventRef event) {
  if (!game_)
    return false;
  if (moviePipe_) {
    if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
         (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_SPACE ||
          event.key.key == SDLK_RETURN)) ||
        (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
         event.button.button == SDL_BUTTON_LEFT)) {
      stopMovie();
      return true;
    }
    return false;
  }
  if (game_->atTitle() &&
      (!sawTitle_ || SDL_GetTicks() - titleShownAt_ < 250))
    return false;
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN ||
        event.key.key == SDLK_AC_BACK) {
      if (game_->atTitle() && event.key.key == SDLK_AC_BACK)
        return false;
      if (game_->waitingForClick())
        game_->click();
      return true;
    }
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      if (game_->atTitle()) {
        int width = 0, height = 0;
        SDL_GetWindowSize(&engine_->GetWindow(), &width, &height);
        if (event.button.x < width * 0.30f ||
            event.button.x > width * 0.70f ||
            event.button.y < height * 0.55f ||
            event.button.y > height * 0.85f)
          return false;
      }
      if (game_->waitingForClick())
        game_->click();
      return true;
    }
  }
  return false;
}

std::string PmailRuntime::debugNode() const {
  if (game_) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "pc=%u", (unsigned)game_->vm().pc());
    return game_->currentScript() + " | " + buf + " | " + status_;
  }
  return status_;
}

} // namespace Kotonoha::PmailPort
