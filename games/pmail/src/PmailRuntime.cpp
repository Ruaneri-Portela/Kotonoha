#include "PmailRuntime.hpp"
#include "Pmail/Port.hpp"
#include "PmailMovieSelection.hpp"
#include "PmailSession.hpp"
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/TextInputDialog.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
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
  for (const char *relative :
       {"data/pack/script.dat", "pack/script.dat", "script.dat",
        "data/pack/SCRIPT.DAT", "SCRIPT.DAT"}) {
    try {
      if (!source->exists(relative) ||
          source->stat(relative).type !=
              Kotonoha::Filesystem::FileType::Regular)
        continue;
      auto data = Kotonoha::Filesystem::readAll(*source->open(relative),
                                                256u * 1024u * 1024u);
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
                   "[PmailPort] Cannot read virtual %s: %s", relative,
                   error.what());
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
    game_->playMovie = [this](const std::string &stem) {
      return startMovie(stem);
    };
    game_->openSaveMenu = [this](bool load) {
      openSaves(load);
      return saves_ && saves_->isOpen();
    };
    game_->musicTracks = [this] {
      std::vector<std::string> ids;
      if (!assets_)
        return ids;
      for (const auto &id : assets_->listAsset("sound/mm")) {
        const auto slash = id.rfind('/');
        if (slash == std::string::npos || id.compare(slash + 1, 2, "mm") != 0)
          continue;
        bool digits = id.size() > slash + 3;
        for (std::size_t i = slash + 3; i < id.size(); ++i)
          if (id[i] < '0' || id[i] > '9') {
            digits = false;
            break;
          }
        if (digits)
          ids.push_back(id);
      }
      std::sort(ids.begin(), ids.end());
      return ids;
    };
    game_->playMusic = [this](const std::string &id) {
      if (!audio_)
        return;
      const auto pos = id.find("mm");
      const int number =
          pos == std::string::npos ? 0 : std::atoi(id.c_str() + pos + 2);
      if (number > 0)
        audio_->Handle(112, {number, 0}, {});
    };
    game_->stopMusic = [this] {
      if (audio_)
        audio_->Handle(115, {}, {});
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

bool PmailRuntime::startMovie(const std::string &stem) {
  if (!engine_ || stem.empty())
    return false;
  if (SDL_getenv("PMAIL_SKIP_INTRO"))
    return false;
  const auto vfs = assets_ ? assets_->source() : nullptr;
  if (!vfs) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[PmailPort] Movie source VFS is unavailable");
    return false;
  }
  // The original binary enumerates four AVI variants. Preserve original AVI
  // quality on native hosts, but prefer playable WebM/MP4 sidecars on browsers.
  const bool browser = Kotonoha::isBrowserPlatform();
  // Port-only debugging aid to compare a particular original video.
  const char *preferred = SDL_getenv("PMAIL_MOVIE_VARIANT");
  const std::string moviePath = selectMovie(
      stem, [&](const std::string &path) { return vfs->exists(path); }, browser,
      preferred ? preferred : "");
  if (moviePath.empty()) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Movie not found in VFS (data/movie or movie, "
                "original %s*.avi)",
                upperAscii(stem).c_str());
    return false;
  }
  moviePlayer_.reset();
  try {
    moviePlayer_ = std::make_unique<Kotonoha::MediaPlayer>(engine_->GetAudio());
    moviePlayer_->open(vfs->open(moviePath), moviePath, false, "bgm");
    if (!moviePlayer_->isOpen()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "[PmailPort] Movie decoder could not open: %s",
                  moviePath.c_str());
      moviePlayer_.reset();
      return false;
    }
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Movie failed: %s (%s)", moviePath.c_str(),
                error.what());
    moviePlayer_.reset();
    return false;
  }
  movieFinished_ = false;
  videoSkip_.reset();
  return true;
}

void PmailRuntime::stopMovie() {
  if (moviePlayer_) {
    moviePlayer_->stop();
    moviePlayer_.reset();
  }
  movieFinished_ = true;
  // SYS 53 blocks the VM until the retail movie finishes.
  if (game_)
    game_->resumeFromMovie();
}

void PmailRuntime::handleNative(int id, const std::vector<::Pmail::Arg> &args) {
  if (!audio_ || !game_ || !PmailAudio::HandlesSyscall(id))
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
    // The save/rename editor lives in the active scene list, but Pure Mail
    // owns the update pump. Start SDL text input and IME for that modal.
    for (auto *scene : GetActiveScenes())
      if (auto *dialog = dynamic_cast<Kotonoha::TextInputDialog *>(scene))
        dialog->update();
    if (session_)
      session_->resetClock();
    return;
  }
  // SYS 225/226 keep the script blocked while the save dialog is open.
  if (game_ && game_->saveMenuWaitActive())
    game_->resumeFromSaveMenu();
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
  // The Save menu creates a separate TextInputDialog for new save names and
  // renames. It must be rendered above the menu, or the save modal is
  // invisible despite accepting the "New save" action.
  if (saves_ && saves_->isOpen()) {
    renderScene(*saves_, renderer, scale);
    for (auto *scene : GetActiveScenes())
      if (scene && dynamic_cast<Kotonoha::TextInputDialog *>(scene))
        renderScene(*scene, renderer, scale);
  }
}

