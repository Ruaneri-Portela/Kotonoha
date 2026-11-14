#include "PmailRuntime.hpp"
#include "Pmail/Port.hpp"
#include "PmailSession.hpp"
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace Kotonoha::PmailPort {

PmailRuntime::PmailRuntime(
    std::string gameDirectory,
    std::shared_ptr<Kotonoha::Providers::SnowAssets> assets)
    : Runtime("Pure Mail", "0.1.0", "me.hirameki.kotonoha.pmail"),
      gameDirectory_(std::move(gameDirectory)), assets_(std::move(assets)) {}

PmailRuntime::~PmailRuntime() = default;

void PmailRuntime::shutdown() {
  stopMovie();
  if (audio_)
    audio_->StopAll();
  ClearScenes();
  saves_.reset();
  session_.reset();
  if (assets_)
    assets_->clearCache();
}

bool PmailRuntime::loadScriptPack() {
  // All ports must read through the mounted Filesystem. Constructing a
  // physical InputFile("/game-assets/...") cannot see assets provided by
  // HTTP Fetch (it only sees the MEMFS local directory).
  const auto source = assets_ ? assets_->source() : nullptr;
  if (!source) {
    status_ = "Pure Mail virtual filesystem unavailable";
    std::cerr << "[PmailPort] " << status_ << std::endl;
    return false;
  }
  // The case-insensitive VFS resolves these logical IDs for local archives,
  // and the HTTP provider retries the lowercase URL on case-sensitive hosts.
  for (const char *relative : {"data/pack/script.dat", "pack/script.dat",
                               "script.dat", "data/pack/SCRIPT.DAT",
                               "SCRIPT.DAT"}) {
    try {
      if (!source->exists(relative) ||
          source->stat(relative).type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      auto data = Kotonoha::Filesystem::readAll(
          *source->open(relative), 256u * 1024u * 1024u);
      if (data.empty())
        continue;
      const std::string logical = relative;
      std::cout << "[PmailPort] Loaded script pack from VFS: " << logical
                << " (" << data.size() << " bytes)" << std::endl;
      game_ = std::make_shared<PmailGame>(std::move(data), logical);
      return true;
    } catch (const Kotonoha::Filesystem::FilesystemError &error) {
      if (error.code() == Kotonoha::Filesystem::ErrorCode::NotFound)
        continue;
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "[PmailPort] Cannot read virtual %s: %s",
                   relative, error.what());
      throw;
    }
  }
  status_ = "script.dat not found in virtual game-assets "
            "(expected data/pack/script.dat)";
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
    audio_ = std::make_unique<PmailAudio>(engine.GetAudio(), assets_);
    game_->sePlaying = [this] { return audio_->SePlaying(); };
    game_->voicePlaying = [this] { return audio_->VoicePlaying(); };
    game_->voiceMouthFrame = [this] { return audio_->VoiceMouthFrame(); };
    game_->animationFrameCount = [this](const std::string &name) {
      return assets_->AnimationOf(name).frames.size();
    };
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
  if (session_)
    session_->init();
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
  const std::string root =
      std::filesystem::is_directory(gameDirectory_ + "/data")
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
  if (!audio_ || !game_)
    return;
  std::vector<int> numbers;
  std::vector<std::string> strings;
  for (const auto &arg : args) {
    if (arg.tag() == 0)
      numbers.push_back(game_->vm().value(arg));
    else if (arg.tag() == 2)
      strings.push_back(game_->vm().text(arg));
  }
  audio_->Handle(id, numbers, strings);
}

void PmailRuntime::update() {
  if (audio_) {
    audio_->SetPaused((saves_ && saves_->isOpen()) ||
                      (session_ && session_->paused()));
    audio_->Update();
    const auto state = audio_->Snapshot();
    currentBgm_ = state[0].id;
    currentSe_ = state[1].id;
    currentVoice_ = state[2].id;
  }
  if (saves_ && saves_->isOpen()) {
    if (session_)
      session_->resetClock();
    return;
  }
  if (session_ && (movieFinished_ || !moviePlayer_))
    session_->update();
  if (game_ && movieFinished_)
    SetState(game_->atTitle() ? Kotonoha::GameState::Menu
                              : Kotonoha::GameState::Gameplay);
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

void PmailRuntime::renderOverlay(Kotonoha::RendererRef renderer, float scale) {
  // Session draws its own pause/backlog in order; saves sit above the session.
  if (saves_ && saves_->isOpen())
    renderScene(*saves_, renderer, scale);
}

bool PmailRuntime::onEvent(Kotonoha::EventRef event) {
  if (saves_ && saves_->isOpen()) {
    dispatchEvent(*saves_, event);
    return true;
  }
  if (moviePlayer_ && !movieFinished_) {
    if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
         (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN ||
          event.key.key == SDLK_SPACE)) ||
        (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
         event.button.button == SDL_BUTTON_LEFT))
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
    auto label = game_->currentScript() + " | " + buf;
    for (const auto &animation : game_->animations())
      if (animation.playing)
        label += " | " + std::string(AnimationPresets[animation.preset].name) +
                 " " + std::to_string(animation.frame + 1) + "/" +
                 std::to_string(animation.count);
    return label;
  }
  return status_;
}

void PmailRuntime::openSaves(bool load) {
  if (!saves_ || !session_)
    return;
  session_->closeMenus();
  const auto directory = Kotonoha::saveDirectory() / "slots";
  std::filesystem::create_directories(directory);
  saves_->open(directory.string(), load);
}

