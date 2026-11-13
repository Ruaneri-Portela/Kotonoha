#include "PmailRuntime.hpp"
#include "Pmail/Port.hpp"
#include "PmailSession.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <SDL3/SDL.h>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>

namespace Kotonoha::PmailPort {

PmailRuntime::PmailRuntime(std::string gameDirectory,
                           std::shared_ptr<Kotonoha::Providers::SnowAssets> assets)
    : Runtime("Pure Mail", "0.1.0", "me.hirameki.kotonoha.pmail"),
      gameDirectory_(std::move(gameDirectory)), assets_(std::move(assets)) {}

PmailRuntime::~PmailRuntime() = default;

void PmailRuntime::shutdown() {
  stopMovie();
  bgm_.Stop();
  se_.Stop();
  voice_.Stop();
  ClearScenes();
  saves_.reset();
  session_.reset();
  if (assets_) assets_->clearCache();
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
    game_ = std::make_shared<PmailGame>(std::move(data), path);
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
    if (SDL_getenv("PMAIL_TRACE")) {
      game_->trace = [](const std::string &line) {
        static size_t count = 0;
        if (count++ < 1000)
          std::cerr << "[PmailTrace] " << line << std::endl;
      };
    }
    game_->sePlaying = [this] { return se_.Playing(); };
    game_->voicePlaying = [this] { return voice_.Playing(); };
    game_->onNative = [this](int id, const std::vector<::Pmail::Arg> &args) {
      handleNative(id, args);
    };
    session_ = std::make_unique<PmailSession>(game_, *engine_, assets_);
    if (game_->init()) {
      running_ = true;
      status_ = "Running: " + game_->currentScript();
      session_->init();
      saves_ = std::make_unique<Kotonoha::VNSaveMenu>(*engine_);
      AddScene(saves_.get(), Kotonoha::GameState::Menu);
      AddScene(saves_.get(), Kotonoha::GameState::Gameplay);
      session_->setMenuActions([this] { openSaves(false); },
                               [this] { openSaves(true); },
                               [this] { returnToTitle(); });
      startMovie();
    } else {
      status_ = "Failed to initialize PMAIL script VM";
    }
  }
}

void PmailRuntime::initSplash(Kotonoha::Engine &engine) {
  if (session_) session_->init();
}

void PmailRuntime::initMenu(Kotonoha::Engine &engine) {}

void PmailRuntime::initGameplay(Kotonoha::Engine &engine) {}

void PmailRuntime::startMovie() {
  if (!engine_ || movieFinished_)
    return;
  if (SDL_getenv("PMAIL_SKIP_INTRO")) {
    movieFinished_ = true;
    return;
  }
  const std::string root = std::filesystem::is_directory(gameDirectory_ + "/data")
                                ? gameDirectory_ + "/data"
                                : gameDirectory_;
  std::string moviePath;
  for (const char *name : {"PM01L_I5.avi", "PM01S_I5.avi", "PM01S_I3.avi"}) {
    const std::string path = root + "/movie/" + name;
    if (std::filesystem::is_regular_file(path)) {
      moviePath = path;
      break;
    }
  }
  if (moviePath.empty()) {
    movieFinished_ = true;
    return;
  }
  moviePlayer_ = std::make_unique<Kotonoha::MediaPlayer>(engine_->GetAudio());
  moviePlayer_->open(moviePath, false, "bgm");
  if (!moviePlayer_->isOpen()) {
    moviePlayer_.reset();
    movieFinished_ = true;
  }
}

void PmailRuntime::stopMovie() {
  if (moviePlayer_) {
    moviePlayer_->stop();
    moviePlayer_.reset();
  }
  movieFinished_ = true;
}

