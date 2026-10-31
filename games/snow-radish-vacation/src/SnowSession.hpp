#pragma once

#include "SnowAssets.hpp"
#include "SnowRadish/Game.hpp"

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Kotonoha {
class Engine;
}

namespace SnowRadish {

inline constexpr int kGameWidth = 800, kGameHeight = 600;

// Snow Radish Vacation!! state shared by the polymorphic engine components:
// the script VM, audio channels, scene snapshot, wipes, the message window,
// the three choice screens and the logo / title front end. Coordinates are
// 800x600 game units (SnowRuntime maps them into the engine novel space).
class SnowSession {
public:
  enum class TitleAction { NewGame, Load, Quit };
  enum class WindowClick { None, Consumed, ToggleAuto };
  using ChoiceKind = Game::ChoiceKind;

  SnowSession(Kotonoha::Engine &engine, std::shared_ptr<SnowAssets> assets);
  ~SnowSession();
  SnowSession(const SnowSession &) = delete;
  SnowSession &operator=(const SnowSession &) = delete;

  // Releases textures, fonts and audio streams (before the renderer goes).
  void Shutdown();

  // ---- story
  // Starts the script silently (no sound, no wipe) to check it boots.
  bool Validate();
  bool Start();
  void DebugJump(const std::string &name);
  bool Playing() const { return playing_; }
  void Stop() { playing_ = false; }
  bool Ended() const;
  // A click (-1) or a choice index; recorded for saves.
  void Advance(int input);
  void Replay(const std::vector<int> &inputs);
  const std::vector<int> &Inputs() const { return inputs_; }
  // Per frame while the story runs: pump the VM and refresh the scene snapshot.
  void Frame();
  bool Skip();
  bool CanSkip() const;
  bool WaitingForClick() const;
  bool HasMessage() const;
  std::string Speaker() const;
  std::string Text() const;
  bool Choosing() const;
  ChoiceKind Kind() const;
  std::vector<std::string> Choices() const;
  // Script texts by translation route (see SnowRadish::kMapRoute).
  std::map<int, std::vector<std::string>> TranslationSources() const;
  int InitialFocus() const;
  bool VoiceActive() const { return voice_.Playing(); }
  bool Auto() const { return auto_; }
  void SetAuto(bool enabled) { auto_ = enabled; }
  int AutoDelayMs() const { return 2000; }
  bool Cleared() const { return cleared_; }
  std::string SaveLabel() const;

  // Save data: the input replay of the current story.
  std::string Serialize() const;
  bool Restore(const std::string &data);
  std::filesystem::path SaveDirectory() const;
  bool HasSaves() const;

  // ---- stage
  void DrawStage(SDL_Renderer *r);
  bool Fading() const;

  // ---- message window
  void DrawMessageWindow(SDL_Renderer *r);
  void DrawMessageText(SDL_Renderer *r, const std::string &speaker, const std::string &text);
  void DrawMessageCursor(SDL_Renderer *r);
  WindowClick MessageWindowClick(float x, float y);

  // ---- choices
  SDL_FRect WindowItem(size_t index);
  void DrawWindowItem(SDL_Renderer *r, size_t index, bool focused);
  SDL_FRect Hotspot(size_t index);
  void DrawMap(SDL_Renderer *r);
  // Captures the map as the "from" picture of the next wipe.
  void PickMap();
  int UniformHit(float x, float y);
  int Focus() const { return focus_; }
  void SetFocus(int index) { focus_ = index; }

  // ---- front end (logo, white, title)
  void EnterFrontend();
  void FrontendEvent(Kotonoha::EventRef event);
  std::optional<TitleAction> FrontendTick();
  void DrawFrontend(SDL_Renderer *r);

  // ---- helpers
  SDL_FPoint DrawText(SDL_Renderer *r, const std::string &text, float x, float y, float size, SDL_Color color,
                      bool measureOnly = false);
  void SetPixelScale(float scale) { pixelScale_ = scale; }
  void SetMouse(float x, float y) { mouse_ = {x, y}; }
  SnowAssets &Assets() { return *assets_; }
  Game *Story() { return game_.get(); }
  bool TitleShown() const { return phase_ == Phase::Title; }

private:
  bool debugJumped_ = false;
  enum class Phase { Logo, White, TitleIn, TitleWait, Title };
  struct Shown {
    std::string name;
    int dx = 0, dy = 0, id = 0;
    int tint[3] = {0x100, 0x100, 0x100};
  };
  struct Flake {
    int type = 0;
    bool alive = false;
    int x = 0, y = 0;
    const std::vector<int> *path = nullptr;
  };
  struct TitleItem {
    SDL_FRect rect;
    TitleAction action;
  };