bool PmailRuntime::onEvent(Kotonoha::EventRef rawEvent) {
  // Keep the same physical gamepad mapping as the other ports. Engine owns
  // gamepad hotplug, A/B swap and analog-stick -> D-pad normalization;
  // Pure Mail consumes keyboard equivalents for title, choices and dialogue.
  const SDL_Event mapped = Kotonoha::MapControllerEvent(rawEvent);
  Kotonoha::EventRef event = mapped;
  if (saves_ && saves_->isOpen()) {
    // The editable save-name modal is a separate Scene. It was previously
    // bypassed by the save menu event path, preventing save creation/rename.
    const auto &scenes = GetActiveScenes();
    for (auto it = scenes.rbegin(); it != scenes.rend(); ++it) {
      if (*it && dynamic_cast<Kotonoha::TextInputDialog *>(*it)) {
        dispatchEvent(**it, event);
        return true;
      }
    }
    dispatchEvent(*saves_, event);
    return true;
  }
  if (moviePlayer_ && !movieFinished_) {
    if (videoSkip_.accept(rawEvent))
      stopMovie();
    return true;
  }
  // Portable accessibility shortcut for saving during long routes.
  // F5/F9 are Kotonoha convenience controls, not asserted to be the
  // original game's hotkeys. Preserve the regular menu-based save slots.
  if (game_ && !game_->atTitle() && event.type == SDL_EVENT_KEY_DOWN &&
      !event.key.repeat &&
      (event.key.key == SDLK_F5 || event.key.key == SDLK_F9)) {
    try {
      const auto quickPath = Kotonoha::saveDirectory() / "pmail-quick.vn";
      if (event.key.key == SDLK_F5) {
        std::filesystem::create_directories(quickPath.parent_path());
        saveGame(quickPath.string());
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[PmailPort] Quicksave written: %s",
                    quickPath.string().c_str());
      } else if (std::filesystem::is_regular_file(quickPath)) {
        loadGame(quickPath.string());
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[PmailPort] Quicksave restored: %s",
                    quickPath.string().c_str());
      } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[PmailPort] No quicksave found");
      }
    } catch (const std::exception &error) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "[PmailPort] Quick save/load failed: %s", error.what());
    }
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
  if (!load && (!game_ || game_->atTitle()))
    return;
  session_->closeMenus();
  const auto directory = Kotonoha::saveDirectory() / "slots";
  std::filesystem::create_directories(directory);
  saves_->open(directory.string(), load);
  // Closing the shared save menu removes it from the runtime scene list.
  // Re-register when it is opened again so modal input and pause work.
  const auto &scenes = GetActiveScenes();
  if (std::find(scenes.begin(), scenes.end(), saves_.get()) == scenes.end())
    AddScene(saves_.get(), GetState());
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

std::string PmailRuntime::debugNodeCatalogKey() const {
  return game_ ? game_->currentScript() : std::string{};
}

void PmailRuntime::debugJumpToNode(const std::string &id) {
  constexpr std::string_view prefix = "pmail | script ";
  if (!game_ || id.compare(0, prefix.size(), prefix) != 0 ||
      !game_->debugJumpScript(id.substr(prefix.size())))
    throw std::invalid_argument("Unknown Pmail script debug node: " + id);
  SetState(Kotonoha::GameState::Gameplay);
}

Kotonoha::Translation::SourceTexts PmailRuntime::translationSources() const {
  return game_ ? game_->translationInventory()
               : Kotonoha::Translation::SourceTexts{};
}

void PmailRuntime::debugOverlayAssetChanged(const std::string &) {
  if (assets_)
    assets_->clearCache();
}

std::vector<Kotonoha::DebugNode> PmailRuntime::debugNodes() const {
  if (!game_)
    return {};
  std::vector<Kotonoha::DebugNode> all;
  for (const auto &name : game_->debugScripts()) {
    Kotonoha::DebugNode entry{"pmail | script " + name, name, {}};
    if (assets_ && assets_->contains(game_->scriptPackId()))
      entry.assets.push_back(game_->scriptPackId());
    all.push_back(std::move(entry));
  }
  Kotonoha::DebugNode node;
  node.id = "pmail | script " + game_->currentScript();
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
  auto found = std::find_if(all.begin(), all.end(), [&](const auto &entry) {
    return entry.id == node.id;
  });
  if (found != all.end())
    found->assets.insert(found->assets.end(), node.assets.begin(),
                         node.assets.end());
  else
    all.push_back(std::move(node));
  return all;
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