void PmailRuntime::saveGame(const std::string &path) {
  if (!game_ || game_->atTitle())
    throw std::runtime_error("No active Pure Mail story to save");
  Kotonoha::VNSave save;
  save.nodeId = std::max(0, translationRoute());
  std::ostringstream payload;
  payload << "PMAIL-RUNTIME-1\n" << std::quoted(game_->serialize()) << '\n';
  payload << std::setprecision(17);
  for (const auto &track : audio_->Snapshot())
    payload << std::quoted(track.id) << ' ' << track.loop << ' '
            << track.playing << ' ' << track.seconds << ' ' << track.gain << ' '
            << track.fadeTarget << ' ' << track.fadeSeconds << '\n';
  const auto entries = session_->backlogEntries();
  payload << entries.size() << '\n';
  for (const auto &entry : entries)
    payload << std::quoted(entry.speaker) << ' ' << std::quoted(entry.text)
            << ' ' << std::quoted(entry.voice) << '\n';
  save.gameData = payload.str();
  save.write(path);
}

void PmailRuntime::loadGame(const std::string &path) {
  const auto save = Kotonoha::VNSave::read(path);
  std::istringstream payload(save.gameData);
  std::string magic, host;
  std::getline(payload, magic);
  if (magic != "PMAIL-RUNTIME-1" || !(payload >> std::quoted(host)))
    throw std::runtime_error("Invalid Pure Mail runtime save");
  PmailAudio::State state;
  for (auto &track : state)
    if (!(payload >> std::quoted(track.id) >> track.loop >> track.playing >>
          track.seconds >> track.gain >> track.fadeTarget >> track.fadeSeconds))
      throw std::runtime_error("Truncated Pure Mail audio state");
  size_t entryCount = 0;
  if (!(payload >> entryCount) || entryCount > 100000)
    throw std::runtime_error("Invalid Pure Mail backlog size");
  std::vector<Kotonoha::DialogueEntry> entries(entryCount);
  for (auto &entry : entries)
    if (!(payload >> std::quoted(entry.speaker) >> std::quoted(entry.text) >>
          std::quoted(entry.voice)))
      throw std::runtime_error("Truncated Pure Mail backlog");
  payload >> std::ws;
  if (!payload.eof())
    throw std::runtime_error("Unexpected Pure Mail save data");
  const auto previousHost = game_->serialize();
  const auto previousAudio = audio_->Snapshot();
  try {
    game_->deserialize(host);
    audio_->Restore(state);
  } catch (...) {
    game_->deserialize(previousHost);
    audio_->Restore(previousAudio);
    throw;
  }
  stopMovie();
  session_->closeMenus(true);
  session_->restoreBacklog(entries);
  SetState(Kotonoha::GameState::Gameplay);
}

void PmailRuntime::returnToTitle() {
  if (audio_)
    audio_->StopAll();
  currentBgm_.clear();
  currentSe_.clear();
  currentVoice_.clear();
  stopMovie();
  session_->closeMenus(true);
  game_->returnToTitle();
  if (audio_)
    audio_->Handle(112, {9, 1}, {});
  SetState(Kotonoha::GameState::Menu);
}

std::vector<Kotonoha::DebugNode> PmailRuntime::debugNodes() const {
  if (!game_)
    return {};
  Kotonoha::DebugNode node;
  node.id = debugNode();
  node.label = game_->currentScript();
  const auto background = game_->background();
  if (!background.empty())
    node.assets.push_back("graphic/" + background + ".CRGB");
  for (const auto &sprite : game_->sprites())
    if (!sprite.name.empty())
      node.assets.push_back("graphic/" + sprite.name + ".CHAR");
  for (const auto &animation : game_->animations())
    node.assets.push_back("graphic/" +
                          std::string(AnimationPresets[animation.preset].name) +
                          ".anim#" + std::to_string(animation.frame));
  if (!currentBgm_.empty())
    node.assets.push_back(currentBgm_);
  if (!currentSe_.empty())
    node.assets.push_back(currentSe_);
  if (!currentVoice_.empty())
    node.assets.push_back(currentVoice_);
  return {std::move(node)};
}

std::string PmailRuntime::dialogueText() const {
  return game_ ? game_->message() : std::string();
}

std::string PmailRuntime::dialogueSpeaker() const {
  return game_ ? game_->speaker() : std::string();
}

int PmailRuntime::translationRoute() const {
  if (!game_)
    return -1;
  const auto current = game_->vm().current();
  return current ? int(current->id & 0xfff) : -1;
}

std::vector<std::string> PmailRuntime::activeTranslationKeys() const {
  std::vector<std::string> keys;
  if (!game_)
    return keys;
  const auto speaker = game_->speaker();
  const auto message = game_->message();
  if (!speaker.empty())
    keys.push_back(speaker);
  if (!message.empty())
    keys.push_back(message);
  for (const auto &choice : game_->choices())
    keys.push_back(choice);
  return keys;
}

std::vector<Kotonoha::DebugAudioAsset> PmailRuntime::debugSceneAudio() {
  std::vector<Kotonoha::DebugAudioAsset> out;
  if (!currentBgm_.empty())
    out.push_back({currentBgm_, {}, {}});
  if (!currentSe_.empty())
    out.push_back({currentSe_, {}, {}});
  if (!currentVoice_.empty())
    out.push_back({currentVoice_, game_ ? game_->speaker() : std::string(),
                   game_ ? game_->message() : std::string()});
  return out;
}

std::optional<Kotonoha::AudioClip>
PmailRuntime::debugOriginalAudio(const std::string &id) {
  if (!assets_ || !assets_->contains(id, Kotonoha::AssetSource::Original))
    return std::nullopt;
  return assets_->loadAudio(id, Kotonoha::AssetSource::Original);
}

} // namespace Kotonoha::PmailPort
