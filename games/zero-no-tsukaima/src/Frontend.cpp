#include "Frontend.hpp"

#include <algorithm>

namespace ZeroNoTsukaima {
namespace {

constexpr float kW = 640, kH = 448;
constexpr uint64_t kCrossfadeMs = 500; // deftrans
constexpr uint64_t kTitleIdleMs = 45 * 1000; // logoSecond
constexpr float kDarkLevel = 0.8f;   // darkLevel: unfocused menu buttons

// titleVoices[set] = {logo, title, menu, warning}; LogoState picks a set at random.
constexpr const char *kTitleVoices[][4] = {
    {"RU9Y0005", "RU9Y0006", "RU9Y0007", "RU9Y0008"}, {"SI9Y0000", "SI9Y0001", "SI9Y0002", "SI9Y0003"},
    {"CU9Y0000", "CU9Y0001", "CU9Y0002", "CU9Y0003"}, {"TA9Y0000", "TA9Y0001", "TA9Y0002", "TA9Y0003"},
    {"AN9Y0000", "AN9Y0001", "AN9Y0002", "AN9Y0003"}, {"YR9Y0000", "YR9Y0001", "YR9Y0002", "YR9Y0003"},
    {"CH9Y0000", "CH9Y0001", "CH9Y0002", "CH9Y0003"}};

// GameMode menu: 247x47 buttons at x 48.
struct MenuButton {
  const char *name;
  float y;
};
constexpr MenuButton kMenu[] = {{"GAME_NEW", 91},  {"GAME_LOAD", 139}, {"GAME_OPTI", 187}, {"GAME_GALL", 235},
                                {"GAME_SCEE", 283}, {"GAME_SOUND", 329}, {"GAME_VOICE", 375}};
constexpr int kMenuCount = int(sizeof(kMenu) / sizeof(kMenu[0]));
constexpr float kMenuX = 48, kMenuW = 247, kMenuH = 47;

float Ramp(uint64_t elapsed, uint64_t total) { return total == 0 ? 1.0f : std::min(1.0f, float(elapsed) / float(total)); }

bool IsConfirm(SDL_Keycode k) { return k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_Z; }
bool IsCancel(SDL_Keycode k) { return k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_X; }

} // namespace

void Frontend::Enter(bool boot) {
  pending_ = Result::None;
  transition_ = false;
  host_.StopVoice();
  if (boot) {
    voiceChoice_ = int(SDL_rand(int(sizeof(kTitleVoices) / sizeof(kTitleVoices[0]))));
    host_.StopMusic();
    Go(Phase::Warning, false);
  } else {
    EnterTitle(false);
  }
}

void Frontend::Leave() {
  movie_.Close();
  host_.StopVoice();
}

void Frontend::Go(Phase phase, bool crossfade) {
  previous_ = phase_;
  phase_ = phase;
  transition_ = crossfade;
  transitionStart_ = stepStart_ = phaseStart_ = SDL_GetTicks();
  step_ = Step::FadeIn;
  if (phase == Phase::Movie) {
    std::vector<uint8_t> pss;
    if (!host_.Movie(0, &pss) || !movie_.Open(pss, host_.Renderer(), 1.0f)) {
      SDL_Log("[zero] opening movie unavailable");
      EnterTitle(false);
    }
  }
}

void Frontend::EnterTitle(bool fromMenu) {
  host_.Music("BGMA01");
  Go(Phase::Title, fromMenu);
  titleIdleStart_ = SDL_GetTicks();
  if (fromMenu) {
    titleStage_ = 2;
    host_.SystemVoice(kTitleVoices[voiceChoice_][1]);
  } else {
    titleStage_ = 0;
  }
}

void Frontend::EnterMenu(bool fromTitle) {
  Go(Phase::Menu, true);
  if (fromTitle) host_.SystemVoice(kTitleVoices[voiceChoice_][2]);
}

void Frontend::Skip() {
  switch (phase_) {
  case Phase::Warning:
    host_.StopVoice();
    Go(Phase::Logo, false);
    break;
  case Phase::Logo:
    host_.StopVoice();
    Go(Phase::Movie, false);
    break;
  case Phase::Movie:
    movie_.Close();
    EnterTitle(false);
    break;
  default: break;
  }
}

void Frontend::Activate(int item) {
  host_.SystemSe("SYS_SE02");
  if (item == 0) pending_ = Result::NewGame;
  else if (item == 1) pending_ = Result::Load;
  else SDL_Log("[zero] %s: this mode is not ported yet", kMenu[item].name);
}

void Frontend::Event(const SDL_Event &e) {
  const bool key = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat;
  const bool click = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT;
  const bool back = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) ||
                    (key && IsCancel(e.key.key));
  switch (phase_) {
  case Phase::Warning:
  case Phase::Logo:
  case Phase::Movie:
    if ((key && e.key.key < SDLK_F1) || (key && e.key.key > SDLK_F12) || click) Skip();
    break;
  case Phase::Title:
    if (!(click || (key && (IsConfirm(e.key.key) || e.key.key == SDLK_ESCAPE)))) break;
    if (titleStage_ < 2) {
      if (titleStage_ == 0) host_.SystemVoice(kTitleVoices[voiceChoice_][1]);
      titleStage_ = 2;
      titleIdleStart_ = SDL_GetTicks();
    } else if (!back) {
      host_.SystemSe("SYS_SE02");
      EnterMenu(true);
    }
    break;
  case Phase::Menu:
    if (back) {
      host_.SystemSe("SYS_SE03");
      EnterTitle(true);
    } else if (e.type == SDL_EVENT_MOUSE_MOTION || click) {
      const SDL_FPoint pt = click ? SDL_FPoint{e.button.x, e.button.y} : SDL_FPoint{e.motion.x, e.motion.y};
      for (int i = 0; i < kMenuCount; ++i) {
        const SDL_FRect rc{kMenuX, kMenu[i].y, kMenuW, kMenuH};
        if (!SDL_PointInRectFloat(&pt, &rc)) continue;
        if (focus_ != i) host_.SystemSe("SYS_SE01");
        focus_ = i;
        if (click) Activate(i);
      }
    } else if (key && (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN)) {
      focus_ = (focus_ + (e.key.key == SDLK_UP ? kMenuCount - 1 : 1)) % kMenuCount;
      host_.SystemSe("SYS_SE01");
    } else if (key && IsConfirm(e.key.key)) {
      Activate(focus_);
    }
    break;
  }
}

