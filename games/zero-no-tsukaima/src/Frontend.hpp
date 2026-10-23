#pragma once
#include "MoviePlayer.hpp"

#include <Kotonoha/components/CanvasImage.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ZeroNoTsukaima {

// What the front end needs from the application (textures, audio, disc).
class FrontendHost {
public:
  virtual ~FrontendHost() = default;
  // NORMAL (system UI) and SCENEDAT pictures by archive name; null if missing.
  virtual Kotonoha::CanvasImage *NormalPicture(const std::string &name) = 0;
  virtual Kotonoha::CanvasImage *ScenePicture(const std::string &name) = 0;
  // Plays a SYSVOICES voice and returns its length in milliseconds (0 = none).
  virtual int SystemVoice(const std::string &name) = 0;
  virtual void StopVoice() = 0;
  virtual void SystemSe(const std::string &name) = 0;
  virtual void Music(const std::string &name) = 0; // looping SOUND_ID stream, e.g. "BGMA01"
  virtual void StopMusic() = 0;
  virtual bool Movie(int number, std::vector<uint8_t> *out) = 0;
  virtual SDL_Renderer *Renderer() = 0;
  virtual Kotonoha::Sound::Channel *MovieAudio() = 0; // engine channel the movie soundtrack plays on
};

// The retail boot and title flow of OVERRIDE.NUT (LogoState, TitleState and
// the GameMode menu), 640x448 logical:
//   WARNING (first boot) -> MMV_LOGO -> ZERO2OP.PSS -> title -> mode menu.
// Each logo fades in, waits for its system voice plus 2 s and fades out; any
// key or click skips the current step. The title returns to the logo after
// 45 s without input. Every state change is the original 500 ms crossfade.
class Frontend {
public:
  enum class Result { None, NewGame, Load };

  explicit Frontend(FrontendHost &host) : host_(host) {}

  void Enter(bool boot);
  void Leave(); // stops the movie and the system voice
  void Event(const SDL_Event &e);
  Result Tick();
  void Draw(SDL_Renderer *r);

private:
  enum class Phase { Warning, Logo, Movie, Title, Menu };
  enum class Step { FadeIn, Hold, FadeOut };

  void Go(Phase phase, bool crossfade);
  void EnterTitle(bool fromMenu);
  void EnterMenu(bool fromTitle);
  void Skip();
  void Activate(int item);
  void DrawPhase(SDL_Renderer *r, Phase phase, float alpha);
  void DrawImage(SDL_Renderer *r, Kotonoha::CanvasImage *img, float x, float y, float alpha, float light = 1.0f, float w = 0,
                 float h = 0);
  uint64_t Since() const { return SDL_GetTicks() - stepStart_; }

  FrontendHost &host_;
  MoviePlayer movie_;
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
};

} // namespace ZeroNoTsukaima
