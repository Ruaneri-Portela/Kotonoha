#pragma once
#include "Media.hpp"

#include <Kotonoha/media/MediaPlayer.hpp>

#include "Media.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// The original system save (zone 1, `sf.*`): options from initOption(),
// per-character voice switches, clear / appear flags and the CG "seen" marks
// (getMainDataAccessF). Kept as a text file next to the save slots.
struct SystemData {
  int charSpeed = 6, autoWait = 3, skipMode = 0, vibration = 1;
  int voiceVol = 10, seVol = 10, bgmVol = 6, stereo = 1;
  std::map<std::string, int> voiceOn;    // VOICES.NUT code -> 0 / 1 (missing = on)
  std::map<std::string, int64_t> flags;  // "sf.clear_ru", "sf.ri_appear", ...
  std::set<std::string> seen;            // SCENEDAT pictures shown in play

  void ResetOptions(); // initOption()
  bool VoiceOn(const std::string &code) const;
  bool Flag(const std::string &name) const;
  bool AllClear() const; // isAllClear()
  bool Load(const std::string &path);
  bool Save(const std::string &path) const;
};

// VOICES.NUT: speaker name -> voice config code ("etc" when unknown).
std::string VoiceCode(const std::string &speaker);

// A back log entry (HistoryState).
struct HistoryEntry {
  std::string name, text;
  int voice = 0; // VOICE_ID index, 0 = none
};

// Save slot metadata kept in the slot label: f.title, play time and getMaxLike().
struct SlotInfo {
  std::string title;
  int64_t playMs = 0;
  int maxLike = -1;
  static SlotInfo Parse(const std::string &label);
  std::string Label() const;
};

// What the front end needs from the application (textures, audio, disc).
class FrontendHost {
public:
  virtual ~FrontendHost() = default;
  // Any picture by name: NORMAL first, then SCENEDAT; null (quietly) if missing.
  virtual ZeroNoTsukaima::Picture *Picture(const std::string &name) = 0;
  // Menu text (TextLayer) in logical coordinates; returns the logical size.
  virtual SDL_FPoint DrawText(SDL_Renderer *r, const std::string &text, float x, float y, float size, SDL_Color color,
                              bool measureOnly = false) = 0;
  virtual SystemData &System() = 0;
  virtual void SystemChanged() = 0; // apply volumes and write the system save
  virtual bool HasScene(const std::string &name) = 0;
  // NORMAL (system UI) and SCENEDAT pictures by archive name; null if missing.
  virtual ZeroNoTsukaima::Picture *NormalPicture(const std::string &name) = 0;
  virtual ZeroNoTsukaima::Picture *ScenePicture(const std::string &name) = 0;
  // Plays a SYSVOICES voice and returns its length in milliseconds (0 = none).
  virtual int SystemVoice(const std::string &name) = 0;
  virtual void StopVoice() = 0;
  virtual void SystemSe(const std::string &name) = 0;
  virtual void Music(const std::string &name) = 0; // looping SOUND_ID stream, e.g. "BGMA01"
  virtual void StopMusic() = 0;
  // Disc movie `number` playing on an engine MediaPlayer (self-timed, with the
  // PSS soundtrack); null when the movie is missing or cannot be decoded.
  virtual std::unique_ptr<Kotonoha::MediaPlayer> OpenMovie(int number) = 0;
  virtual Kotonoha::Upscaling *Upscaler() = 0; // engine pipeline for movie frames
  virtual SDL_Renderer *Renderer() = 0;
  // Save slot `slot` (0..63): false when empty.
  virtual bool Slot(int slot, SlotInfo *info) = 0;
  virtual void PlayVoice(int voice) = 0; // VOICE_ID index (back log replay)
};

// The retail boot, title and system menus of OVERRIDE.NUT, 640x448 logical:
//   WARNING (first boot) -> MMV_LOGO -> ZERO2OP.PSS -> title -> mode menu,
// and from the mode menu OptionState / VoiceOptionState, the CG and scene
// galleries (CgModeMenuState, CgViewState) and the sound / special voice
// lists (SoundModeMenuState). Each logo fades in, waits for its system voice
// plus 2 s and fades out; any key or click skips the current step. The title
// returns to the logo after 45 s without input. Every state change is the
// original 500 ms crossfade. Keys: arrows, Enter/Space/Z (circle),
// Esc/Backspace/X or right click (cross), PageUp/PageDown or Q/W (L1/R1).
//
// The in-game screens are here too (they share the menu parts): CommandState
// (normal and the coffee break), CharacterState, the 64-slot SaveState /
// LoadState and HistoryState. While InGame() they draw over the story.
class Frontend {
public:
  // PlayScene: start Scene() (scene replay or special voice drama).
  // Save / LoadSlot: write / load Slot(); Title: back to the title; Next: the
  // coffee break ended (NEXT); Resume: an in-game screen closed.
  enum class Result { None, NewGame, PlayScene, Save, LoadSlot, Title, Next, Resume };
  enum class Replay { None, Scene, Voice };

  explicit Frontend(FrontendHost &host) : host_(host) {}

  void Enter(bool boot);
  // After a story ends: a replay goes back to its gallery, the story to the logo (sceneend).
  void Return(Replay from);
  const std::string &Scene() const { return scene_; }
  Replay ReplayKind() const { return replay_; }
  void Leave(); // stops the movie and the system voice
  void Event(const SDL_Event &e);
  Result Tick();
  void Draw(SDL_Renderer *r);

