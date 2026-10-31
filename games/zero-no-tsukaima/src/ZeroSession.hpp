#pragma once

#include "DiscAssetProvider.hpp"
#include "Frontend.hpp"
#include "Media.hpp"
#include <Kotonoha/media/MediaPlayer.hpp>

#include <ZeroNoTsukaima/Game.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Kotonoha {
class Engine;
}

namespace ZeroNoTsukaima {

// Logical size of the PS2 game; every Zero scene draws in these coordinates.
constexpr int kGameWidth = 640, kGameHeight = 448;
constexpr int kSaveSlots = 64; // SaveState: 16 pages x 4

// One play of Zero no Tsukaima on the Kotonoha engine: the script interpreter,
// the retail front end (FrontendHost), the audio on the engine channels
// "voice" / "bgm" / "se", pictures from the engine AssetProvider (the disc) and
// the replay saves. The engine components (ZeroComponents.hpp) drive it.
class ZeroSession : public FrontendHost {
public:
  ZeroSession(Kotonoha::Engine &engine, std::shared_ptr<DiscAssetProvider> disc);
  ~ZeroSession() override;
  ZeroSession(const ZeroSession &) = delete;
  ZeroSession &operator=(const ZeroSession &) = delete;

  // Releases the textures, audio and movie while the renderer still exists.
  void Shutdown();

  // ---- Front end (boot logos, title, mode menu, galleries) ----
  enum class TitleAction { None, NewGame, Load };
  void EnterFrontend();
  TitleAction FrontendTick();
  void FrontendEvent(const SDL_Event &e) { frontend_.Event(e); }
  void DrawFrontend(SDL_Renderer *r) { frontend_.Draw(r); }
  bool HasPendingLoad() const { return pendingLoad_ >= 0; }

  // ---- Story ----
  // New game, a scene / voice replay picked in the galleries, or the pending load.
  bool Start();
  void DebugJump(const std::string &script);
  bool Playing() const { return playing_ && game_; }
  bool Ended() const { return game_ && game_->Finished(); }
  void Frame(); // once per frame while playing: clock, audio, in-game screens
  // Click (-1) or choice index; recorded for the replay saves.
  void Advance(int choice);
  bool Skip();
  bool TakeTitleRequest() { return std::exchange(titleRequest_, false); }

  const Game *Story() const { return game_.get(); }
  // Translation of a text of the running script (route = its scene index).
  std::string Translate(const std::string &text) const;
  bool HasMessage() const { return Playing() && game_->MessageVisible() && MovieFinished(); }
  bool Choosing() const { return Playing() && game_->Choosing(); }
  bool WaitingForClick() const { return Playing() && game_->WaitingForClick() && !frontend_.InGame(); }
  bool MovieFinished() const { return !movie_ || movie_->finished(); }
  int TextDelayMs() const;
  int AutoDelayMs() const;
  bool VoiceActive() const { return voice_.Playing(); }
  bool CanSkip() const;
  bool Auto() const { return auto_; }
  void SetAuto(bool on);

  // ---- In-game screens (CommandState / HistoryState) ----
  void OpenCommand();
  void OpenHistory();
  bool ScreenActive() const { return Playing() && frontend_.InGame(); }
  bool HistoryActive() const { return Playing() && frontend_.InHistory(); }
  void ScreenEvent(const SDL_Event &e) { frontend_.Event(e); }
  void DrawScreen(SDL_Renderer *r) { frontend_.Draw(r); }

  // ---- Roulette (tndlslot) ----
  bool RouletteActive() const { return Playing() && roulette_.active; }
  void RouletteEvent(const SDL_Event &e);
  void DrawRoulette(SDL_Renderer *r);

  // ---- Saves: label line + replay inputs, compatible with Kotonoha SaveStore ----
  bool SaveSlot(int slot);
  bool LoadSlot(int slot);
  bool SaveTo(const std::string &path) const;
  bool LoadFrom(const std::string &path);
  static std::string SlotPath(int slot);

  // ---- Drawing (640x448 logical units) ----
  // Device pixels per logical unit, so text is rasterised sharp.
  void SetPixelScale(float k) { pixelScale_ = k > 0 ? k : 1.0f; }
  void DrawStage(SDL_Renderer *r);
  void DrawMessageWindow(SDL_Renderer *r);
  void DrawMessageText(SDL_Renderer *r, const std::string &text);
  void DrawMessageCursor(SDL_Renderer *r);
  void DrawNormal(SDL_Renderer *r, const std::string &name, float x, float y, float alpha = 1.0f);
  ZeroNoTsukaima::Picture *SceneIndexPicture(int index);
  ZeroNoTsukaima::Picture *NamedScene(const std::string &name);
  const DiscAssetProvider &Disc() const { return *disc_; }