Frontend::Result Frontend::Tick() {
  const uint64_t now = SDL_GetTicks();
  if (transition_ && now - transitionStart_ >= kCrossfadeMs) transition_ = false;
  switch (phase_) {
  case Phase::Warning:
  case Phase::Logo: {
    const bool warning = phase_ == Phase::Warning;
    if (step_ == Step::FadeIn && Since() >= 2000) {
      holdMs_ = host_.SystemVoice(kTitleVoices[voiceChoice_][warning ? 3 : 0]) + 2000;
      step_ = Step::Hold;
      stepStart_ = now;
    } else if (step_ == Step::Hold && Since() >= uint64_t(holdMs_)) {
      step_ = Step::FadeOut;
      stepStart_ = now;
    } else if (step_ == Step::FadeOut && Since() >= (warning ? 2000u : 1000u)) {
      host_.StopVoice();
      Go(warning ? Phase::Logo : Phase::Movie, false);
    }
    break;
  }
  case Phase::Movie:
    movie_.Update();
    if (movie_.Finished()) {
      movie_.Close();
      EnterTitle(false);
    }
    break;
  case Phase::Title:
    if (titleStage_ == 0 && Since() >= 1000) {
      host_.SystemVoice(kTitleVoices[voiceChoice_][1]);
      titleStage_ = 1;
      stepStart_ = now;
    } else if (titleStage_ == 1 && Since() >= 1000) {
      titleStage_ = 2;
      titleIdleStart_ = now;
    } else if (titleStage_ == 2 && now - titleIdleStart_ >= kTitleIdleMs) {
      // logostart: back to the logo (no warning) and the opening movie, with a new voice set.
      host_.StopMusic();
      host_.StopVoice();
      voiceChoice_ = int(SDL_rand(int(sizeof(kTitleVoices) / sizeof(kTitleVoices[0]))));
      Go(Phase::Logo, true);
    }
    break;
  case Phase::Menu: break;
  }
  const Result r = pending_;
  pending_ = Result::None;
  return r;
}

