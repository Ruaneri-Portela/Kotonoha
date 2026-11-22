// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Ponscripter {

struct Resolution {
  int width = 640;
  int height = 480;
};

enum class TransMode : std::uint8_t {
  Alpha = 0,   // :a (native PNG alpha if RGBA/GrayA; else right-half inverted mask)
  TopLeft,     // :l (color-key pixel (0,0), preserving PNG alpha if present)
  TopRight,    // :r (color-key pixel (w-1,0))
  Copy,        // :c (opaque copy)
  String,      // :s (proportional text sprite)
  MaskFile,    // :m;mask;img
  DirectColor, // #RRGGBB, black, white
  None         // empty / clear
};

struct TaggedImageSpec {
  TransMode trans = TransMode::None;
  std::string rawTag;
  std::string imagePath;
  std::string maskPath;
  int numCells = 1;
  int durationMs = 0;
  int loopMode = 0; // 0=loop, 1=once, 2=pingpong, 3=manual button cell

  // For TransMode::String (:s) and TransMode::DirectColor
  int fontWidth = 0;
  int fontHeight = 0;
  int fontSpacing = 0;
  std::uint32_t color = 0xFFFFFFFFu;      // 0xRRGGBBAA or 0x00RRGGBB (RGB24)
  std::uint32_t hoverColor = 0xFFFFFFFFu; // 2nd #RRGGBB if specified
  bool hasHoverColor = false;
  std::string text;
};

// Parses #RRGGBB (with or without leading '#') into 0xRRGGBB. Returns false if invalid.
bool parseHexColor24(std::string_view text, std::uint32_t &outRgb24);

// Binary-verified PONscripter parseTaggedString grammar.
TaggedImageSpec parseTaggedString(std::string_view raw,
                                  TransMode defaultTrans = TransMode::Copy);

struct EffectDef {
  int id = 0;
  int builtinEffect = 10; // 0/1=instant, 2..9=shutter/curtain, 10=crossfade, 15/18=mask
  int durationMs = 0;
  std::string maskPath;
};

struct ActiveEffect {
  bool active = false;
  int builtinEffect = 0;
  int durationMs = 0;
  double elapsedMs = 0.0;
  std::string maskPath;
};

struct SpriteSlot {
  bool loaded = false;
  bool visible = false;
  bool secondaryLayer = false; // lsp2/lsph2 separate hide-state group
  int blendMode = 0; // 0=alpha, 1=add, 2=reverse subtract
  TaggedImageSpec spec;
  int x = 0;
  int y = 0;
  int alpha = 255;
  int cellWidth = 0;
  int cellHeight = 0;
  int currentCell = 0;
  int animDir = 1;
  double animAccumMs = 0.0;
};

// Retained manual drawing stream for drawclear/drawfill/drawbg/drawsp/
// drawtext and draw. The VM captures immutable draw inputs so render frames
// never execute script commands or mutate the layer stack.
struct ManualDrawOp {
  enum class Kind : std::uint8_t {
    Clear, Fill, Background, Sprite, Text, Blit, Rect, Window
  };
  Kind kind = Kind::Clear;
  std::uint32_t colorRgb = 0;
  TaggedImageSpec spec;
  std::string text;
  int x = 0, y = 0;
  int width = 0, height = 0;
  int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
  int cell = 0;
  int alpha = 255;
  int scaleX = 100, scaleY = 100;
  int rotation = 0;
  int transformA = 1000, transformB = 0;
  int transformC = 0, transformD = 1000;
  bool affine = false;
};

struct StandingSlot {
  bool loaded = false;
  TaggedImageSpec spec;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int alpha = 255;
};

struct BarGauge {
  bool active = false;
  int value = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int maxValue = 100;
  std::uint32_t colorRgb = 0xFFFFFFu;
};

struct PrintedNumber {
  bool active = false;
  int value = 0;
  int x = 0;
  int y = 0;
  int fontWidth = 16;
  int fontHeight = 16;
  std::uint32_t colorRgb = 0xFFFFFFu;
};

struct ButtonDef {
  enum class Kind : std::uint8_t { RectBtn = 0, SpriteBtn = 1, TextBtn = 2 };
  int id = 0;
  Kind kind = Kind::SpriteBtn;
  int spriteNo = -1;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int srcX = 0;
  int srcY = 0;
  std::string exbtnControl;
  std::string text; // UI-rendered custom-selection caption
};

struct RMenuEntry {
  std::string label;
  std::string command; // save, load, lookback, skip, windowerase, reset, end, "" (spacer)
};

