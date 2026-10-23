#include "Frontend.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace ZeroNoTsukaima {
namespace {

#include "MenuData.inc"

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
bool IsPrevPage(SDL_Keycode k) { return k == SDLK_PAGEUP || k == SDLK_Q; }
bool IsNextPage(SDL_Keycode k) { return k == SDLK_PAGEDOWN || k == SDLK_W; }

std::string Up(std::string s) {
  for (char &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

SDL_FPoint Size(Kotonoha::CanvasImage *img) {
  float w = 0, h = 0;
  if (img && img->Source()) SDL_GetTextureSize(img->Source(), &w, &h);
  return {w, h};
}

bool Inside(SDL_FPoint pt, float x, float y, float w, float h) {
  const SDL_FRect rc{x, y, w, h};
  return SDL_PointInRectFloat(&pt, &rc);
}

// OptionState rows: label button (OPTI_<label>_OFF / _ON) and its y positions.
struct OptRow {
  const char *label;
  float offY, onY;
};
constexpr OptRow kOptRows[] = {{"SOUND", 96, 96}, {"BGM", 142, 142},  {"VOICE", 188, 188}, {"SE", 234, 233},
                               {"MESS", 280, 279}, {"AUTO", 326, 325}, {"SKIP", 372, 371},  {"ARK", 418, 417},
                               {"KOE", 464, 463},  {"RES", 510, 509}};
constexpr int kOptCount = 10, kOptVisible = 7;
constexpr float kOptStep = 46;

int *OptValue(SystemData &d, int row) {
  switch (row) {
  case 1: return &d.bgmVol;
  case 2: return &d.voiceVol;
  case 3: return &d.seVol;
  case 4: return &d.charSpeed;
  case 5: return &d.autoWait;
  default: return nullptr;
  }
}

// VoiceOptionState: cell i = x * 8 + y.
constexpr const char *kVoiceCells[] = {"RU", "SA", "CU", "TA", "SI", "AN", "MO", "GI", "DE", "RI", "CH", "etc"};
constexpr int kVoiceCellCount = 12;
struct VoiceName {
  const char *name;
  float x, y;
};
constexpr VoiceName kVoiceNames[] = {{"RUIZE", 79, 83},    {"SAITO", 78, 126},   {"KYURUKE", 67, 169}, {"TABASA", 77, 212},
                                     {"SIESUTA", 67, 254}, {"ANRIETTA", 41, 298}, {"MONMON", 35, 340}, {"GISYU", 72, 382},
                                     {"DERUHU", 334, 83},  {"RISYU", 376, 125},  {"KURISU", 375, 168}, {"SONOTA", 375, 211}};

// scenelist: [thumbnail, scenario, sf flag].
struct SceneEntry {
  const char *thumb, *scene, *flag;
};
constexpr SceneEntry kSceneList[] = {{"TH_CG88", "STAAA_00", "sf.clear_ru"},  {"TH_CG90", "STAAB_00", "sf.clear_si"},
                                     {"TH_CG92", "STAAC_00", "sf.clear_an"},  {"TH_CG96", "STAAD_00", "sf.clear_ta"},
                                     {"TH_CG94", "STAAE_00", "sf.clear_cu"},  {"TH_CG98", "STAAF_00", "sf.clear_ch"},
                                     {"TH_CG100", "STAAG_00", "sf.clear_ri"}};
constexpr int kSceneCount = int(sizeof(kSceneList) / sizeof(kSceneList[0]));

// voicetitles: [scenario, title, unlock flag (null = isAllClear())].
struct VoiceDrama {
  const char *scene, *title, *flag;
};
constexpr VoiceDrama kVoiceDramas[] = {
    {"TUNDORAMA01", "ツンデレドラマ１", "sf.clear_ru"}, {"TUNDORAMA02", "ツンデレドラマ２", "sf.clear_si"},
    {"TUNDORAMA03", "ツンデレドラマ３", "sf.clear_an"}, {"TUNDORAMA04", "ツンデレドラマ４", "sf.clear_cu"},
    {"TUNDORAMA05", "ツンデレドラマ５", "sf.clear_ta"}, {"TUNDORAMA06", "ツンデレドラマ６", "sf.clear_ch"},
    {"TUNDORAMA07", "ツンデレドラマ７", "sf.clear_ri"}, {"TUNDORAMA08", "ツンデレドラマ８", nullptr},
    {"ALLCREAREV01", "オールクリアー・ルイズ", nullptr},    {"ALLCREAREV02", "オールクリアー・シエスタ", nullptr},
    {"ALLCREAREV03", "オールクリアー・キュルケ", nullptr},  {"ALLCREAREV04", "オールクリアー・タバサ", nullptr},
    {"ALLCREAREV05", "オールクリアー・アンリエッタ", nullptr}, {"ALLCREAREV06", "オールクリアー・クリス", nullptr},
    {"ALLCREAREV07", "オールクリアー・リシュ", nullptr}};
constexpr int kVoiceDramaCount = int(sizeof(kVoiceDramas) / sizeof(kVoiceDramas[0]));
constexpr int kBgmCount = int(sizeof(kBgmTitles) / sizeof(kBgmTitles[0]));
constexpr int kSoundRows = 6;

constexpr const char *kClearFlags[] = {"sf.clear_ru", "sf.clear_cu", "sf.clear_ta", "sf.clear_ch",
                                       "sf.clear_si", "sf.clear_an", "sf.clear_ri"};

} // namespace

void SystemData::ResetOptions() {
  charSpeed = 6, autoWait = 3, skipMode = 0, vibration = 1;
  voiceVol = 10, seVol = 10, bgmVol = 6, stereo = 1;
  voiceOn.clear();
}

bool SystemData::VoiceOn(const std::string &code) const {
  auto it = voiceOn.find(code);
  return it == voiceOn.end() || it->second != 0;
}

bool SystemData::Flag(const std::string &name) const {
  auto it = flags.find(name);
  return it != flags.end() && it->second != 0;
}

bool SystemData::AllClear() const {
  for (const char *f : kClearFlags)
    if (!Flag(f)) return false;
  return true;
}

bool SystemData::Load(const std::string &path) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    const int v = std::atoi(value.c_str());
    if (key == "charSpeed") charSpeed = std::clamp(v, 0, 10);
    else if (key == "autoWait") autoWait = std::clamp(v, 0, 10);
    else if (key == "skipMode") skipMode = v ? 1 : 0;
    else if (key == "vibration") vibration = v ? 1 : 0;
    else if (key == "voiceVol") voiceVol = std::clamp(v, 0, 10);
    else if (key == "seVol") seVol = std::clamp(v, 0, 10);
    else if (key == "bgmVol") bgmVol = std::clamp(v, 0, 10);
    else if (key == "stereo") stereo = v ? 1 : 0;
    else if (key.rfind("voice.", 0) == 0) voiceOn[key.substr(6)] = v;
    else if (key.rfind("flag.", 0) == 0) flags[key.substr(5)] = std::atoll(value.c_str());
    else if (key == "seen") seen.insert(value);
  }
  return true;
}