  // In-game screens. `tundere` is f.prm_tundere_eval, `maxLike` getMaxLike(f).
  void OpenCommand(bool coffee, int tundere, int maxLike);
  void OpenHistory(std::vector<HistoryEntry> entries);
  bool InGame() const { return inGame_; }
  bool InHistory() const { return inGame_ && phase_ == Phase::History; }
  int Slot() const { return slot_; }

private:
  // Game: no screen, the story shows (in-game screens close to it).
  enum class Phase { Warning, Logo, Movie, Title, Menu, Option, VoiceOption, Gallery, CgView, Sound,
                     Command, Character, Files, History, Game };
  // Confirm dialogs (ConfirmDialog YESNO / OK).
  enum class Ask { None, Reset, Title, Next, Save, Overwrite, Load, Loaded };
  enum class Step { FadeIn, Hold, FadeOut };

  void Go(Phase phase, bool crossfade);
  void EnterTitle(bool fromMenu);
  void EnterMenu(bool fromTitle);
  void Skip();
  void Activate(int item);
  void EnterGallery(bool cg);
  void EnterSound(bool voice);
  void OptionKey(SDL_Keycode k);
  void OptionChange(int dir);
  void VoiceOptionKey(SDL_Keycode k);
  void GalleryKey(SDL_Keycode k);
  void GalleryPage(int dir);
  void OpenCg(int entry);
  void CgViewKey(SDL_Keycode k);
  void CgNext();
  void SoundKey(SDL_Keycode k);
  void SoundPage(int dir, bool wrapped);
  void Back(Phase to);
  void MenuEvent(const SDL_Event &e, SDL_Keycode key, bool click, SDL_FPoint pt);
  bool GalleryUnlocked(int n) const;
  int GalleryCount() const;
  int SoundCount() const;
  bool SoundUnlocked(int n) const;
  std::string SoundTitle(int n) const;
  void DrawPageNumber(SDL_Renderer *r, int page, int pages, float alpha);
  void DrawOption(SDL_Renderer *r, float alpha);
  void DrawVoiceOption(SDL_Renderer *r, float alpha);
  void DrawGallery(SDL_Renderer *r, float alpha);
  void DrawCgView(SDL_Renderer *r, float alpha);
  void DrawSound(SDL_Renderer *r, float alpha);
  void DrawConfirm(SDL_Renderer *r);
  void Answer(bool yes);
  void OpenFiles(bool save, Phase back);
  void CommandKey(SDL_Keycode k);
  void CommandPress();
  void CharacterKey(SDL_Keycode k);
  void FilesKey(SDL_Keycode k);
  void HistoryKey(SDL_Keycode k);
  void Close(Result result);
  const std::pair<bool, SlotInfo> &CachedSlot(int slot);
  int CommandCount() const { return coffee_ ? 6 : 5; }
  void DrawCommand(SDL_Renderer *r, float alpha);
  void DrawCharacter(SDL_Renderer *r, float alpha);
  void DrawFiles(SDL_Renderer *r, float alpha);
  void DrawHistory(SDL_Renderer *r, float alpha);
  Picture *Img(const std::string &name) { return host_.Picture(name); }
  void DrawPhase(SDL_Renderer *r, Phase phase, float alpha);
  void DrawImage(SDL_Renderer *r, Picture *img, float x, float y, float alpha, float light = 1.0f, float w = 0,
                 float h = 0);
  uint64_t Since() const { return SDL_GetTicks() - stepStart_; }

  FrontendHost &host_;
  std::unique_ptr<Kotonoha::MediaPlayer> movie_;
  Phase phase_ = Phase::Title, previous_ = Phase::Title;
  Step step_ = Step::FadeIn;
  uint64_t stepStart_ = 0, phaseStart_ = 0, transitionStart_ = 0;
  bool transition_ = false;
  int voiceChoice_ = 0;
  int holdMs_ = 0;
  // Title: 0 = background fading in, 1 = logo fading in, 2 = blinking start button.
  int titleStage_ = 2;
  uint64_t titleIdleStart_ = 0;
  int focus_ = 0; // menu cursor (kept like the original `curpos`)
  Result pending_ = Result::None;
  std::string scene_;
  Replay replay_ = Replay::None;
  // OptionState (persistent like the original instance fields).
  int optRow_ = 0, optDisp_ = 0;
  Ask ask_ = Ask::None;
  // VoiceOptionState cursor.
  int voiceX_ = 0, voiceY_ = 0;
  // CgModeMenuState: cg (true) or scene list.
  bool galleryCg_ = true;
  int galleryPage_ = 1, curX_ = 0, curY_ = 0;
  // CgViewState: unlocked images of the entry, the one showing and its scroll.
  std::vector<std::string> view_;
  size_t viewIndex_ = 0;
  float viewX_ = 0, viewY_ = 0;
  uint64_t viewFade_ = 0;
  // SoundModeMenuState: BGM (false) or special voice list.
  bool soundVoice_ = false;
  int soundPage_ = 1, soundCur_ = 0;
  bool soundPlaying_ = false;
  // In-game screens.
  bool inGame_ = false, coffee_ = false;
  int commandCur_ = 0, tundere_ = 127, maxLike_ = 0;
  int charaPage_ = 0, charaPrev_ = 0;
  uint64_t charaFade_ = 0;
  bool filesSave_ = false;
  Phase filesBack_ = Phase::Menu;
  int filesPage_ = 1, filesCur_ = 0, slot_ = 0;
  std::map<int, std::pair<bool, SlotInfo>> slotCache_;
  std::vector<HistoryEntry> history_;
  int historyCur_ = 0, historyTop_ = 0;
};

} // namespace ZeroNoTsukaima
