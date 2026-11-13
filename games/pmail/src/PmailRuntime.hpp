#pragma once

#include <Pmail/Port.hpp>
#include "PmailSession.hpp"
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <SnowRadish/Pack.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailRuntime : public Kotonoha::Runtime {
public:
  PmailRuntime(std::string gameDirectory,
               std::shared_ptr<Kotonoha::Providers::SnowAssets> assets);
  ~PmailRuntime() override;
  void shutdown();
  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;
  void openSaves(bool load);
  void returnToTitle();

  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  SDL_Point viewport() const override { return {800, 600}; }
  bool continuousTouch() const override { return true; }
  void render(Kotonoha::RendererRef renderer, float scale) override;
  void update() override;
  bool onEvent(Kotonoha::EventRef event) override;

  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  std::string dialogueText() const override;
  std::string dialogueSpeaker() const override;
  int translationRoute() const override;
  std::vector<std::string> activeTranslationKeys() const override;
  std::vector<Kotonoha::DebugAudioAsset> debugSceneAudio() override;
  std::optional<Kotonoha::AudioClip> debugOriginalAudio(const std::string &id) override;

private:
  bool loadScriptPack();
  void handleNative(int id, const std::vector<::Pmail::Arg> &args);
  bool playTrack(Kotonoha::PcmPlayer &player, const char *channel,
                 SnowRadish::AdpsTrack track, bool loop);

  std::string gameDirectory_;
  Kotonoha::Engine *engine_ = nullptr;
  std::shared_ptr<PmailGame> game_;
  std::unique_ptr<PmailSession> session_;
  std::unique_ptr<Kotonoha::VNSaveMenu> saves_;
  std::shared_ptr<Kotonoha::Providers::SnowAssets> assets_;
  std::string status_;
  bool running_ = false;
  Kotonoha::PcmPlayer bgm_, se_, voice_;
  std::string currentBgm_, currentSe_, currentVoice_;
  uint64_t titleShownAt_ = 0;
  bool sawTitle_ = false;
  TTF_Font *font_ = nullptr;
  std::unique_ptr<Kotonoha::MediaPlayer> moviePlayer_;
  bool movieFinished_ = false;
  void startMovie();
  void stopMovie();
};

} // namespace Kotonoha::PmailPort