void Frontend::DrawImage(SDL_Renderer *r, SDL_Texture *tex, float x, float y, float alpha, float light, float w,
                         float h) {
  if (!tex || alpha <= 0.0f) return;
  float tw = 0, th = 0;
  SDL_GetTextureSize(tex, &tw, &th);
  const SDL_FRect src{0, 0, w > 0 ? std::min(w, tw) : tw, h > 0 ? std::min(h, th) : th};
  const SDL_FRect dst{x, y, src.w, src.h};
  const auto level = uint8_t(std::clamp(light, 0.0f, 1.0f) * 255.0f);
  SDL_SetTextureAlphaModFloat(tex, std::clamp(alpha, 0.0f, 1.0f));
  SDL_SetTextureColorMod(tex, level, level, level);
  SDL_RenderTexture(r, tex, &src, &dst);
  SDL_SetTextureAlphaModFloat(tex, 1.0f);
  SDL_SetTextureColorMod(tex, 255, 255, 255);
}

void Frontend::DrawPhase(SDL_Renderer *r, Phase phase, float alpha) {
  const SDL_FRect all{0, 0, kW, kH};
  switch (phase) {
  case Phase::Warning:
  case Phase::Logo: {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, uint8_t(alpha * 255.0f));
    SDL_RenderFillRect(r, &all);
    float f = 1.0f;
    if (phase == phase_) {
      if (step_ == Step::FadeIn) f = Ramp(Since(), 2000);
      else if (step_ == Step::FadeOut) f = 1.0f - Ramp(Since(), phase == Phase::Warning ? 2000 : 1000);
    }
    DrawImage(r, host_.ScenePicture(phase == Phase::Warning ? "WARNING" : "MMV_LOGO"), 0, 0, alpha * f);
    break;
  }
  case Phase::Movie:
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, &all);
    movie_.Draw(r, all);
    break;
  case Phase::Title: {
    const int stage = phase == phase_ ? titleStage_ : 2;
    const float bg = stage == 0 ? Ramp(Since(), 1000) : 1.0f;
    DrawImage(r, host_.NormalPicture("TITLE_TITLEBG"), 0, 0, alpha * bg);
    if (stage >= 1) DrawImage(r, host_.NormalPicture("TITLE_TITLELOGO"), 16, 61, alpha * (stage == 1 ? Ramp(Since(), 1000) : 1.0f));
    if (stage >= 2) {
      DrawImage(r, host_.NormalPicture("TITLE_COPYRIGHT"), 80, 374, alpha);
      // Focused start button: TriangleAction on the opacity, 0 -> 255 in 1500 ms and back in 800 ms.
      const uint64_t t = (SDL_GetTicks() - titleIdleStart_) % 2300;
      const float blink = t < 1500 ? float(t) / 1500.0f : 1.0f - float(t - 1500) / 800.0f;
      DrawImage(r, host_.NormalPicture("TITLE_STARTBUTTON"), 166, 293, alpha * blink);
    }
    break;
  }
  case Phase::Menu:
    DrawImage(r, host_.NormalPicture("SYSTEM_BG02"), 0, 0, alpha);
    DrawImage(r, host_.NormalPicture("GAMEMODE_TITLE_GAMEMODE"), 0, 24, alpha, 1.0f, 640, 46);
    for (int i = 0; i < kMenuCount; ++i) {
      const bool on = i == focus_;
      DrawImage(r, host_.NormalPicture(std::string("GAMEMODE_") + kMenu[i].name + (on ? "_ON" : "_OFF")), kMenuX,
                kMenu[i].y, alpha, on ? 1.0f : kDarkLevel, kMenuW, kMenuH);
    }
    break;
  }
}

void Frontend::Draw(SDL_Renderer *r) {
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  const SDL_FRect all{0, 0, kW, kH};
  SDL_RenderFillRect(r, &all);
  if (transition_) {
    DrawPhase(r, previous_, 1.0f);
    DrawPhase(r, phase_, Ramp(SDL_GetTicks() - transitionStart_, kCrossfadeMs));
  } else {
    DrawPhase(r, phase_, 1.0f);
  }
}

} // namespace ZeroNoTsukaima