void PmailRuntime::handleNative(int id, const std::vector<::Pmail::Arg> &args) {
  if (!assets_ || !game_ || !engine_) return;
  auto name = [&] {
    for (const auto &arg : args)
      if (arg.tag() == 2) return Kotonoha::Providers::SnowAssets::Lower(game_->vm().text(arg));
    return std::string();
  };
  auto number = [&] {
    for (const auto &arg : args)
      if (arg.tag() == 0) return int(game_->vm().value(arg));
    return 0;
  };
  auto play = [&](Kotonoha::PcmPlayer &player, const char *channel,
                  const std::string &asset, bool loop) {
    player.Stop();
    const auto audio = assets_->loadAudio(asset);
    if (audio.data.empty()) return;
    Uint8 *converted = nullptr;
    int length = 0;
    SDL_AudioSpec target{SDL_AUDIO_S16, audio.spec.channels, audio.spec.freq};
    if (!SDL_ConvertAudioSamples(&audio.spec, audio.data.data(), int(audio.data.size()),
                                &target, &converted, &length))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::PcmPlayer::Clip clip;
    clip.channels = target.channels;
    clip.rate = target.freq;
    clip.loop = loop;
    clip.samples.resize(size_t(length) / sizeof(int16_t));
    std::memcpy(clip.samples.data(), converted, size_t(length));
    SDL_free(converted);
    player.Play(engine_->GetAudio().channel(channel), std::move(clip));
  };
  switch (id) {
  case 96:
  case 97: {
    const auto stem = name();
    if (stem.empty()) break;
    // SYS 96/97 belong to the SE bus even when the source lives in VOICE.DAT.
    currentSe_ = assets_->contains("voice/" + stem) ? "voice/" + stem : "sound/" + stem;
    play(se_, "se", currentSe_, number() != 0);
    break;
  }
  case 98:
  case 99:
  case 100:
    se_.Stop(); currentSe_.clear(); break;
  case 112: {
    char file[16];
    std::snprintf(file, sizeof(file), "mm%03d", number());
    currentBgm_ = "sound/" + std::string(file);
    if (assets_->hasOverlay(currentBgm_)) {
      play(bgm_, "bgm", currentBgm_, true);
    } else {
      SnowRadish::AdpsTrack track;
      bgm_.Stop();
      if (assets_->Adps(file, track)) playTrack(bgm_, "bgm", std::move(track), true);
    }
    break;
  }
  case 115:
  case 116:
    bgm_.Stop(); currentBgm_.clear(); break;
  case 128:
    currentVoice_ = "voice/" + name();
    play(voice_, "voice", currentVoice_, false);
    break;
  case 132:
    voice_.Stop(); currentVoice_.clear(); break;
  default: break;
  }
}

void PmailRuntime::update() {
  if (saves_ && saves_->isOpen()) return;
  if (session_ && (movieFinished_ || !moviePlayer_))
    session_->update();
  if (game_ && movieFinished_)
    SetState(game_->atTitle() ? Kotonoha::GameState::Menu : Kotonoha::GameState::Gameplay);
}

void PmailRuntime::render(Kotonoha::RendererRef renderer, float scale) {
  if (moviePlayer_ && !movieFinished_) {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    moviePlayer_->update(renderer);
    if (moviePlayer_->isOpen()) {
      const SDL_FRect stage{0, 0, 800, 600};
      moviePlayer_->render(renderer, stage, nullptr, true);
    }
    if (moviePlayer_->finished())
      stopMovie();
    if (!movieFinished_)
      return;
  }

  if (session_) {
    session_->render(renderer);
  }
}