  // F2-F5: voice / BGM sample rate presets (live).
  void StepRate(bool bgm, int dir);
  static int MovePosition(const std::string &text);

  static constexpr float kMsgX = 8, kMsgY = 276;

  // ---- FrontendHost ----
  ZeroNoTsukaima::Picture *Picture(const std::string &name) override;
  SDL_FPoint DrawText(SDL_Renderer *r, const std::string &text, float x, float y, float size, SDL_Color color,
                      bool measureOnly = false) override;
  SystemData &System() override { return system_; }
  void SystemChanged() override;
  bool HasScene(const std::string &name) override;
  ZeroNoTsukaima::Picture *NormalPicture(const std::string &name) override;
  ZeroNoTsukaima::Picture *ScenePicture(const std::string &name) override;
  int SystemVoice(const std::string &name) override;
  void StopVoice() override { voice_.Stop(); }
  void SystemSe(const std::string &name) override { PlaySe(name); }
  void Music(const std::string &name) override { PlayBgm(name); }
  void StopMusic() override;
  std::unique_ptr<Kotonoha::MediaPlayer> OpenMovie(int number) override;
  Kotonoha::Upscaling *Upscaler() override;
  SDL_Renderer *Renderer() override;
  bool Slot(int slot, SlotInfo *info) override;
  void PlayVoice(int voice) override;

private:
  bool debugJumped_ = false;
  using Block = Game::Block;

  struct RouletteAnim {
    bool active = false, slot03 = false, finished = false;
    int state = 0, left = 0, right = 0, pick = -1;
    double shiftl = 0, shiftr = 0;
    Uint64 start = 0, last = 0, doneAt = 0;
  };

  ZeroNoTsukaima::Picture *Texture(const std::string &id);
  bool ReadAsset(const std::string &id, std::vector<uint8_t> *out) const;
  bool PlayStream(PcmPlayer &out, const char *channel, const std::vector<uint8_t> &raw, int rate, bool loop);
  void PlayVoiceIndex(int index);
  void PlayBgm(const std::string &name);
  void PlaySe(const std::string &name);
  void ApplyVolumes();

  void Step(int input);
  void Replay(const std::vector<int> &inputs);
  void Idle();
  void Sync();
  void Tick();
  void Settle();
  void RunCues();
  bool VoiceAllowed() const;
  void Remember();
  int MaxLike() const;
  void LoadRequest(int slot);
  void MarkRead(const std::string &id);
  void RecordHistory();
  void TickRoulette();
  std::string SaveLabel() const;

  void DrawElements(SDL_Renderer *r, const std::vector<Game::Element> &scene);
  void DrawMeter(SDL_Renderer *r, const Game::Meter &m);
  SDL_Texture *Snapshot(SDL_Renderer *r, const Game::Transition &t);

  Kotonoha::Engine &engine_;
  std::shared_ptr<DiscAssetProvider> disc_;
  Frontend frontend_;
  std::unique_ptr<Game> game_;
  std::unique_ptr<Kotonoha::MediaPlayer> movie_;
  PcmPlayer voice_, bgm_, se_;
  std::string lastBgm_;
  SystemData system_;
  std::string systemPath_, readPath_;
  std::set<std::string> read_;

  std::map<std::string, ZeroNoTsukaima::Picture> pictures_;
  float picturesScale_ = 1.0f;
  TTF_Font *font_ = nullptr;
  std::map<int, TTF_Font *> sizedFonts_;
  std::map<std::string, SDL_Texture *> texts_;
  float pixelScale_ = 1.0f;
  SDL_Texture *snapshot_ = nullptr;
  int snapW_ = 0, snapH_ = 0;
  uint32_t snapSerial_ = 0;

  bool booted_ = false, playing_ = false, replaying_ = false, auto_ = false, titleRequest_ = false;
  std::vector<int> inputs_;
  int pendingLoad_ = -1;
  int64_t playMs_ = 0, pendingPlayMs_ = 0;
  Uint64 lastTicks_ = 0;
  std::string lastKey_, lastLogged_;
  std::deque<HistoryEntry> history_;
  std::string historyId_;
  RouletteAnim roulette_;
  Frontend::Replay replay_ = Frontend::Replay::None;
  int voiceRate_, bgmRate_, seRate_;
};

} // namespace ZeroNoTsukaima