struct SelectOption {
  std::string text;
  std::string targetLabel;
};

struct CursorSpec {
  bool configured = false;
  TaggedImageSpec spec;
  int offsetX = 0;
  int offsetY = 0;
  bool absolute = false;
  int currentCell = 0;
  int animDir = 1;
  double animAccumMs = 0.0;
};

struct WindowSpec {
  int textX = 20;
  int textY = 20;
  int cols = 30;
  int rows = 16;
  int fontW = 20;
  int fontH = 20;
  int pitchX = 0;
  int pitchY = 4;
  int textSpeed = 20;
  bool bold = false;
  bool shadow = true;
  TaggedImageSpec backdrop;
  int winX = 0;
  int winY = 0;
  int winW = 0;
  int winH = 0;

  int textAreaWidth() const { return (fontW + pitchX) * cols; }
  int textAreaHeight() const { return (fontH + pitchY) * rows; }
};

struct TextStyle {
  int fontSlot = 0;
  bool italic = false;
  bool bold = false;
  bool underline = false;
  int fontSizePx = 0; // 0 = use WindowSpec::fontH
  std::uint32_t colorRgb = 0xFFFFFFu;
  int offsetX = 0;
  int offsetY = 0;
};

struct TextSpan {
  std::string text;      // UTF-8 text run (or "\n" for explicit line break)
  std::string lineKey;   // Original line key for Kotonoha live translation
  TextStyle style;
  bool isLineBreak = false;
};

enum class WaitState : std::uint8_t {
  Running = 0,
  WaitClickInline, // '@'
  WaitClickPage,   // '\'
  WaitClickBare,   // 'click'
  WaitTimer,       // 'wait' / 'delay' / inline '!w' / '!d'
  WaitEffect,      // 'print' / 'bg' / 'ld' / 'cl' timed transition
  WaitButton,      // 'btnwait' / 'btnwait2'
  WaitSelect,      // 'select'
  WaitSystemCall,  // 'systemcall save|load|rmenu|lookback|windowerase'
  WaitVideo,       // 'mpegplay' / 'avi'
  Halted           // 'end'
};

struct SaveTimestamp {
  bool exists = false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  std::string title;
};

class HostCallbacks {
public:
  virtual ~HostCallbacks() = default;

  // Query image pixel dimensions (raw image size before cell splitting).
  // Returns true if asset exists and dimensions were populated.
  virtual bool queryImageSize(const std::string &path, int &outW, int &outH,
                              bool &outHasAlpha) {
    (void)path;
    outW = 0;
    outH = 0;
    outHasAlpha = true;
    return false;
  }

  // Scene & transition hooks
  virtual void onSceneDirty() {}
  virtual void onBeginEffect(const ActiveEffect &effect) { (void)effect; }
  virtual void onFinishEffect() {}

  // Audio & video hooks
  virtual void onPlayBgm(const std::string &path, bool loop, int fadeInMs = 0) {
    (void)path;
    (void)loop;
    (void)fadeInMs;
  }
  virtual void onStopBgm(int fadeOutMs = 0) { (void)fadeOutMs; }
  virtual void onPlayWave(int channel, const std::string &path, bool loop) {
    (void)channel;
    (void)path;
    (void)loop;
  }
  virtual void onStopWave(int channel) { (void)channel; } // -1 = all channels
  virtual void onVolumeChanged(int bgmVol, int seVol, int voiceVol) {
    (void)bgmVol;
    (void)seVol;
    (void)voiceVol;
  }
  virtual void onChannelVolumeChanged(int channel, int vol) {
    (void)channel;
    (void)vol;
  }
  virtual void onPlayVideo(const std::string &path, bool canCancel) {
    (void)path;
    (void)canCancel;
  }

  // Windowing & caption hooks
  virtual void onCaptionChanged(const std::string &caption) { (void)caption; }
  virtual void onFullscreenChanged(bool fullscreen) { (void)fullscreen; }
  virtual bool queryFullscreen() const { return false; }

  // Persistence hooks
  virtual void onPersistGlobals() {}
  virtual SaveTimestamp querySaveSlot(int slot) const {
    (void)slot;
    return {};
  }
  virtual bool onSaveSlot(int slot, const std::string &customTitle) {
    (void)slot;
    (void)customTitle;
    return false;
  }
  virtual bool onLoadSlot(int slot) {
    (void)slot;
    return false;
  }
  virtual void onSaveScreenshot(const std::string &path) { (void)path; }
  virtual void onSystemCall(const std::string &kind) { (void)kind; }
};

} // namespace Ponscripter