bool PmailRuntime::onEvent(Kotonoha::EventRef event) {
  if (saves_ && saves_->isOpen()) {
    dispatchEvent(*saves_, event);
    return true;
  }
  if (moviePlayer_ && !movieFinished_) {
    if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
         (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)) ||
        (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT))
      stopMovie();
    return true;
  }
  if (session_) {
    session_->onEvent(event);
    return true;
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

void PmailRuntime::openSaves(bool load) {
  if (!saves_ || !session_) return;
  session_->closeMenus();
  const auto directory = Kotonoha::saveDirectory() / "slots";
  std::filesystem::create_directories(directory);
  saves_->open(directory.string(), load);
}

void PmailRuntime::saveGame(const std::string &path) {
  if (!game_ || game_->atTitle()) throw std::runtime_error("No active Pure Mail story to save");
  Kotonoha::VNSave save;
  save.nodeId = std::max(0, translationRoute());
  save.gameData = game_->serialize();
  save.write(path);
}

void PmailRuntime::loadGame(const std::string &path) {
  const auto save = Kotonoha::VNSave::read(path);
  game_->deserialize(save.gameData);
  bgm_.Stop(); se_.Stop(); voice_.Stop();
  currentBgm_.clear(); currentSe_.clear(); currentVoice_.clear();
  stopMovie();
  session_->closeMenus(true);
  SetState(Kotonoha::GameState::Gameplay);
}

void PmailRuntime::returnToTitle() {
  bgm_.Stop(); se_.Stop(); voice_.Stop();
  currentBgm_.clear(); currentSe_.clear(); currentVoice_.clear();
  stopMovie();
  session_->closeMenus(true);
  game_->returnToTitle();
  SetState(Kotonoha::GameState::Menu);
}

std::vector<Kotonoha::DebugNode> PmailRuntime::debugNodes() const {
  if (!game_) return {};
  Kotonoha::DebugNode node;
  node.id = debugNode();
  node.label = game_->currentScript();
  const auto background = game_->background();
  if (!background.empty()) node.assets.push_back("graphic/" + background + ".CRGB");
  for (const auto &sprite : game_->sprites())
    if (!sprite.name.empty()) node.assets.push_back("graphic/" + sprite.name + ".CHAR");
  if (!currentBgm_.empty()) node.assets.push_back(currentBgm_);
  if (!currentSe_.empty()) node.assets.push_back(currentSe_);
  if (!currentVoice_.empty()) node.assets.push_back(currentVoice_);
  return {std::move(node)};
}

std::string PmailRuntime::dialogueText() const {
  return game_ ? game_->message() : std::string();
}

std::string PmailRuntime::dialogueSpeaker() const {
  return game_ ? game_->speaker() : std::string();
}

int PmailRuntime::translationRoute() const {
  if (!game_) return -1;
  const auto current = game_->vm().current();
  return current ? int(current->id & 0xfff) : -1;
}

std::vector<std::string> PmailRuntime::activeTranslationKeys() const {
  std::vector<std::string> keys;
  if (!game_) return keys;
  const auto speaker = game_->speaker();
  const auto message = game_->message();
  if (!speaker.empty()) keys.push_back(speaker);
  if (!message.empty()) keys.push_back(message);
  for (const auto &choice : game_->choices())
    keys.push_back(choice);
  return keys;
}

std::vector<Kotonoha::DebugAudioAsset> PmailRuntime::debugSceneAudio() {
  std::vector<Kotonoha::DebugAudioAsset> out;
  if (!currentBgm_.empty()) out.push_back({currentBgm_, {}, {}});
  if (!currentSe_.empty()) out.push_back({currentSe_, {}, {}});
  if (!currentVoice_.empty())
    out.push_back({currentVoice_, game_ ? game_->speaker() : std::string(),
                   game_ ? game_->message() : std::string()});
  return out;
}

std::optional<Kotonoha::AudioClip> PmailRuntime::debugOriginalAudio(const std::string &id) {
  if (!assets_ || !assets_->contains(id, Kotonoha::AssetSource::Original))
    return std::nullopt;
  return assets_->loadAudio(id, Kotonoha::AssetSource::Original);
}

bool PmailRuntime::playTrack(Kotonoha::PcmPlayer &player, const char *channel,
                             SnowRadish::AdpsTrack track, bool loop) {
  if (!engine_ || (track.intro.samples.empty() && track.loop.samples.empty()))
    return false;
  Kotonoha::PcmPlayer::Clip clip;
  clip.channels = 2;
  clip.rate = assets_->sampleRate();
  clip.loopStart = track.intro.samples.size() / 2;
  clip.samples = std::move(track.intro.samples);
  clip.samples.insert(clip.samples.end(), track.loop.samples.begin(),
                      track.loop.samples.end());
  clip.loop = loop && !track.loop.samples.empty();
  return player.Play(engine_->GetAudio().channel(channel), std::move(clip));
}

} // namespace Kotonoha::PmailPort
