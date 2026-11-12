#pragma once

#include "Pmail/Port.hpp"
#include <SnowRadish/SnowAssets.hpp>

#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SDL3_ttf/SDL_ttf.h>

#include <memory>
#include <cstdio>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailRuntime : public Kotonoha::Runtime {
public:
  explicit PmailRuntime(std::string gameDirectory);
  ~PmailRuntime() override;

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

private:
  bool loadScriptPack();
  void handleNative(int id, const std::vector<::Pmail::Arg> &args);
  bool playTrack(Kotonoha::PcmPlayer &player, const char *channel,
                 SnowRadish::AdpsTrack track, bool loop);

  std::string gameDirectory_;
  Kotonoha::Engine *engine_ = nullptr;
  std::unique_ptr<PmailGame> game_;
  std::unique_ptr<Kotonoha::Providers::SnowAssets> assets_;
  std::string status_;
  bool running_ = false;
  Kotonoha::PcmPlayer bgm_, se_, voice_;
  uint64_t titleShownAt_ = 0;
  bool sawTitle_ = false;
  TTF_Font *font_ = nullptr;
  SDL_Texture *messageTexture_ = nullptr;
  SDL_Texture *backgroundTexture_ = nullptr;
  SDL_Texture *titleButton_ = nullptr;
  bool titleButtonTried_ = false;
  std::string renderedBackground_;
  std::string renderedMessage_;
  std::string moviePath_;
  FILE *moviePipe_ = nullptr;
  SDL_Texture *movieTexture_ = nullptr;
  Kotonoha::PcmPlayer movieAudio_;
  uint64_t movieStartedAt_ = 0;
  int movieFrame_ = -1;
  bool movieFinished_ = false;
  void startMovie();
  void stopMovie();
  void updateMovie();
};

} // namespace Kotonoha::PmailPort