  // audio
  void PlaySound(const std::string &kind, const std::string &name, bool loop);
  bool StartChannel(Kotonoha::PcmPlayer &player, const char *channel, std::vector<int16_t> first,
                    const std::vector<int16_t> &loop, int channels, bool looping);
  void StopAll();
  void SetMuted(bool muted);
  void PersistCleared(bool flag);

  // drawing
  float UpscaleFactor() const;
  SDL_Texture *Texture(SDL_Renderer *r, const std::string &id, float &w, float &h);
  void DrawPicture(SDL_Renderer *r, const std::string &file, float x, float y, float alpha = 1);
  void DrawSprite(SDL_Renderer *r, const std::string &name, float dx = 0, float dy = 0, const int *tint = nullptr);
  void DrawFrame(SDL_Renderer *r, const std::string &animation, size_t frame, float x, float y,
                 const int *tint = nullptr);
  void DrawTexture(SDL_Renderer *r, SDL_Texture *t, float x, float y, float w, float h, const int *tint);
  void SyncScene();
  void DrawScene(SDL_Renderer *r);
  void DrawAnimation(SDL_Renderer *r, const std::string &name, float dx, float dy, const int *tint);
  void DrawUniformSelect(SDL_Renderer *r);

  // wipes
  void BeginTransition(int type, int pattern, int speed);
  bool Capture(const std::function<void(SDL_Renderer *)> &draw);
  float WipeProgress() const;
  void DrawWipe(SDL_Renderer *r);

  // front end
  static void FillWhite(SDL_Renderer *r);
  void BeginPhase(Phase phase, int ms);
  void StartLogo();
  void EndLogo();
  void LoadPaths();
  void Spawn(Flake &flake);
  void LogoFrame();
  void DrawLogo(SDL_Renderer *r);
  std::vector<TitleItem> Items() const;
  void DrawTitleScreen(SDL_Renderer *r, int hover);
  void TitleEvent(Kotonoha::EventRef event);
  void SelectTitle(TitleAction action);

  Kotonoha::Engine &engine_;
  std::shared_ptr<SnowAssets> assets_;
  std::vector<uint8_t> script_;
  std::unique_ptr<Game> game_;
  std::filesystem::path systemPath_;

  // audio
  Kotonoha::PcmPlayer voice_, bgm_, se_;
  std::string lastBgm_, resumeBgm_;
  bool lastBgmLoop_ = true, muted_ = false;
  std::vector<int16_t> voiceSamples_;
  uint64_t voiceStart_ = 0;
  int voiceActor_ = 0;

  // story
  bool playing_ = false, booting_ = false, ended_ = false, cleared_ = false, auto_ = false, booted_ = false;
  std::vector<int> inputs_;

  // scene snapshot
  bool filled_ = false;
  uint32_t fillColor_ = 0, seenVersion_ = ~0u;
  std::string background_;
  std::vector<Shown> sprites_;
  std::set<std::string> missing_;

  // wipes
  SDL_Texture *from_ = nullptr, *wipeTexture_ = nullptr, *to_ = nullptr;
  uint64_t toClock_ = ~uint64_t(0), fadeStart_ = 0, fadeMs_ = 0;
  std::vector<uint8_t> fromPixels_;
  AlphaMask wipeMask_, flashMask_;
  int fadeType_ = 1, fadePattern_ = 0, maskEdge_ = 128, fadeRange_ = 256, fadeStep_ = 4;
  bool keepFrom_ = false;

  // choices
  int focus_ = -1, mapHover_ = -1;
  struct AlphaHit {
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<uint8_t> alpha;
    bool hit(float px, float py) const;
  };
  std::vector<AlphaHit> uniformHit_;

  // front end
  Phase phase_ = Phase::Logo;
  uint64_t phaseStart_ = 0, phaseMs_ = 0, logoClock_ = 0;
  bool boot_ = false, voiced_ = false, titleSaves_ = false;
  int hover_ = -1;
  std::optional<TitleAction> action_;
  std::vector<int> paths_[3];
  Flake flakes_[2][40];
  int count_[2] = {1, 1};
  int frame_ = 0, sec_ = 0, fade_ = 0, logoState_ = 0;

  // text
  TTF_Font *font_ = nullptr;
  std::map<int, TTF_Font *> sizedFonts_;
  std::map<std::string, SDL_Texture *> texts_;
  float pixelScale_ = 1;
  SDL_FPoint mouse_{-1, -1};
  std::string wrappedKey_;
  std::vector<std::string> wrapped_;
};

} // namespace SnowRadish
