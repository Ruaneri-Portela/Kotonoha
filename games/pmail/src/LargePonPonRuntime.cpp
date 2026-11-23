#include "LargePonPonRuntime.hpp"
#include <Kotonoha/Formats/Crgb.hpp>
#include <Kotonoha/media/DecodedImageSurface.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/InputMapper.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace Kotonoha::PmailPort {

namespace {

constexpr std::array<SDL_Point, 3> kTitlePage0Pos = {
    {{287, 353}, {287, 398}, {287, 444}}};
constexpr std::array<SDL_Point, 4> kTitlePage1Pos = {
    {{260, 341}, {260, 388}, {260, 433}, {260, 478}}};

constexpr std::array<SDL_Point, 11> kMapLocPos = {{{573, 88},
                                                   {573, 182},
                                                   {424, 135},
                                                   {273, 88},
                                                   {106, 88},
                                                   {273, 182},
                                                   {273, 275},
                                                   {106, 275},
                                                   {106, 182},
                                                   {273, 368},
                                                   {71, 391}}};

constexpr std::array<const char *, 11> kMapLocNames = {
    "駅",   "商店街", "公園",   "学校", "自宅",  "喫茶店",
    "神社", "海岸",   "プール", "病院", "その他"};

uint64_t transitionMsFromSpeed(int32_t speed) {
  switch (speed) {
  case 0:
    return 500;
  case 1:
    return 250;
  case 2:
    return 1000;
  case 3:
    return 125;
  case 4:
    return 2000;
  default:
    return 400;
  }
}

uint64_t bgmFadeMsFromSpeed(int32_t speed) {
  switch (speed) {
  case 0:
    return 1000;
  case 1:
    return 500;
  case 2:
    return 2000;
  case 3:
    return 250;
  case 4:
    return 4000;
  default:
    return 1000;
  }
}

bool pointInRect(float x, float y, float rx, float ry, float rw, float rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

} // namespace

LargePonPonRuntime::LargePonPonRuntime(
    std::string gameDirectory,
    std::shared_ptr<Kotonoha::Providers::LargePonPonAssets> assets)
    : Runtime("Large PonPon", "0.1.0", "me.hirameki.kotonoha.pmail"),
      gameDirectory_(std::move(gameDirectory)), assets_(std::move(assets)),
      vm_(*this), frontBuffer_(static_cast<size_t>(kW) * kH * 4, 0),
      backBuffer_(static_cast<size_t>(kW) * kH * 4, 0),
      displayBuffer_(static_cast<size_t>(kW) * kH * 4, 0) {
  // Set alpha = 255 on initial black buffers
  for (size_t i = 3; i < frontBuffer_.size(); i += 4) {
    frontBuffer_[i] = 255;
    backBuffer_[i] = 255;
    displayBuffer_[i] = 255;
  }
}

LargePonPonRuntime::~LargePonPonRuntime() { shutdown(); }

void LargePonPonRuntime::shutdown() {
  bgmPlayer_.Stop();
  voicePlayer_.Stop();
  sePlayer_.Stop();
  canvasTex_.reset();
  if (assets_)
    assets_->clearCache();
}

void LargePonPonRuntime::initGlobal(Kotonoha::Engine &engine) {
  engine_ = &engine;
  if (booted_)
    return;
  bootGame(true);
}

void LargePonPonRuntime::initSplash(Kotonoha::Engine &) {}
void LargePonPonRuntime::initMenu(Kotonoha::Engine &) {}
void LargePonPonRuntime::initGameplay(Kotonoha::Engine &) {}

void LargePonPonRuntime::bootGame(bool firstBoot) {
  if (!assets_)
    return;
  vm_.setGlobalConfig(assets_->config().config);
  vm_.setPlayerNames(assets_->config().names);
  loadAllPersistentFlags();

  auto defineBytes = assets_->loadScriptBytes("DEFINE");
  if (!defineBytes.empty())
    vm_.loadDefine(std::move(defineBytes));

  booted_ = true;
  mode_ = Mode::Running;
  const char *entryScript = firstBoot ? "TITLE" : "START";
  if (!vm_.jumpScript(entryScript, 0)) {
    std::cerr << "[LargePonPon] Failed to load entry script: " << entryScript
              << std::endl;
    return;
  }
  stepVm();
}

void LargePonPonRuntime::stepVm() {
  if (mode_ != Mode::Running || vm_.halted())
    return;
  vm_.runUntilYield(4096);
}

bool LargePonPonRuntime::waitingForInput() const {
  return mode_ == Mode::WaitClick || mode_ == Mode::Choice ||
         mode_ == Mode::TitleMenu || mode_ == Mode::MapMode ||
         mode_ == Mode::HeartMode || mode_ == Mode::CgMode ||
         mode_ == Mode::NameInput || mode_ == Mode::SaveLoadDialog ||
         mode_ == Mode::EndingWait;
}

void LargePonPonRuntime::advanceInput() {
  if (mode_ == Mode::WaitClick) {
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::Transition) {
    finishTransition();
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::WaitTimer && !(waitFlags_ & 1)) {
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::WaitVoice && !voiceWaitNonSkippable_) {
    voicePlayer_.Stop();
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::EndingWait) {
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::Choice && !choices_.empty()) {
    const int idx =
        std::clamp(selectedChoice_, 0, static_cast<int>(choices_.size()) - 1);
    vm_.setW(1, choices_[static_cast<size_t>(idx)].id);
    choices_.clear();
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::TitleMenu) {
    vm_.setW(1, 0);
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::NameInput) {
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::HeartMode) {
    vm_.setW(1, -1);
    mode_ = Mode::Running;
    stepVm();
  } else if (mode_ == Mode::CgMode) {
    vm_.setW(1, -1);
    mode_ = Mode::Running;
    stepVm();
  }
}

// ---- Graphics Helpers ----

void LargePonPonRuntime::blitSurfaceToBuffer(std::vector<uint8_t> &dst,
                                             SDL_Surface *src, int dstX,
                                             int dstY, bool alphaBlend) {
  if (!src || !src->pixels)
    return;
  const auto *srcPixels = static_cast<const uint8_t *>(src->pixels);
  const int sw = src->w;
  const int sh = src->h;
  const int spitch = src->pitch;

  for (int y = 0; y < sh; ++y) {
    const int ty = dstY + y;
    if (ty < 0 || ty >= kH)
      continue;
    const uint8_t *srow = srcPixels + y * spitch;
    uint8_t *drow = dst.data() + static_cast<size_t>(ty) * kW * 4;
    for (int x = 0; x < sw; ++x) {
      const int tx = dstX + x;
      if (tx < 0 || tx >= kW)
        continue;
      const uint8_t sr = srow[x * 4 + 0];
      const uint8_t sg = srow[x * 4 + 1];
      const uint8_t sb = srow[x * 4 + 2];
      const uint8_t sa = srow[x * 4 + 3];
      uint8_t *dp = drow + tx * 4;
      if (!alphaBlend || sa == 255) {
        dp[0] = sr;
        dp[1] = sg;
        dp[2] = sb;
        dp[3] = 255;
      } else if (sa > 0) {
        const uint32_t inv = 255 - sa;
        dp[0] = static_cast<uint8_t>((sr * sa + dp[0] * inv) >> 8);
        dp[1] = static_cast<uint8_t>((sg * sa + dp[1] * inv) >> 8);
        dp[2] = static_cast<uint8_t>((sb * sa + dp[2] * inv) >> 8);
        dp[3] = 255;
      }
    }
  }
}

void LargePonPonRuntime::drawCanmToBuffer(std::vector<uint8_t> &dst, int slot,
                                          int type, int frame) {
  if (slot < 0 || slot >= 3)
    return;
  for (const auto &ch : canm_[static_cast<size_t>(slot)].slots) {
    if (ch.frames.empty())
      continue;
    if (type > 0 && ch.slotType != type)
      continue;
    const size_t fi = static_cast<size_t>(
        std::clamp(frame, 0, static_cast<int>(ch.frames.size()) - 1));
    int ox = ch.x;
    int oy = ch.y;
    if (slot == 1 || slot == 2) {
      ox += charPos_[static_cast<size_t>(slot - 1)].x;
      oy += charPos_[static_cast<size_t>(slot - 1)].y;
    }
    blitSurfaceToBuffer(dst, ch.frames[fi].get(), ox, oy, true);
  }
}

void LargePonPonRuntime::renderTransitionFrame(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const size_t pixels = static_cast<size_t>(kW) * kH;
  if (transType_ == 0x40) {
    // Fade from black -> backBuffer
    const uint32_t a = static_cast<uint32_t>(t * 256.0f);
    for (size_t i = 0; i < pixels; ++i) {
      displayBuffer_[i * 4 + 0] =
          static_cast<uint8_t>((backBuffer_[i * 4 + 0] * a) >> 8);
      displayBuffer_[i * 4 + 1] =
          static_cast<uint8_t>((backBuffer_[i * 4 + 1] * a) >> 8);
      displayBuffer_[i * 4 + 2] =
          static_cast<uint8_t>((backBuffer_[i * 4 + 2] * a) >> 8);
      displayBuffer_[i * 4 + 3] = 255;
    }
  } else if (transType_ == 0x41) {
    // Fade from front -> black
    const uint32_t inv = static_cast<uint32_t>((1.0f - t) * 256.0f);
    for (size_t i = 0; i < pixels; ++i) {
      displayBuffer_[i * 4 + 0] =
          static_cast<uint8_t>((transStartFront_[i * 4 + 0] * inv) >> 8);
      displayBuffer_[i * 4 + 1] =
          static_cast<uint8_t>((transStartFront_[i * 4 + 1] * inv) >> 8);
      displayBuffer_[i * 4 + 2] =
          static_cast<uint8_t>((transStartFront_[i * 4 + 2] * inv) >> 8);
      displayBuffer_[i * 4 + 3] = 255;
    }
  } else if (transType_ == 0x42) {
    // Fade from white -> backBuffer
    const uint32_t a = static_cast<uint32_t>(t * 256.0f);
    const uint32_t inv = 256 - a;
    for (size_t i = 0; i < pixels; ++i) {
      displayBuffer_[i * 4 + 0] =
          static_cast<uint8_t>((backBuffer_[i * 4 + 0] * a + 255 * inv) >> 8);
      displayBuffer_[i * 4 + 1] =
          static_cast<uint8_t>((backBuffer_[i * 4 + 1] * a + 255 * inv) >> 8);
      displayBuffer_[i * 4 + 2] =
          static_cast<uint8_t>((backBuffer_[i * 4 + 2] * a + 255 * inv) >> 8);
      displayBuffer_[i * 4 + 3] = 255;
    }
  } else if (transType_ == 0x43) {
    // Fade from front -> white
    const uint32_t a = static_cast<uint32_t>(t * 256.0f);
    const uint32_t inv = 256 - a;
    for (size_t i = 0; i < pixels; ++i) {
      displayBuffer_[i * 4 + 0] = static_cast<uint8_t>(
          (transStartFront_[i * 4 + 0] * inv + 255 * a) >> 8);
      displayBuffer_[i * 4 + 1] = static_cast<uint8_t>(
          (transStartFront_[i * 4 + 1] * inv + 255 * a) >> 8);
      displayBuffer_[i * 4 + 2] = static_cast<uint8_t>(
          (transStartFront_[i * 4 + 2] * inv + 255 * a) >> 8);
      displayBuffer_[i * 4 + 3] = 255;
    }
  } else if (!transMask_.empty() && transMask_.size() == pixels) {
    const int thresh = static_cast<int>(t * 255.0f);
    const bool reverse = (transType_ & 1) != 0;
    for (size_t i = 0; i < pixels; ++i) {
      const int mv = reverse ? (255 - transMask_[i]) : transMask_[i];
      const uint8_t *src =
          (mv <= thresh) ? &backBuffer_[i * 4] : &transStartFront_[i * 4];
      displayBuffer_[i * 4 + 0] = src[0];
      displayBuffer_[i * 4 + 1] = src[1];
      displayBuffer_[i * 4 + 2] = src[2];
      displayBuffer_[i * 4 + 3] = 255;
    }
  } else {
    // Standard crossfade
    const uint32_t a = static_cast<uint32_t>(t * 256.0f);
    const uint32_t inv = 256 - a;
    for (size_t i = 0; i < pixels; ++i) {
      displayBuffer_[i * 4 + 0] = static_cast<uint8_t>(
          (backBuffer_[i * 4 + 0] * a + transStartFront_[i * 4 + 0] * inv) >>
          8);
      displayBuffer_[i * 4 + 1] = static_cast<uint8_t>(
          (backBuffer_[i * 4 + 1] * a + transStartFront_[i * 4 + 1] * inv) >>
          8);
      displayBuffer_[i * 4 + 2] = static_cast<uint8_t>(
          (backBuffer_[i * 4 + 2] * a + transStartFront_[i * 4 + 2] * inv) >>
          8);
      displayBuffer_[i * 4 + 3] = 255;
    }
  }
  displayDirty_ = true;
}

void LargePonPonRuntime::finishTransition() {
  if (transType_ == 0x41) {
    for (size_t i = 0; i < frontBuffer_.size(); i += 4) {
      frontBuffer_[i + 0] = 0;
      frontBuffer_[i + 1] = 0;
      frontBuffer_[i + 2] = 0;
      frontBuffer_[i + 3] = 255;
    }
  } else if (transType_ == 0x43) {
    std::fill(frontBuffer_.begin(), frontBuffer_.end(), 255);
  } else {
    frontBuffer_ = backBuffer_;
  }
  displayBuffer_ = frontBuffer_;
  displayDirty_ = true;
}

// ---- LargePonPonHost Implementation ----

std::vector<uint8_t> LargePonPonRuntime::loadScript(const std::string &name) {
  return assets_ ? assets_->loadScriptBytes(name) : std::vector<uint8_t>{};
}

void LargePonPonRuntime::onMsgAppend(const std::vector<uint16_t> &words) {
  auto decoded = vm_.decodeTextWords(words);
  if (decoded.fontProfile != 0)
    currentFontProfile_ = decoded.fontProfile;
  pendingText_ += decoded.utf8;
}

bool LargePonPonRuntime::onMsgShow() {
  textBoxVisible_ = true;
  currentText_ = pendingText_;
  pendingText_.clear();
  if (!attachedVoice_.empty()) {
    onVoicePlay(attachedVoice_, attachedVoiceCh_);
    attachedVoice_.clear();
  }
  mode_ = Mode::WaitClick;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onWinOpen() {
  textBoxVisible_ = true;
  return false;
}

bool LargePonPonRuntime::onWinClose() {
  textBoxVisible_ = false;
  currentText_.clear();
  currentSpeaker_.clear();
  return false;
}

void LargePonPonRuntime::onSetSpeaker(const std::vector<uint16_t> &words) {
  auto decoded = vm_.decodeTextWords(words);
  if (decoded.fontProfile != 0)
    currentFontProfile_ = decoded.fontProfile;
  std::string s = decoded.utf8;
  // Single space clears speaker nameplate
  if (s == " " || s == "　")
    s.clear();
  currentSpeaker_ = std::move(s);
}

void LargePonPonRuntime::onChoiceClear() {
  textBoxVisible_ = false;
  choices_.clear();
}

void LargePonPonRuntime::onChoiceAdd(int32_t id, int32_t flag,
                                     const std::vector<uint16_t> &words) {
  auto decoded = vm_.decodeTextWords(words);
  choices_.push_back({id, flag, decoded.utf8});
}

bool LargePonPonRuntime::onChoiceRun(int32_t defaultId) {
  if (choices_.empty()) {
    vm_.setW(1, defaultId);
    return false;
  }
  selectedChoice_ = 0;
  for (size_t i = 0; i < choices_.size(); ++i) {
    if (choices_[i].id == defaultId) {
      selectedChoice_ = static_cast<int>(i);
      break;
    }
  }
  hoveredChoice_ = -1;
  mode_ = Mode::Choice;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

void LargePonPonRuntime::onSetSeenFlag(int32_t) {}

void LargePonPonRuntime::onAttachVoice(const std::string &voice,
                                       int32_t channel) {
  attachedVoice_ = voice;
  attachedVoiceCh_ = channel;
}

bool LargePonPonRuntime::onTransition(int32_t type, int32_t speed) {
  transType_ = type;
  transMask_.clear();
  if (type == 0) {
    frontBuffer_ = backBuffer_;
    displayBuffer_ = frontBuffer_;
    displayDirty_ = true;
    return false;
  }

  // Load transition wipe mask if applicable
  if (assets_) {
    if (type == 2)
      transMask_ = assets_->loadAlphaMask("SIMA200", false);
    else if (type == 4 || type == 5)
      transMask_ = assets_->loadAlphaMask("SURI200", false);
    else if (type == 7)
      transMask_ = assets_->loadAlphaMask("SURI800", true);
    else if (type == 8 || type == 9)
      transMask_ = assets_->loadAlphaMask("SURI200R", false);
    else if (type == 0x10 || type == 0x11)
      transMask_ = assets_->loadAlphaMask("HART200", false);
    else if (type == 0x14 || type == 0x15)
      transMask_ = assets_->loadAlphaMask("TAKO200", false);
    else if (type == 0x18 || type == 0x19)
      transMask_ = assets_->loadAlphaMask("STAR200", false);
    else if (type == 0x1C || type == 0x1D)
      transMask_ = assets_->loadAlphaMask("MARU200", false);
  }

  transStartFront_ = frontBuffer_;
  transDurationMs_ = transitionMsFromSpeed(speed);
  mode_ = Mode::Transition;
  modeStartMs_ = SDL_GetTicks();
  renderTransitionFrame(0.0f);
  return true;
}

void LargePonPonRuntime::onLoadCg(const std::string &name) {
  if (assets_)
    cgSurface_ = assets_->loadCrgb(name);
  sceneFlags_ = 2;
  for (auto &c : canm_)
    c = {};
}

void LargePonPonRuntime::onLoadBg(const std::string &name) {
  if (assets_)
    bgSurface_ = assets_->loadBgd(name);
  sceneFlags_ = (sceneFlags_ & 0x300u) | 1u;
  for (auto &c : canm_)
    c = {};
}

void LargePonPonRuntime::onLoadChar(int32_t slot, const std::string &name) {
  if (slot < 0 || slot > 1)
    return;
  if (name.empty() || name == "@") {
    charSurfaces_[static_cast<size_t>(slot)].reset();
    sceneFlags_ &= ~(0x100u << slot);
    canm_[static_cast<size_t>(slot + 1)] = {};
  } else if (assets_) {
    charSurfaces_[static_cast<size_t>(slot)] = assets_->loadChar(name);
    sceneFlags_ |= (0x100u << slot);
  }
}

void LargePonPonRuntime::onFillColor(int32_t bgr24) {
  fillColor_.r = static_cast<uint8_t>((bgr24 >> 16) & 0xFF);
  fillColor_.g = static_cast<uint8_t>((bgr24 >> 8) & 0xFF);
  fillColor_.b = static_cast<uint8_t>(bgr24 & 0xFF);
  fillColor_.a = 255;
  sceneFlags_ = 0x80;
  for (auto &c : canm_)
    c = {};
}

bool LargePonPonRuntime::onPlayPchr(const std::string &pchr, int32_t flags,
                                    int32_t x, int32_t y, int32_t loops,
                                    const std::string &se) {
  if (!assets_)
    return false;
  pchrFrames_ = assets_->loadPchrFrames(pchr);
  if (pchrFrames_.empty())
    return false;
  pchrFlags_ = flags;
  pchrX_ = x;
  pchrY_ = y;
  pchrLoops_ = std::max(1, loops);
  pchrFrameIdx_ = 0;
  pchrLoopsDone_ = 0;
  if (!se.empty() && se != "@" && engine_) {
    auto adp = assets_->loadAdp4(se);
    if (!adp.samples.empty()) {
      Kotonoha::PcmPlayer::Clip clip;
      clip.samples = std::move(adp.samples);
      clip.channels = adp.channels;
      clip.rate = adp.sampleRate;
      sePlayer_.Play(engine_->GetAudio().channel("Se"), std::move(clip));
    }
  }
  mode_ = Mode::PlayPchr;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onScreenShake(int32_t dx, int32_t dy, int32_t reps) {
  if (reps <= 0)
    return false;
  shakeDx_ = dx;
  shakeDy_ = dy;
  shakeReps_ = reps;
  shakeOffsetX_ = 0;
  shakeOffsetY_ = 0;
  mode_ = Mode::Shake;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

void LargePonPonRuntime::onLoadCgVariant(const std::string &name, int32_t h,
                                         int32_t p) {
  std::string full = name;
  const int bits = (h != 0 ? 1 : 0) | (p != 0 ? 2 : 0);
  if (bits == 1)
    full += "_H";
  else if (bits == 2)
    full += "_P";
  else if (bits == 3)
    full += "_HP";
  onLoadCg(full);
}

void LargePonPonRuntime::onLoadCanm(int32_t slot, const std::string &name) {
  if (slot < 0 || slot >= 3 || !assets_)
    return;
  if (name.empty() || name == "@")
    canm_[static_cast<size_t>(slot)] = {};
  else
    canm_[static_cast<size_t>(slot)] = assets_->loadCanm(name);
}

void LargePonPonRuntime::onComposite() {
  if (sceneFlags_ & 0x80) {
    for (size_t i = 0; i < backBuffer_.size(); i += 4) {
      backBuffer_[i + 0] = fillColor_.r;
      backBuffer_[i + 1] = fillColor_.g;
      backBuffer_[i + 2] = fillColor_.b;
      backBuffer_[i + 3] = 255;
    }
  } else if ((sceneFlags_ & 2) && cgSurface_) {
    blitSurfaceToBuffer(backBuffer_, cgSurface_.get(), 0, 0, false);
    drawCanmToBuffer(backBuffer_, 0, 0, 0);
  } else if ((sceneFlags_ & 1) && bgSurface_) {
    blitSurfaceToBuffer(backBuffer_, bgSurface_.get(), 0, 0, false);
  }

  if (!(sceneFlags_ & 2)) {
    for (int s = 0; s < 2; ++s) {
      if ((sceneFlags_ & (0x100u << s)) && charSurfaces_[s]) {
        blitSurfaceToBuffer(backBuffer_, charSurfaces_[s].get(), charPos_[s].x,
                            charPos_[s].y, true);
        drawCanmToBuffer(backBuffer_, s + 1, 0, 0);
      }
    }
  }
}

void LargePonPonRuntime::onDrawCanm(int32_t buf, int32_t slot, int32_t type,
                                    int32_t frame) {
  if (buf == 0) {
    drawCanmToBuffer(frontBuffer_, slot, type, frame);
    displayBuffer_ = frontBuffer_;
    displayDirty_ = true;
  } else {
    drawCanmToBuffer(backBuffer_, slot, type, frame);
  }
}

void LargePonPonRuntime::onSetCharPos(int32_t slot, int32_t x, int32_t y) {
  if (slot == 0 || slot == 1)
    charPos_[static_cast<size_t>(slot)] = {x, y};
}

void LargePonPonRuntime::onBgmPlay(const std::string &name, int32_t loop) {
  if (!assets_ || !engine_)
    return;
  bgmFading_ = false;
  auto clip = assets_->loadSmfdAudio(name);
  if (clip.data.empty())
    return;
  Kotonoha::PcmPlayer::Clip pcm;
  pcm.channels = clip.spec.channels;
  pcm.rate = clip.spec.freq;
  pcm.loop = (loop != 0);
  pcm.samples.resize(clip.data.size() / sizeof(int16_t));
  std::memcpy(pcm.samples.data(), clip.data.data(), clip.data.size());
  bgmPlayer_.Play(engine_->GetAudio().channel("BGM"), std::move(pcm));
  bgmPlayer_.SetGain(1.0f);
}

void LargePonPonRuntime::onBgmStop() {
  bgmFading_ = false;
  bgmPlayer_.Stop();
}

void LargePonPonRuntime::onBgmFade(int32_t speed) {
  if (!bgmPlayer_.Active())
    return;
  bgmFading_ = true;
  bgmFadeStartMs_ = SDL_GetTicks();
  bgmFadeDurationMs_ = bgmFadeMsFromSpeed(speed);
}

void LargePonPonRuntime::onVoicePlay(const std::string &name, int32_t) {
  if (!assets_ || !engine_ || name.empty() || name == "@")
    return;
  auto adp = assets_->loadAdp4(name);
  if (adp.samples.empty())
    return;
  voiceMouthFrames_ = std::move(adp.mouthFrames);
  Kotonoha::PcmPlayer::Clip pcm;
  pcm.samples = std::move(adp.samples);
  pcm.channels = adp.channels;
  pcm.rate = adp.sampleRate;
  pcm.loop = false;
  voicePlayer_.Play(engine_->GetAudio().channel("Voice"), std::move(pcm));
}

void LargePonPonRuntime::onVoiceStop() {
  voicePlayer_.Stop();
  voiceMouthFrames_.clear();
}

bool LargePonPonRuntime::onVoiceWait(int32_t flags) {
  if (!voicePlayer_.Playing())
    return false;
  voiceWaitNonSkippable_ = (flags & 1) != 0;
  mode_ = Mode::WaitVoice;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

void LargePonPonRuntime::onSetVoiceSync(int32_t val) { voiceSyncVal_ = val; }

bool LargePonPonRuntime::onWait(int32_t ms, int32_t flags) {
  if (ms <= 0 && !(flags & 0x80))
    return false;
  waitDurationMs_ = (flags & 0x80) ? 0xFFFFFFFFull : static_cast<uint64_t>(ms);
  waitFlags_ = flags;
  mode_ = Mode::WaitTimer;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onNameInput() {
  mode_ = Mode::NameInput;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onMapMode() {
  ensureMapPack();
  mapHover_ = -1;
  mode_ = Mode::MapMode;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

void LargePonPonRuntime::onSaveCheckpoint() {}

void LargePonPonRuntime::onSetSaveEnable(int32_t enable) {
  saveEnabled_ = (enable != 0);
}

bool LargePonPonRuntime::onCheckSaveData() {
  for (int i = 0; i < 20; ++i) {
    if (std::filesystem::exists(saveSlotPath(i)))
      return true;
  }
  return false;
}

bool LargePonPonRuntime::onLoadDialog() {
  modeBeforeSaveDialog_ = Mode::Running;
  saveDialogIsLoad_ = true;
  mode_ = Mode::SaveLoadDialog;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onReturnTitle(int32_t noSysSave) {
  if (noSysSave == 0)
    saveSlot(99);
  bgmPlayer_.Stop();
  voicePlayer_.Stop();
  sePlayer_.Stop();
  textBoxVisible_ = false;
  currentText_.clear();
  currentSpeaker_.clear();
  vm_.resetForTitle();
  vm_.jumpScript("START", 0);
  mode_ = Mode::Running;
  return false;
}

bool LargePonPonRuntime::onHeartMode(int32_t mode) {
  ensureHeartPack();
  heartModeArg_ = mode;
  mode_ = Mode::HeartMode;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onCgMode(int32_t) {
  ensureCgModePack();
  cgHover_ = -1;
  mode_ = Mode::CgMode;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onTitleMenu(int32_t page) {
  ensureTitlePack();
  titlePage_ = page;
  titleHover_ = -1;
  mode_ = Mode::TitleMenu;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

bool LargePonPonRuntime::onEndingWait(int32_t count) {
  waitDurationMs_ = static_cast<uint64_t>(std::max(1, count)) * 13300ull;
  waitFlags_ = 0;
  mode_ = Mode::EndingWait;
  modeStartMs_ = SDL_GetTicks();
  return true;
}

// ---- Save / Load Storage ----

std::filesystem::path LargePonPonRuntime::saveDir() const {
  return Kotonoha::applicationDirectories("pmail").saves / "largeponpon" /
         "SAVEDATA";
}

std::filesystem::path LargePonPonRuntime::saveSlotPath(int slot) const {
  char name[32];
  std::snprintf(name, sizeof(name), "SAVEDATA%02d.DAT", slot);
  return saveDir() / name;
}

void LargePonPonRuntime::loadAllPersistentFlags() {
  for (int i = 0; i < 20; ++i) {
    std::ifstream f(saveSlotPath(i), std::ios::binary);
    if (!f)
      continue;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    vm_.mergeGlobalFlagsFromSave(buf, false);
  }
  {
    std::ifstream f(saveSlotPath(99), std::ios::binary);
    if (f) {
      std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
      vm_.mergeGlobalFlagsFromSave(buf, true);
    }
  }
}

bool LargePonPonRuntime::saveSlot(int slot) {
  std::error_code ec;
  std::filesystem::create_directories(saveDir(), ec);
  auto bytes = vm_.serializeSaveData(slot != 99);
  std::ofstream f(saveSlotPath(slot), std::ios::binary | std::ios::trunc);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char *>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  return f.good();
}

bool LargePonPonRuntime::loadSlot(int slot) {
  std::ifstream f(saveSlotPath(slot), std::ios::binary);
  if (!f)
    return false;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  std::string scriptName;
  uint32_t pc = 0;
  if (!vm_.deserializeSaveData(buf, scriptName, pc))
    return false;
  if (!vm_.jumpScript(scriptName, pc))
    return false;
  textBoxVisible_ = false;
  currentText_.clear();
  pendingText_.clear();
  mode_ = Mode::Running;
  stepVm();
  return true;
}

void LargePonPonRuntime::saveGame(const std::string &path) {
  auto bytes = vm_.serializeSaveData(true);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (f) {
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  }
}

void LargePonPonRuntime::loadGame(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
  std::string scriptName;
  uint32_t pc = 0;
  if (vm_.deserializeSaveData(buf, scriptName, pc) &&
      vm_.jumpScript(scriptName, pc)) {
    mode_ = Mode::Running;
    stepVm();
  }
}

// ---- Sprite Bank Loaders ----

void LargePonPonRuntime::ensureTitlePack() {
  if (titleSprites_.empty() && assets_)
    titleSprites_ = assets_->loadPrgbSprites("TITLEPACK");
}

void LargePonPonRuntime::ensureMapPack() {
  if (mapSprites_.empty() && assets_)
    mapSprites_ = assets_->loadPrgbSprites("PACKMAP");
}

void LargePonPonRuntime::ensureHeartPack() {
  if (heartSprites_.empty() && assets_)
    heartSprites_ = assets_->loadPrgbSprites("PACKHART");
}

void LargePonPonRuntime::ensureCgModePack() {
  if (cgModeSprites_.empty() && assets_)
    cgModeSprites_ = assets_->loadPrgbSprites("PACKCGMD");
  if (assets_) {
    for (size_t i = 0; i < 4; ++i) {
      if (cgP256Raw_[i].empty()) {
        char name[32];
        std::snprintf(name, sizeof(name), "CGMODE%02zu.P256", i + 1);
        cgP256Raw_[i] = assets_->graphicPack().read(name);
      }
    }
  }
}

void LargePonPonRuntime::ensureMesIcon() {
  if (mesIconFrames_.empty() && assets_)
    mesIconFrames_ = assets_->loadIconFrames("MESICON");
}

// ---- Update Loop ----

void LargePonPonRuntime::updateAudioFade(uint64_t now) {
  if (!bgmFading_)
    return;
  const uint64_t elapsed = now - bgmFadeStartMs_;
  if (elapsed >= bgmFadeDurationMs_) {
    bgmPlayer_.Stop();
    bgmFading_ = false;
  } else {
    const float g = 1.0f - static_cast<float>(elapsed) /
                               static_cast<float>(bgmFadeDurationMs_);
    bgmPlayer_.SetGain(std::clamp(g, 0.0f, 1.0f));
  }
}

void LargePonPonRuntime::update() {
  const uint64_t now = SDL_GetTicks();
  updateAudioFade(now);

  switch (mode_) {
  case Mode::Running:
    stepVm();
    break;
  case Mode::Transition: {
    const uint64_t elapsed = now - modeStartMs_;
    if (elapsed >= transDurationMs_) {
      finishTransition();
      mode_ = Mode::Running;
      stepVm();
    } else {
      renderTransitionFrame(static_cast<float>(elapsed) /
                            static_cast<float>(transDurationMs_));
    }
    break;
  }
  case Mode::WaitTimer:
  case Mode::EndingWait: {
    if (now - modeStartMs_ >= waitDurationMs_) {
      mode_ = Mode::Running;
      stepVm();
    }
    break;
  }
  case Mode::WaitVoice: {
    if (!voicePlayer_.Playing()) {
      mode_ = Mode::Running;
      stepVm();
    }
    break;
  }
  case Mode::PlayPchr: {
    const uint64_t elapsed = now - modeStartMs_;
    const size_t totalFrame = static_cast<size_t>(elapsed / 125);
    const size_t n = pchrFrames_.size();
    if (n == 0 || totalFrame >= n * static_cast<size_t>(pchrLoops_)) {
      if ((pchrFlags_ & 0x80) && n > 0 && pchrFrames_.back()) {
        blitSurfaceToBuffer(frontBuffer_, pchrFrames_.back().get(), pchrX_,
                            pchrY_, true);
        displayBuffer_ = frontBuffer_;
        displayDirty_ = true;
      }
      pchrFrames_.clear();
      mode_ = Mode::Running;
      stepVm();
    } else {
      pchrFrameIdx_ = totalFrame % n;
    }
    break;
  }
  case Mode::Shake: {
    const uint64_t step = (now - modeStartMs_) / 125;
    if (step >= static_cast<uint64_t>(shakeReps_) * 4) {
      shakeOffsetX_ = 0;
      shakeOffsetY_ = 0;
      mode_ = Mode::Running;
      stepVm();
    } else {
      switch (step % 4) {
      case 0:
        shakeOffsetX_ = -shakeDx_;
        shakeOffsetY_ = -shakeDy_;
        break;
      case 2:
        shakeOffsetX_ = shakeDx_;
        shakeOffsetY_ = shakeDy_;
        break;
      default:
        shakeOffsetX_ = 0;
        shakeOffsetY_ = 0;
        break;
      }
    }
    break;
  }
  default:
    break;
  }
}

// ---- Rendering ----

void LargePonPonRuntime::ensureCanvasTexture(SDL_Renderer *renderer) {
  if (!canvasTex_) {
    canvasTex_.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING, kW, kH));
  }
}

void LargePonPonRuntime::uploadDisplayBuffer(SDL_Renderer *renderer) {
  ensureCanvasTexture(renderer);
  if (!canvasTex_)
    return;
  if (displayDirty_) {
    SDL_UpdateTexture(canvasTex_.get(), nullptr, displayBuffer_.data(), kW * 4);
    ++canvasSerial_;
    displayDirty_ = false;
  }
}

void LargePonPonRuntime::drawSurfaceAt(SDL_Renderer *renderer, SDL_Surface *src,
                                       int x, int y) {
  if (!src)
    return;
  TexturePtr tex(SDL_CreateTextureFromSurface(renderer, src));
  if (!tex)
    return;
  SDL_FRect dst{static_cast<float>(x), static_cast<float>(y),
                static_cast<float>(src->w), static_cast<float>(src->h)};
  Kotonoha::RenderUpscaledTexture(renderer, tex.get(), nullptr, &dst);
}

void LargePonPonRuntime::render(Kotonoha::RendererRef rendererRef, float) {
  SDL_Renderer *r = &rendererRef;
  uploadDisplayBuffer(r);

  if (canvasTex_) {
    SDL_FRect dst{static_cast<float>(shakeOffsetX_),
                  static_cast<float>(shakeOffsetY_), static_cast<float>(kW),
                  static_cast<float>(kH)};
    Kotonoha::RenderUpscaledTexture(r, canvasTex_.get(), nullptr, &dst,
                                    canvasSerial_);
  }

  // Live eye-blink & lip-sync CANM overlay when not mid-transition
  if (mode_ != Mode::Transition) {
    const uint64_t now = SDL_GetTicks();
    const bool blinkClosed = ((now % 3800) > 3650);
    int mouthFrame = 0;
    if (voicePlayer_.Playing() && !voiceMouthFrames_.empty()) {
      const size_t idx = static_cast<size_t>(voicePlayer_.Position() * 10.0);
      if (idx < voiceMouthFrames_.size() && voiceMouthFrames_[idx] != 0)
        mouthFrame = 1;
    }
    for (int slot = 0; slot < 3; ++slot) {
      for (const auto &ch : canm_[static_cast<size_t>(slot)].slots) {
        if (ch.frames.size() < 2)
          continue;
        int fi = 0;
        if (ch.slotType == 1 && blinkClosed)
          fi = static_cast<int>(ch.frames.size()) - 1;
        else if (ch.slotType == 2 && mouthFrame > 0)
          fi = std::min(mouthFrame, static_cast<int>(ch.frames.size()) - 1);
        if (fi == 0)
          continue;
        int ox = ch.x + (slot > 0 ? charPos_[slot - 1].x : 0);
        int oy = ch.y + (slot > 0 ? charPos_[slot - 1].y : 0);
        drawSurfaceAt(r, ch.frames[static_cast<size_t>(fi)].get(), ox, oy);
      }
    }
  }

  // Active PCHR frame overlay
  if (mode_ == Mode::PlayPchr && pchrFrameIdx_ < pchrFrames_.size()) {
    drawSurfaceAt(r, pchrFrames_[pchrFrameIdx_].get(), pchrX_, pchrY_);
  }

  // Dialogue Box (0, 448, 800, 152)
  if (textBoxVisible_ && engine_) {
    auto &ui = engine_->GetUI();
    const bool solidBlack = (vm_.getGW(24) == 0);
    const uint8_t alpha = solidBlack ? 255 : (vm_.getGW(18) == 1 ? 192 : 128);
    ui.panel(rendererRef, {0.0f, 448.0f, 800.0f, 152.0f}, {0, 0, 0, alpha});

    SDL_Color textColor{240, 240, 232, 255};
    if (assets_ && currentFontProfile_ >= 0 && currentFontProfile_ < 21)
      textColor = assets_->config().fonts[currentFontProfile_].color;

    float textY = 460.0f;
    if (!currentSpeaker_.empty()) {
      ui.textSized(rendererRef, currentSpeaker_, 72.0f, textY, 656.0f, 24.0f,
                   textColor);
      textY += 30.0f;
    }
    if (!currentText_.empty()) {
      ui.textSized(rendererRef, currentText_, 72.0f, textY, 656.0f, 26.0f,
                   textColor);
    }

    if (mode_ == Mode::WaitClick) {
      ensureMesIcon();
      if (!mesIconFrames_.empty()) {
        const size_t fi =
            static_cast<size_t>((SDL_GetTicks() / 120) % mesIconFrames_.size());
        drawSurfaceAt(r, mesIconFrames_[fi].get(), 756, 564);
      }
    }
  }

  // Choice Menu
  if (mode_ == Mode::Choice && engine_ && !choices_.empty()) {
    auto &ui = engine_->GetUI();
    const int count = static_cast<int>(choices_.size());
    const float y0 = static_cast<float>((15 - count) * 20);
    float maxW = 240.0f;
    for (const auto &c : choices_)
      maxW = std::max(maxW, ui.textWidth(c.text, 24.0f) + 48.0f);
    const float barW = std::min(720.0f, maxW);
    const float barX = (800.0f - barW) * 0.5f;

    for (int i = 0; i < count; ++i) {
      const float y = y0 + static_cast<float>(i) * 44.0f;
      const bool active = (i == hoveredChoice_ || i == selectedChoice_);
      SDL_Color bg =
          active ? SDL_Color{40, 80, 140, 220} : SDL_Color{10, 15, 25, 190};
      ui.panel(rendererRef, {barX, y, barW, 40.0f}, bg);
      SDL_Color fg = active ? SDL_Color{255, 255, 180, 255}
                            : SDL_Color{240, 240, 245, 255};
      ui.textSized(rendererRef, choices_[static_cast<size_t>(i)].text,
                   barX + 20.0f, y + 8.0f, barW - 40.0f, 24.0f, fg);
    }
  }

  // Title Menu (0xFC)
  if (mode_ == Mode::TitleMenu) {
    ensureTitlePack();
    const int count = (titlePage_ == 0) ? 3 : 4;
    for (int i = 0; i < count; ++i) {
      const int bitIdx = (titlePage_ == 0) ? (90 + i) : (93 + i);
      const bool enabled = vm_.getBit(bitIdx);
      const int baseSprite = ((titlePage_ == 0) ? i : (3 + i)) * 3;
      const int stateOff = !enabled ? 0 : (titleHover_ == i ? 2 : 1);
      const int spriteIdx = baseSprite + stateOff;
      const SDL_Point pos =
          (titlePage_ == 0) ? kTitlePage0Pos[i] : kTitlePage1Pos[i];
      if (spriteIdx >= 0 &&
          static_cast<size_t>(spriteIdx) < titleSprites_.size()) {
        drawSurfaceAt(r, titleSprites_[static_cast<size_t>(spriteIdx)].get(),
                      pos.x, pos.y);
      } else if (engine_) {
        static const char *kP0[] = {"New Game", "Load Game", "Extras"};
        static const char *kP1[] = {"CG Mode", "BGM Mode", "H Mode", "Back"};
        engine_->GetUI().button(rendererRef,
                                {static_cast<float>(pos.x),
                                 static_cast<float>(pos.y), 220.0f, 40.0f},
                                (titlePage_ == 0) ? kP0[i] : kP1[i],
                                titleHover_ == i);
      }
    }
  }

  // Map Mode (0xF4)
  if (mode_ == Mode::MapMode && engine_) {
    auto &ui = engine_->GetUI();
    for (int i = 0; i < 11; ++i) {
      const auto pos = kMapLocPos[static_cast<size_t>(i)];
      const bool hov = (mapHover_ == i);
      ui.panel(
          rendererRef,
          {static_cast<float>(pos.x), static_cast<float>(pos.y), 110.0f, 34.0f},
          hov ? SDL_Color{220, 120, 40, 230} : SDL_Color{20, 30, 50, 200});
      ui.textSized(rendererRef, kMapLocNames[static_cast<size_t>(i)],
                   static_cast<float>(pos.x + 10),
                   static_cast<float>(pos.y + 6), 90.0f, 20.0f,
                   {255, 255, 255, 255});
    }
    char hud[128];
    std::snprintf(hud, sizeof(hud), "Day %d  Time %d  Stamina %d",
                  vm_.getW(131), vm_.getW(128), vm_.getW(130));
    ui.panel(rendererRef, {40.0f, 480.0f, 720.0f, 90.0f}, {10, 15, 25, 210});
    ui.textSized(rendererRef, hud, 60.0f, 495.0f, 420.0f, 22.0f,
                 {245, 245, 220, 255});
    ui.button(rendererRef, {520.0f, 515.0f, 100.0f, 38.0f}, "Heart", false);
    ui.button(rendererRef, {635.0f, 515.0f, 100.0f, 38.0f}, "Rest", false);
  }

  // Heart Mode (0xFA)
  if (mode_ == Mode::HeartMode && engine_) {
    auto &ui = engine_->GetUI();
    ui.panel(rendererRef, {100.0f, 80.0f, 600.0f, 440.0f}, {20, 10, 25, 220});
    ui.textSized(rendererRef, "Affection Status (Click or Esc to continue)",
                 130.0f, 100.0f, 540.0f, 24.0f, {255, 210, 230, 255});
    for (int i = 0; i < 5; ++i) {
      char line[64];
      std::snprintf(line, sizeof(line), "Heroine %d: %d  [%s]", i + 1,
                    vm_.getW(16 + i), vm_.getBit(90 + i) ? "Active" : "-");
      ui.textSized(rendererRef, line, 150.0f,
                   160.0f + static_cast<float>(i) * 52.0f, 480.0f, 24.0f,
                   {255, 240, 245, 255});
    }
  }

  // CG Mode (0xFB)
  if (mode_ == Mode::CgMode && engine_) {
    ensureCgModePack();
    auto &ui = engine_->GetUI();
    const int p256Idx = cgPage_ / 4;
    const int baseInFile = (cgPage_ % 4) * 20;
    if (p256Idx >= 0 && p256Idx < 4 && !cgP256Raw_[p256Idx].empty()) {
      const auto &raw = cgP256Raw_[p256Idx];
      const size_t total =
          Kotonoha::Formats::Crgb::p256ThumbnailCount(raw.size());
      for (int i = 0; i < 20; ++i) {
        const size_t ti = static_cast<size_t>(baseInFile + i);
        if (ti >= total)
          break;
        const int col = i % 5;
        const int row = i / 5;
        const int x = 24 + col * 152;
        const int y = 26 + row * 114;
        auto thumb = Kotonoha::Formats::Crgb::decodeP256Thumbnail(
            raw.data(), raw.size(), ti, cgHover_ == i);
        auto thumbSurface = Kotonoha::media::surfaceFromDecoded(thumb);
        drawSurfaceAt(r, thumbSurface.get(), x, y);
      }
    }
    ui.button(rendererRef, {32.0f, 487.0f, 120.0f, 38.0f}, "Prev", false);
    ui.button(rendererRef, {186.0f, 487.0f, 120.0f, 38.0f}, "Next", false);
    char pageBuf[32];
    std::snprintf(pageBuf, sizeof(pageBuf), "Page %d / 16", cgPage_ + 1);
    ui.textSized(rendererRef, pageBuf, 330.0f, 495.0f, 160.0f, 22.0f,
                 {255, 255, 255, 255});
    ui.button(rendererRef, {653.0f, 487.0f, 115.0f, 38.0f}, "Exit", false);
  }

  // Name Input (0xF3)
  if (mode_ == Mode::NameInput && engine_) {
    auto &ui = engine_->GetUI();
    ui.panel(rendererRef, {160.0f, 140.0f, 480.0f, 300.0f}, {15, 20, 35, 235});
    ui.textSized(rendererRef, "Protagonist Name", 200.0f, 165.0f, 400.0f, 26.0f,
                 {255, 240, 200, 255});
    const auto &names = vm_.playerNames();
    ui.textSized(rendererRef, "Family: " + names[0] + " (" + names[2] + ")",
                 200.0f, 220.0f, 400.0f, 24.0f, {240, 240, 245, 255});
    ui.textSized(rendererRef, "Given:  " + names[1] + " (" + names[3] + ")",
                 200.0f, 265.0f, 400.0f, 24.0f, {240, 240, 245, 255});
    ui.button(rendererRef, {310.0f, 350.0f, 180.0f, 44.0f}, "Confirm", true);
  }

  // Save / Load Dialog
  if (mode_ == Mode::SaveLoadDialog && engine_) {
    auto &ui = engine_->GetUI();
    ui.panel(rendererRef, {60.0f, 40.0f, 680.0f, 520.0f}, {15, 20, 35, 240});
    ui.textSized(rendererRef,
                 saveDialogIsLoad_ ? "Load Game (Click slot or Esc)"
                                   : "Save Game (Click slot or Esc)",
                 90.0f, 56.0f, 520.0f, 24.0f, {255, 235, 180, 255});
    for (int i = 0; i < 20; ++i) {
      const int col = i / 10;
      const int row = i % 10;
      const float sx = 90.0f + static_cast<float>(col) * 320.0f;
      const float sy = 96.0f + static_cast<float>(row) * 42.0f;
      const bool exists = std::filesystem::exists(saveSlotPath(i));
      char label[64];
      std::snprintf(label, sizeof(label), "Slot %02d: %s", i + 1,
                    exists ? "SAVED" : "(empty)");
      ui.button(rendererRef, {sx, sy, 290.0f, 36.0f}, label, false);
    }
  }
}

void LargePonPonRuntime::renderOverlay(Kotonoha::RendererRef, float) {}

// ---- Input Handling ----

bool LargePonPonRuntime::onEvent(Kotonoha::EventRef eventRef) {
  const bool inDialogue = (mode_ == Mode::Running);
  const SDL_Event ev = Kotonoha::InputMapper::toKeyEvent(
      eventRef, inDialogue ? Kotonoha::InputContext::Dialogue
                           : Kotonoha::InputContext::Menu);

  if (ev.type == SDL_EVENT_MOUSE_MOTION) {
    mouseX_ = ev.motion.x;
    mouseY_ = ev.motion.y;
    if (mode_ == Mode::TitleMenu) {
      titleHover_ = -1;
      const int count = (titlePage_ == 0) ? 3 : 4;
      for (int i = 0; i < count; ++i) {
        const SDL_Point pos =
            (titlePage_ == 0) ? kTitlePage0Pos[i] : kTitlePage1Pos[i];
        if (pointInRect(mouseX_, mouseY_, static_cast<float>(pos.x),
                        static_cast<float>(pos.y), 220.0f, 40.0f)) {
          titleHover_ = i;
          break;
        }
      }
    } else if (mode_ == Mode::Choice && !choices_.empty()) {
      hoveredChoice_ = -1;
      const int count = static_cast<int>(choices_.size());
      const float y0 = static_cast<float>((15 - count) * 20);
      for (int i = 0; i < count; ++i) {
        if (pointInRect(mouseX_, mouseY_, 40.0f,
                        y0 + static_cast<float>(i) * 44.0f, 720.0f, 40.0f)) {
          hoveredChoice_ = i;
          selectedChoice_ = i;
          break;
        }
      }
    } else if (mode_ == Mode::MapMode) {
      mapHover_ = -1;
      for (int i = 0; i < 11; ++i) {
        if (pointInRect(mouseX_, mouseY_, static_cast<float>(kMapLocPos[i].x),
                        static_cast<float>(kMapLocPos[i].y), 110.0f, 34.0f)) {
          mapHover_ = i;
          break;
        }
      }
    } else if (mode_ == Mode::CgMode) {
      cgHover_ = -1;
      for (int i = 0; i < 20; ++i) {
        const int col = i % 5;
        const int row = i / 5;
        if (pointInRect(mouseX_, mouseY_, static_cast<float>(24 + col * 152),
                        static_cast<float>(26 + row * 114), 150.0f, 112.0f)) {
          cgHover_ = i;
          break;
        }
      }
    }
    return false;
  }

  if (ev.type == SDL_EVENT_KEY_DOWN) {
    const SDL_Keycode key = ev.key.key;
    if (key == SDLK_F5 && saveEnabled_ && mode_ != Mode::SaveLoadDialog) {
      modeBeforeSaveDialog_ = mode_;
      saveDialogIsLoad_ = false;
      mode_ = Mode::SaveLoadDialog;
      return true;
    }
    if (key == SDLK_F9 && mode_ != Mode::SaveLoadDialog) {
      modeBeforeSaveDialog_ = mode_;
      saveDialogIsLoad_ = true;
      mode_ = Mode::SaveLoadDialog;
      return true;
    }
    if (Kotonoha::InputMapper::isBackKey(key)) {
      if (mode_ == Mode::SaveLoadDialog) {
        mode_ = modeBeforeSaveDialog_;
        if (mode_ == Mode::Running)
          stepVm();
        return true;
      }
      if (mode_ == Mode::CgMode || mode_ == Mode::HeartMode ||
          mode_ == Mode::MapMode) {
        vm_.setW(1, -1);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
      if (mode_ == Mode::TitleMenu && titlePage_ == 1) {
        vm_.setW(1, 3);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
    }
    if (mode_ == Mode::TitleMenu) {
      const int count = (titlePage_ == 0) ? 3 : 4;
      if (key == SDLK_UP || key == SDLK_LEFT) {
        titleHover_ = titleHover_ < 0 ? 0 : (titleHover_ + count - 1) % count;
        return true;
      }
      if (key == SDLK_DOWN || key == SDLK_RIGHT) {
        titleHover_ = titleHover_ < 0 ? 0 : (titleHover_ + 1) % count;
        return true;
      }
      if (Kotonoha::InputMapper::isConfirmKey(key)) {
        const int idx =
            std::clamp(titleHover_ < 0 ? 0 : titleHover_, 0, count - 1);
        const int bitIdx = (titlePage_ == 0) ? (90 + idx) : (93 + idx);
        if (vm_.getBit(bitIdx)) {
          vm_.setW(1, idx);
          mode_ = Mode::Running;
          stepVm();
        }
        return true;
      }
    }
    if (mode_ == Mode::MapMode) {
      if (key == SDLK_UP || key == SDLK_LEFT) {
        mapHover_ = mapHover_ < 0 ? 0 : (mapHover_ + 10) % 11;
        return true;
      }
      if (key == SDLK_DOWN || key == SDLK_RIGHT) {
        mapHover_ = mapHover_ < 0 ? 0 : (mapHover_ + 1) % 11;
        return true;
      }
      if (Kotonoha::InputMapper::isConfirmKey(key) && mapHover_ >= 0 &&
          mapHover_ < 11) {
        vm_.setW(1, mapHover_);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
    }
    if (mode_ == Mode::CgMode) {
      if (key == SDLK_LEFT) {
        cgHover_ = cgHover_ < 0 ? 0 : (cgHover_ + 19) % 20;
        return true;
      }
      if (key == SDLK_RIGHT) {
        cgHover_ = cgHover_ < 0 ? 0 : (cgHover_ + 1) % 20;
        return true;
      }
      if (key == SDLK_UP) {
        cgHover_ = cgHover_ < 0 ? 0 : (cgHover_ + 15) % 20;
        return true;
      }
      if (key == SDLK_DOWN) {
        cgHover_ = cgHover_ < 0 ? 0 : (cgHover_ + 5) % 20;
        return true;
      }
      if (key == SDLK_PAGEUP) {
        cgPage_ = (cgPage_ + 15) % 16;
        return true;
      }
      if (key == SDLK_PAGEDOWN) {
        cgPage_ = (cgPage_ + 1) % 16;
        return true;
      }
      if (Kotonoha::InputMapper::isConfirmKey(key) && cgHover_ >= 0 &&
          cgHover_ < 20) {
        vm_.setW(1, cgPage_ * 20 + cgHover_);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
    }
    if (mode_ == Mode::Choice && !choices_.empty()) {
      if (key == SDLK_UP) {
        selectedChoice_ =
            (selectedChoice_ + static_cast<int>(choices_.size()) - 1) %
            static_cast<int>(choices_.size());
        return true;
      }
      if (key == SDLK_DOWN) {
        selectedChoice_ =
            (selectedChoice_ + 1) % static_cast<int>(choices_.size());
        return true;
      }
    }
    if (Kotonoha::InputMapper::isConfirmKey(key) ||
        Kotonoha::InputMapper::isSkipHoldKey(key)) {
      advanceInput();
      return true;
    }
  }

  if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      ev.button.button == SDL_BUTTON_RIGHT) {
    if (mode_ == Mode::SaveLoadDialog) {
      mode_ = modeBeforeSaveDialog_;
      if (mode_ == Mode::Running)
        stepVm();
      return true;
    }
    if (mode_ == Mode::CgMode || mode_ == Mode::HeartMode ||
        mode_ == Mode::MapMode) {
      vm_.setW(1, -1);
      mode_ = Mode::Running;
      stepVm();
      return true;
    }
    if (mode_ == Mode::TitleMenu && titlePage_ == 1) {
      vm_.setW(1, 3);
      mode_ = Mode::Running;
      stepVm();
      return true;
    }
  }

  if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      ev.button.button == SDL_BUTTON_LEFT) {
    const float mx = ev.button.x;
    const float my = ev.button.y;

    if (mode_ == Mode::TitleMenu) {
      const int count = (titlePage_ == 0) ? 3 : 4;
      for (int i = 0; i < count; ++i) {
        const int bitIdx = (titlePage_ == 0) ? (90 + i) : (93 + i);
        if (!vm_.getBit(bitIdx))
          continue;
        const SDL_Point pos =
            (titlePage_ == 0) ? kTitlePage0Pos[i] : kTitlePage1Pos[i];
        if (pointInRect(mx, my, static_cast<float>(pos.x),
                        static_cast<float>(pos.y), 220.0f, 40.0f)) {
          vm_.setW(1, i);
          mode_ = Mode::Running;
          stepVm();
          return true;
        }
      }
      return true;
    }

    if (mode_ == Mode::MapMode) {
      for (int i = 0; i < 11; ++i) {
        if (pointInRect(mx, my, static_cast<float>(kMapLocPos[i].x),
                        static_cast<float>(kMapLocPos[i].y), 110.0f, 34.0f)) {
          vm_.setW(1, i);
          mode_ = Mode::Running;
          stepVm();
          return true;
        }
      }
      if (pointInRect(mx, my, 520.0f, 515.0f, 100.0f, 38.0f)) {
        vm_.setW(1, 30);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
      if (pointInRect(mx, my, 635.0f, 515.0f, 100.0f, 38.0f)) {
        vm_.setW(1, -1);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
      return true;
    }

    if (mode_ == Mode::CgMode) {
      if (pointInRect(mx, my, 32.0f, 487.0f, 120.0f, 38.0f)) {
        cgPage_ = (cgPage_ + 15) % 16;
        return true;
      }
      if (pointInRect(mx, my, 186.0f, 487.0f, 120.0f, 38.0f)) {
        cgPage_ = (cgPage_ + 1) % 16;
        return true;
      }
      if (pointInRect(mx, my, 653.0f, 487.0f, 115.0f, 38.0f)) {
        vm_.setW(1, -1);
        mode_ = Mode::Running;
        stepVm();
        return true;
      }
      for (int i = 0; i < 20; ++i) {
        const int col = i % 5;
        const int row = i / 5;
        if (pointInRect(mx, my, static_cast<float>(24 + col * 152),
                        static_cast<float>(26 + row * 114), 150.0f, 112.0f)) {
          vm_.setW(1, cgPage_ * 20 + i);
          mode_ = Mode::Running;
          stepVm();
          return true;
        }
      }
      return true;
    }

    if (mode_ == Mode::SaveLoadDialog) {
      for (int i = 0; i < 20; ++i) {
        const int col = i / 10;
        const int row = i % 10;
        const float sx = 90.0f + static_cast<float>(col) * 320.0f;
        const float sy = 96.0f + static_cast<float>(row) * 42.0f;
        if (pointInRect(mx, my, sx, sy, 290.0f, 36.0f)) {
          if (saveDialogIsLoad_) {
            if (loadSlot(i))
              return true;
          } else {
            saveSlot(i);
            mode_ = modeBeforeSaveDialog_;
            return true;
          }
        }
      }
      return true;
    }

    advanceInput();
    return true;
  }

  return false;
}

std::string LargePonPonRuntime::debugNode() const {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s@0x%zx", vm_.currentScriptName().c_str(),
                vm_.pc());
  return buf;
}

} // namespace Kotonoha::PmailPort