bool SystemData::Save(const std::string &path) const {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << "charSpeed=" << charSpeed << "\nautoWait=" << autoWait << "\nskipMode=" << skipMode
      << "\nvibration=" << vibration << "\nvoiceVol=" << voiceVol << "\nseVol=" << seVol << "\nbgmVol=" << bgmVol
      << "\nstereo=" << stereo << "\n";
  for (const auto &[k, v] : voiceOn) out << "voice." << k << "=" << v << "\n";
  for (const auto &[k, v] : flags) out << "flag." << k << "=" << v << "\n";
  for (const auto &n : seen) out << "seen=" << n << "\n";
  return bool(out);
}

std::string VoiceCode(const std::string &speaker) {
  for (const auto &[name, code] : kVoiceCodes)
    if (speaker == name) return code;
  return "etc";
}

void Frontend::Enter(bool boot) {
  pending_ = Result::None;
  replay_ = Replay::None;
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

void Frontend::Return(Replay from) {
  pending_ = Result::None;
  transition_ = false;
  replay_ = Replay::None;
  host_.StopVoice();
  if (from == Replay::Scene) {
    EnterGallery(false);
  } else if (from == Replay::Voice) {
    EnterSound(true);
  } else {
    // sceneend -> syssave -> logo: the attract loop without the warning screen.
    host_.StopMusic();
    voiceChoice_ = int(SDL_rand(int(sizeof(kTitleVoices) / sizeof(kTitleVoices[0]))));
    Go(Phase::Logo, false);
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
    if (!host_.Movie(0, &pss) || !movie_.Open(pss, host_.Renderer(), host_.MovieAudio(), 1.0f)) {
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
  replay_ = Replay::None;
  switch (item) {
  case 0: pending_ = Result::NewGame; break;
  case 1: pending_ = Result::Load; break;
  case 2: Go(Phase::Option, true); break;
  case 3: EnterGallery(true); break;
  case 4: EnterGallery(false); break;
  case 5: EnterSound(false); break;
  case 6: EnterSound(true); break;
  default: break;
  }
}

void Frontend::Back(Phase to) {
  host_.SystemSe("SYS_SE03");
  Go(to, true);
}

void Frontend::EnterGallery(bool cg) {
  galleryCg_ = cg;
  galleryPage_ = 1;
  curX_ = curY_ = 0;
  Go(Phase::Gallery, true);
}

void Frontend::EnterSound(bool voice) {
  soundVoice_ = voice;
  soundPage_ = 1;
  soundCur_ = 0;
  soundPlaying_ = false;
  if (voice) host_.Music("BGMA01");
  else host_.StopMusic();
  Go(Phase::Sound, true);
}

// ---- OptionState ------------------------------------------------------------

void Frontend::OptionKey(SDL_Keycode k) {
  if (k == SDLK_UP) {
    if (optRow_ > 0) optDisp_ = std::min(optDisp_, --optRow_);
    else optRow_ = kOptCount - 1, optDisp_ = kOptCount - kOptVisible;
    host_.SystemSe("SYS_SE01");
  } else if (k == SDLK_DOWN) {
    if (optRow_ < kOptCount - 1) {
      if (++optRow_ > optDisp_ + kOptVisible - 1) optDisp_ = optRow_ - (kOptVisible - 1);
    } else {
      optRow_ = optDisp_ = 0;
    }
    host_.SystemSe("SYS_SE01");
  } else if (k == SDLK_LEFT) {
    OptionChange(-1);
  } else if (k == SDLK_RIGHT) {
    OptionChange(+1);
  } else if (IsConfirm(k)) {
    if (optRow_ == 8) {
      host_.SystemSe("SYS_SE02");
      Go(Phase::VoiceOption, true);
    } else if (optRow_ == 9) {
      host_.SystemSe("SYS_SE02");
      confirm_ = true;
    }
  } else if (IsCancel(k)) {
    Back(Phase::Menu);
  }
}

// toleft / toright: left = more (STEREO, すべて, 振動あり) or less on the sliders.
void Frontend::OptionChange(int dir) {
  SystemData &d = host_.System();
  bool changed = false, voiceSample = false;
  if (optRow_ == 0) {
    const int want = dir < 0 ? 1 : 0;
    changed = d.stereo != want;
    d.stereo = want;
  } else if (int *v = OptValue(d, optRow_)) {
    const int n = std::clamp(*v + dir, 0, 10);
    changed = n != *v;
    voiceSample = changed && dir < 0 && optRow_ == 2;
    *v = n;
  } else if (optRow_ == 6) {
    const int want = dir < 0 ? 1 : 0;
    changed = d.skipMode != want;
    d.skipMode = want;
  } else if (optRow_ == 7) {
    const int want = dir < 0 ? 1 : 0;
    changed = d.vibration != want;
    d.vibration = want;
  }
  if (!changed) return;
  host_.SystemChanged();
  if (voiceSample) host_.SystemVoice(kTitleVoices[voiceChoice_][1]);
  else host_.SystemSe("SYS_SE01");
}

// ---- VoiceOptionState -------------------------------------------------------

void Frontend::VoiceOptionKey(SDL_Keycode k) {
  if (k == SDLK_UP || k == SDLK_DOWN) {
    const int step = k == SDLK_UP ? 7 : 1;
    do voiceY_ = (voiceY_ + step) % 8;
    while (voiceX_ * 8 + voiceY_ >= kVoiceCellCount);
    host_.SystemSe("SYS_SE01");
  } else if (k == SDLK_LEFT || k == SDLK_RIGHT) {
    const int nx = 1 - voiceX_;
    if (nx * 8 + voiceY_ < kVoiceCellCount) {
      voiceX_ = nx;
      host_.SystemSe("SYS_SE01");
    }
  } else if (IsConfirm(k)) {
    SystemData &d = host_.System();
    const char *code = kVoiceCells[voiceX_ * 8 + voiceY_];
    d.voiceOn[code] = d.VoiceOn(code) ? 0 : 1;
    host_.SystemChanged();
  } else if (IsCancel(k)) {
    Back(Phase::Option);
  }
}

// ---- CgModeMenuState --------------------------------------------------------

int Frontend::GalleryCount() const { return galleryCg_ ? int(kCgList.size()) : kSceneCount; }

bool Frontend::GalleryUnlocked(int n) const {
  const SystemData &d = const_cast<FrontendHost &>(host_).System();
  if (!galleryCg_) return d.Flag(kSceneList[n].flag);
  const auto &entry = kCgList[size_t(n)];
  for (size_t i = 1; i < entry.size(); ++i)
    if (d.seen.count(entry[i])) return true;
  return false;
}

void Frontend::GalleryPage(int dir) {
  const int pages = (GalleryCount() + 11) / 12;
  galleryPage_ = (galleryPage_ - 1 + dir + pages) % pages + 1;
  if ((galleryPage_ - 1) * 12 + curY_ * 4 + curX_ >= GalleryCount()) curX_ = curY_ = 0;
}

void Frontend::GalleryKey(SDL_Keycode k) {
  const int count = GalleryCount(), pages = (count + 11) / 12, base = (galleryPage_ - 1) * 12;
  auto exists = [&](int x, int y) { return base + y * 4 + x < count; };
  if (k == SDLK_UP || k == SDLK_DOWN) {
    int y = curY_;
    for (int i = 0; i < 3; ++i) {
      y = (y + (k == SDLK_UP ? 2 : 1)) % 3;
      if (exists(curX_, y)) break;
    }
    if (y != curY_ && exists(curX_, y)) {
      curY_ = y;
      host_.SystemSe("SYS_SE01");
    }
  } else if (k == SDLK_LEFT || k == SDLK_RIGHT) {
    const bool left = k == SDLK_LEFT;
    if (pages > 1) {
      if (left && curX_ == 0) {
        curX_ = 3;
        GalleryPage(-1);
      } else if (!left && (curX_ == 3 || !exists(curX_ + 1, curY_))) {
        curX_ = 0;
        GalleryPage(+1);
      } else {
        curX_ += left ? -1 : 1;
      }
      host_.SystemSe("SYS_SE01");
    } else {
      int x = curX_;
      for (int i = 0; i < 4; ++i) {
        x = (x + (left ? 3 : 1)) % 4;
        if (exists(x, curY_)) break;
      }
      if (x != curX_ && exists(x, curY_)) {
        curX_ = x;
        host_.SystemSe("SYS_SE01");
      }
    }
  } else if ((IsPrevPage(k) || IsNextPage(k)) && pages > 1) {
    GalleryPage(IsNextPage(k) ? 1 : -1);
    host_.SystemSe("SYS_SE02");
  } else if (IsConfirm(k)) {
    const int n = base + curY_ * 4 + curX_;
    if (n >= count || !GalleryUnlocked(n)) return;
    if (galleryCg_) {
      host_.SystemSe("SYS_SE02");
      OpenCg(n);
    } else if (host_.HasScene(kSceneList[n].scene)) {
      host_.SystemSe("SYS_SE02");
      scene_ = kSceneList[n].scene;
      replay_ = Replay::Scene;
      pending_ = Result::PlayScene;
    }
  } else if (IsCancel(k)) {
    Back(Phase::Menu);
  }
}

// ---- CgViewState ------------------------------------------------------------

void Frontend::OpenCg(int entry) {
  const SystemData &d = host_.System();
  view_.clear();
  const auto &list = kCgList[size_t(entry)];
  for (size_t i = 1; i < list.size(); ++i)
    if (d.seen.count(list[i])) view_.push_back(list[i]);
  viewIndex_ = 0;
  viewX_ = viewY_ = 0;
  viewFade_ = SDL_GetTicks();
  Go(Phase::CgView, false);
}

void Frontend::CgNext() {
  if (SDL_GetTicks() - viewFade_ < 500) {
    viewFade_ = 0; // a click finishes the fade first
    return;
  }
  if (++viewIndex_ >= view_.size()) {
    Go(Phase::Gallery, false);
    return;
  }
  viewX_ = viewY_ = 0;
  viewFade_ = SDL_GetTicks();
}

void Frontend::CgViewKey(SDL_Keycode k) {
  const SDL_FPoint size = viewIndex_ < view_.size() ? Size(host_.Picture(view_[viewIndex_])) : SDL_FPoint{kW, kH};
  constexpr float kRoll = 64;
  if (k == SDLK_UP) viewY_ = std::min(0.0f, viewY_ + kRoll);
  else if (k == SDLK_DOWN) viewY_ = std::min(0.0f, std::max(kH - size.y, viewY_ - kRoll));
  else if (k == SDLK_LEFT) viewX_ = std::min(0.0f, viewX_ + kRoll);
  else if (k == SDLK_RIGHT) viewX_ = std::min(0.0f, std::max(kW - size.x, viewX_ - kRoll));
  else if (IsConfirm(k)) CgNext();
  else if (IsCancel(k)) Back(Phase::Gallery);
}

// ---- SoundModeMenuState -----------------------------------------------------

int Frontend::SoundCount() const { return soundVoice_ ? kVoiceDramaCount : kBgmCount; }

bool Frontend::SoundUnlocked(int n) const {
  if (!soundVoice_) return true;
  const SystemData &d = const_cast<FrontendHost &>(host_).System();
  return kVoiceDramas[n].flag ? d.Flag(kVoiceDramas[n].flag) : d.AllClear();
}

std::string Frontend::SoundTitle(int n) const {
  if (!SoundUnlocked(n)) return "ＳＥＣＲＥＴ";
  return soundVoice_ ? kVoiceDramas[n].title : kBgmTitles[n].second;
}

void Frontend::SoundPage(int dir, bool) {
  const int pages = (SoundCount() + kSoundRows - 1) / kSoundRows;
  soundPage_ = (soundPage_ - 1 + dir + pages) % pages + 1;
  const int onPage = std::min(kSoundRows, SoundCount() - (soundPage_ - 1) * kSoundRows);
  soundCur_ = std::min(soundCur_, onPage - 1);
}

void Frontend::SoundKey(SDL_Keycode k) {
  const int base = (soundPage_ - 1) * kSoundRows;
  if (k == SDLK_UP) {
    if (soundCur_ > 0) --soundCur_;
    else soundCur_ = kSoundRows - 1, SoundPage(-1, false);
    host_.SystemSe("SYS_SE01");
  } else if (k == SDLK_DOWN) {
    if (soundCur_ < kSoundRows - 1 && base + soundCur_ + 1 < SoundCount()) ++soundCur_;
    else soundCur_ = 0, SoundPage(+1, false);
    host_.SystemSe("SYS_SE01");
  } else if (IsPrevPage(k) || IsNextPage(k) || k == SDLK_LEFT || k == SDLK_RIGHT) {
    SoundPage(IsNextPage(k) || k == SDLK_RIGHT ? 1 : -1, false);
    host_.SystemSe("SYS_SE02");
  } else if (IsConfirm(k)) {
    const int n = base + soundCur_;
    if (n >= SoundCount()) return;
    if (!soundVoice_) {
      host_.StopMusic();
      host_.Music(kBgmTitles[n].first);
      soundPlaying_ = true;
    } else if (SoundUnlocked(n) && host_.HasScene(kVoiceDramas[n].scene)) {
      host_.SystemSe("SYS_SE02");
      scene_ = kVoiceDramas[n].scene;
      replay_ = Replay::Voice;
      pending_ = Result::PlayScene;
    }
  } else if (IsCancel(k)) {
    if (!soundVoice_ && soundPlaying_) {
      host_.StopMusic();
      soundPlaying_ = false;
    } else {
      Back(Phase::Menu);
    }
  }
}

// Pointer support (the original is pad only): hover moves the cursor, a click
// presses, the right button is cross and the wheel scrolls.
void Frontend::MenuEvent(const SDL_Event &e, SDL_Keycode key, bool click, SDL_FPoint pt) {
  const bool motion = e.type == SDL_EVENT_MOUSE_MOTION;
  const bool wheel = e.type == SDL_EVENT_MOUSE_WHEEL;
  const SDL_Keycode wheelKey = wheel ? (e.wheel.y > 0 ? SDLK_UP : SDLK_DOWN) : 0;
  if (confirm_) {
    const bool yes = (key && IsConfirm(key)) || (click && Inside(pt, 177, 262, 136, 40));
    const bool no = (key && IsCancel(key)) || (click && Inside(pt, 331, 262, 136, 40));
    if (yes) {
      host_.SystemSe("SYS_SE02");
      host_.System().ResetOptions();
      host_.SystemChanged();
      confirm_ = false;
    } else if (no) {
      host_.SystemSe("SYS_SE03");
      confirm_ = false;
    }
    return;
  }
  switch (phase_) {
  case Phase::Option:
    if (key) OptionKey(key);
    else if (wheel) OptionKey(wheelKey);
    else if (motion || click) {
      for (int row = optDisp_; row < std::min(kOptCount, optDisp_ + kOptVisible); ++row) {
        const float y = kOptRows[row].offY - float(optDisp_) * kOptStep;
        if (!Inside(pt, kMenuX, y, 560, kMenuH - 1)) continue;
        if (row != optRow_ && motion) host_.SystemSe("SYS_SE01");
        optRow_ = row;
        if (!click) break;
        if (row >= 8) OptionKey(SDLK_RETURN);
        else if (OptValue(host_.System(), row) && pt.x >= 307) {
          int *v = OptValue(host_.System(), row);
          const int want = std::clamp(int((pt.x - 338.0f) / 21.0f + 0.5f), 0, 10);
          if (want != *v) OptionChange(want < *v ? -(*v - want) : want - *v);
        } else if (pt.x >= 337) {
          OptionChange(pt.x < 460 ? -1 : 1);
        }
        break;
      }
    }
    break;
  case Phase::VoiceOption:
    if (key) VoiceOptionKey(key);
    else if (wheel) VoiceOptionKey(wheelKey);
    else if (motion || click) {
      for (int i = 0; i < kVoiceCellCount; ++i) {
        const int x = i / 8, y = i % 8;
        if (!Inside(pt, 23.0f + 298.0f * float(x), 87.0f + 43.0f * float(y), 284, 28)) continue;
        if ((voiceX_ != x || voiceY_ != y) && motion) host_.SystemSe("SYS_SE01");
        voiceX_ = x, voiceY_ = y;
        if (click) VoiceOptionKey(SDLK_RETURN);
      }
    }
    break;
  case Phase::Gallery: {
    if (key) GalleryKey(key);
    else if (wheel) GalleryKey(e.wheel.y > 0 ? SDLK_PAGEUP : SDLK_PAGEDOWN);
    else if (click && Inside(pt, 18, 237, 40, 60)) GalleryKey(SDLK_PAGEUP);
    else if (click && Inside(pt, 572, 237, 46, 60)) GalleryKey(SDLK_PAGEDOWN);
    else if (motion || click) {
      for (int i = 0; i < 12; ++i) {
        const int x = i % 4, y = i / 4;
        if ((galleryPage_ - 1) * 12 + i >= GalleryCount()) break;
        if (!Inside(pt, 40.0f + 144.0f * float(x), 96.0f + 112.0f * float(y), 136, 104)) continue;
        if ((curX_ != x || curY_ != y) && motion) host_.SystemSe("SYS_SE01");
        curX_ = x, curY_ = y;
        if (click) GalleryKey(SDLK_RETURN);
      }
    }
    break;
  }
  case Phase::CgView:
    if (key) CgViewKey(key);
    else if (wheel) CgViewKey(wheelKey);
    else if (click) CgNext();
    break;
  case Phase::Sound:
    if (key) SoundKey(key);
    else if (wheel) SoundKey(e.wheel.y > 0 ? SDLK_PAGEUP : SDLK_PAGEDOWN);
    else if (motion || click) {
      const int base = (soundPage_ - 1) * kSoundRows;
      for (int i = 0; i < kSoundRows && base + i < SoundCount(); ++i) {
        if (!Inside(pt, 80, 110.0f + 50.0f * float(i), 365, 40)) continue;
        if (soundCur_ != i && motion) host_.SystemSe("SYS_SE01");
        soundCur_ = i;
        if (click) SoundKey(SDLK_RETURN);
      }
    }
    break;
  default: break;
  }
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
  case Phase::Option:
  case Phase::VoiceOption:
  case Phase::Gallery:
  case Phase::CgView:
  case Phase::Sound: {
    if (transition_) break;
    const bool keyDown = e.type == SDL_EVENT_KEY_DOWN;
    SDL_Keycode k = keyDown && (!e.key.repeat || e.key.key == SDLK_UP || e.key.key == SDLK_DOWN ||
                                e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT)
                        ? e.key.key
                        : 0;
    const bool rightClick = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT;
    if (rightClick) k = SDLK_ESCAPE;
    SDL_FPoint pt{0, 0};
    if (click) pt = {e.button.x, e.button.y};
    else if (e.type == SDL_EVENT_MOUSE_MOTION) pt = {e.motion.x, e.motion.y};
    MenuEvent(e, k, click, pt);
    break;
  }
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
  default: break;
  }
  const Result r = pending_;
  pending_ = Result::None;
  return r;
}

void Frontend::DrawImage(SDL_Renderer *r, Kotonoha::CanvasImage *img, float x, float y, float alpha, float light,
                         float w, float h) {
  SDL_Texture *tex = img ? img->Source() : nullptr;
  if (!tex || alpha <= 0.0f) return;
  float tw = 0, th = 0;
  SDL_GetTextureSize(tex, &tw, &th);
  const SDL_FRect src{0, 0, w > 0 ? std::min(w, tw) : tw, h > 0 ? std::min(h, th) : th};
  const SDL_FRect dst{x, y, src.w, src.h};
  const auto level = uint8_t(std::clamp(light, 0.0f, 1.0f) * 255.0f);
  SDL_SetTextureAlphaModFloat(tex, std::clamp(alpha, 0.0f, 1.0f));
  SDL_SetTextureColorMod(tex, level, level, level);
  img->Draw(r, &src, &dst);
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
  case Phase::Option: DrawOption(r, alpha); break;
  case Phase::VoiceOption: DrawVoiceOption(r, alpha); break;
  case Phase::Gallery: DrawGallery(r, alpha); break;
  case Phase::CgView: DrawCgView(r, alpha); break;
  case Phase::Sound: DrawSound(r, alpha); break;
  }
}

void Frontend::DrawPageNumber(SDL_Renderer *r, int page, int pages, float alpha) {
  auto digit = [&](int d) { return Img("FAILES_PAGE_" + std::to_string(d)); };
  DrawImage(r, Img("FAILES_PAGE_BASE"), 465, 26, alpha);
  if (pages > 9) {
    DrawImage(r, digit(pages / 10), 552, 25, alpha);
    DrawImage(r, digit(page / 10), 470, 25, alpha);
  }
  DrawImage(r, digit(pages % 10), 576, 25, alpha);
  DrawImage(r, digit(page % 10), 494, 25, alpha);
}

void Frontend::DrawOption(SDL_Renderer *r, float alpha) {
  const SystemData &d = host_.System();
  DrawImage(r, Img("SYSTEM_BG00"), 0, 0, alpha);
  DrawImage(r, Img("OPTION_OPTI_TITLE"), 0, 24, alpha, 1.0f, 640, 46);
  for (int row = optDisp_; row < std::min(kOptCount, optDisp_ + kOptVisible); ++row) {
    const float dy = -float(optDisp_) * kOptStep;
    const bool on = row == optRow_;
    const std::string label = std::string("OPTION_OPTI_") + kOptRows[row].label + (on ? "_ON" : "_OFF");
    DrawImage(r, Img(label), kMenuX, (on ? kOptRows[row].onY : kOptRows[row].offY) + dy, alpha, on ? 1.0f : kDarkLevel);
    if (row == 0) {
      DrawImage(r, Img("OPTION_OPTI_SOUNDBOARD"), 337, 94 + dy, alpha);
      if (d.stereo) DrawImage(r, Img("OPTION_OPTI_STEREO"), 347, 100 + dy, alpha);
      else DrawImage(r, Img("OPTION_OPTI_MONO"), 359, 100 + dy, alpha);
    } else if (row <= 5) {
      const std::string n = std::to_string(row);
      const float y = 142.0f + 46.0f * float(row - 1);
      DrawImage(r, Img("OPTION_OPTI_BER0" + n), 307, y + dy, alpha);
      DrawImage(r, Img("OPTION_OPTI_ICON0" + n), 338.0f + 21.0f * float(*OptValue(const_cast<SystemData &>(d), row)),
                y + 6 + dy, alpha);
    } else if (row == 6) {
      DrawImage(r, Img("OPTION_OPTI_SKIPBOARD"), 337, 372 + dy, alpha);
      if (d.skipMode) DrawImage(r, Img("OPTION_OPTI_SUBETE"), 347, 378 + dy, alpha);
      else DrawImage(r, Img("OPTION_OPTI_KIDOKU"), 378, 378 + dy, alpha);
    } else if (row == 7) {
      DrawImage(r, Img("OPTION_OPTI_ARKBOARD"), 337, 416 + dy, alpha);
      if (d.vibration) DrawImage(r, Img("OPTION_OPTI_SINDOARI"), 347, 422 + dy, alpha);
      else DrawImage(r, Img("OPTION_OPTI_SINDONASI"), 382, 422 + dy, alpha);
    }
  }
  DrawImage(r, Img("OPTION_UE"), 154, 83, alpha);
  DrawImage(r, Img("OPTION_SITA"), 155, 405, alpha);
  if (confirm_ && phase_ == Phase::Option) DrawConfirm(r);
}

void Frontend::DrawConfirm(SDL_Renderer *r) {
  DrawImage(r, Img("MESSEGE_DATA_SYS_WINDOW"), 0, 140, 1.0f);
  const std::string text = "初期化してよろしいですか？";
  const SDL_FPoint size = host_.DrawText(r, text, 0, 0, 16, {0, 0, 0, 255}, true);
  host_.DrawText(r, text, (kW - size.x) / 2, (kH - size.y) / 2 - 12, 16, {0, 0, 0, 255});
  DrawImage(r, Img("MESSEGE_DATA_BO_MARU"), 177, 262, 1.0f);
  DrawImage(r, Img("MESSEGE_DATA_BO_BATU"), 331, 262, 1.0f);
}

void Frontend::DrawVoiceOption(SDL_Renderer *r, float alpha) {
  const SystemData &d = host_.System();
  DrawImage(r, Img("SYSTEM_BG00"), 0, 0, alpha);
  DrawImage(r, Img("VOICEOPTION_VOICE_TITLE"), 0, 25, alpha);
  const bool allSeen = false;
  for (int i = 0; i < kVoiceCellCount; ++i) {
    const float x = float(i / 8), y = float(i % 8);
    const bool cur = i == voiceX_ * 8 + voiceY_;
    if (cur) DrawImage(r, Img("VOICEOPTION_COMMAND_ON"), 24 + 298 * x, 87 + 43 * y, alpha);
    else DrawImage(r, Img("VOICEOPTION_COMMAND_OFF"), 23 + 298 * x, 87 + 43 * y, alpha, kDarkLevel);
    if (d.VoiceOn(kVoiceCells[i])) DrawImage(r, Img("VOICEOPTION_ON"), 238 + 298 * x, 90 + 43 * y, alpha);
    else DrawImage(r, Img("VOICEOPTION_OFF"), 234 + 298 * x, 90 + 43 * y, alpha);
    const VoiceName &n = kVoiceNames[i];
    const bool hidden = (i == 9 && !(allSeen || d.Flag("sf.ri_appear"))) || (i == 10 && !(allSeen || d.Flag("sf.ch_appear")));
    if (hidden) DrawImage(r, Img("VOICEOPTION_HATENA"), 394, i == 9 ? 126 : 169, alpha);
    else DrawImage(r, Img(std::string("VOICEOPTION_") + n.name), n.x, n.y, alpha);
  }
}

void Frontend::DrawGallery(SDL_Renderer *r, float alpha) {
  const int count = GalleryCount(), pages = (count + 11) / 12;
  DrawImage(r, Img("SYSTEM_BG00"), 0, 0, alpha);
  if (galleryCg_) DrawImage(r, Img("GALLERY_TITLE_CGGALLERY"), 0, 26, alpha, 1.0f, 640, 44);
  else DrawImage(r, Img("SCENE_TITLE_SCENEGALLERY"), -8, 25, alpha, 1.0f, 648, 45);
  DrawPageNumber(r, galleryPage_, pages, alpha);
  if (pages > 1) {
    DrawImage(r, Img("SCENE_HIDARI"), 28, 237, alpha);
    DrawImage(r, Img("FILE_FILE_L1"), 18, 277, alpha);
    DrawImage(r, Img("SCENE_MIGI"), 592, 237, alpha);
    DrawImage(r, Img("FILE_FILE_R1"), 572, 277, alpha);
  }
  for (int i = 0; i < 12; ++i) {
    const int n = (galleryPage_ - 1) * 12 + i;
    if (n >= count) break;
    const int x = i % 4, y = i / 4;
    const std::string thumb = !GalleryUnlocked(n) ? "GALLERY_CG_MINI" : galleryCg_ ? kCgList[size_t(n)][0] : kSceneList[n].thumb;
    const bool cur = x == curX_ && y == curY_;
    DrawImage(r, Img(thumb), 40.0f + 144.0f * float(x), 96.0f + 112.0f * float(y), alpha * (cur ? 1.0f : 0.5f));
  }
}

void Frontend::DrawCgView(SDL_Renderer *r, float alpha) {
  const SDL_FRect all{0, 0, kW, kH};
  SDL_SetRenderDrawColor(r, 0, 0, 0, uint8_t(alpha * 255.0f));
  SDL_RenderFillRect(r, &all);
  if (viewIndex_ >= view_.size()) return;
  const float fade = viewFade_ ? Ramp(SDL_GetTicks() - viewFade_, 500) : 1.0f;
  if (fade < 1.0f && viewIndex_ > 0) DrawImage(r, host_.Picture(view_[viewIndex_ - 1]), 0, 0, alpha);
  Kotonoha::CanvasImage *img = host_.Picture(view_[viewIndex_]);
  DrawImage(r, img, viewX_, viewY_, alpha * fade);
  const SDL_FPoint size = Size(img);
  if (size.y > kH) {
    DrawImage(r, Img("OPTION_UE"), 300, 12, alpha);
    DrawImage(r, Img("OPTION_SITA"), 300, 420, alpha);
  }
  if (size.x > kW) {
    DrawImage(r, Img("SCENE_HIDARI"), 20, 237, alpha);
    DrawImage(r, Img("SCENE_MIGI"), 600, 237, alpha);
  }
}

void Frontend::DrawSound(SDL_Renderer *r, float alpha) {
  const int count = SoundCount(), pages = (count + kSoundRows - 1) / kSoundRows, base = (soundPage_ - 1) * kSoundRows;
  DrawImage(r, Img("SYSTEM_BG00"), 0, 0, alpha);
  DrawImage(r, Img(soundVoice_ ? "SOUND_TITLE_SPECIALLVOICE" : "SOUND_TITLE_SOUNDGALLERY"), 0, 23, alpha);
  if (!soundVoice_) DrawImage(r, Img("SOUND_SOUND_TEACH"), 485, 327, alpha);
  DrawPageNumber(r, soundPage_, pages, alpha);
  DrawImage(r, Img("OPTION_UE"), 250, 96, alpha);
  DrawImage(r, Img("OPTION_SITA"), 250, 386, alpha);
  for (int i = 0; i < kSoundRows && base + i < count; ++i) {
    const bool cur = i == soundCur_;
    const float y = 110.0f + 50.0f * float(i);
    DrawImage(r, Img(cur ? "SOUND_SOUNDBAR_ON" : "SOUND_SOUNDBAR_OFF"), 80, y, alpha, cur ? 1.0f : kDarkLevel);
    host_.DrawText(r, SoundTitle(base + i), 96, y + 2, 16, {0, 0, 0, uint8_t(alpha * 255.0f)});
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
