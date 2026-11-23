#include "RealLiveRuntime.hpp"
#include "RealLiveAudioFade.hpp"
#include "RealLiveAudioWait.hpp"
#include "RealLiveBgmCompat.hpp"
#include "RealLiveChannelLevels.hpp"
#include "RealLiveInput.hpp"
#include "RealLiveMouseCursor.hpp"
#include "RealLiveNumeric.hpp"
#include "RealLivePersistentHistory.hpp"
#include "RealLiveReadProgress.hpp"
#include "RealLiveSelectRules.hpp"
#include "RealLiveSelection.hpp"
#include "RealLiveTransition.hpp"
#include "SceneTexture.hpp"
#include "TitleInput.hpp"
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/ImageButtonBar.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/TransformTween.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/SurfaceBlend.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <RealLive/Images.hpp>
#include <RealLive/Session.hpp>
#include <RealLive/System.hpp>
#include <RealLive/Voice.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <vector>
namespace RealLive {
using namespace Kotonoha;
namespace {
// Keep the composed stage active while the separate presentation scenes are
// introduced; bind() still supplies its drawing and input callbacks.
struct Stage final : Scene {
  std::function<void(RendererRef)> draw;
  std::function<bool(EventRef)> input;
  explicit Stage(Engine &engine) : Scene(engine) {}
  void render(RendererRef renderer, float) override {
    if (draw)
      draw(renderer);
  }
  bool onEvent(EventRef event) override { return input && input(event); }
};
namespace RealLive {
struct Impl;
} // namespace RealLive

namespace {
struct RealLiveBackgroundScene final : Scene {
  RealLive::Impl &impl;
  RealLiveBackgroundScene(Engine &e, RealLive::Impl &impl)
      : Scene(e), impl(impl) {}
  // void render(RendererRef r, float scale) override;
  // bool handleEvent(EventRef e) override;
  bool designSpace() const override { return false; }
};

struct RealLiveMovieScene final : Scene {
  RealLive::Impl &impl;
  RealLiveMovieScene(Engine &e, RealLive::Impl &impl) : Scene(e), impl(impl) {}
  // void render(RendererRef r, float scale) override;
  // bool handleEvent(EventRef e) override { return false; }
  bool designSpace() const override { return false; }
};

struct RealLiveEditFieldScene final : Scene {
  RealLive::Impl &impl;
  RealLiveEditFieldScene(Engine &e, RealLive::Impl &impl)
      : Scene(e), impl(impl) {}
  // void render(RendererRef r, float scale) override;
  // bool handleEvent(EventRef e) override;
  bool designSpace() const override { return false; }
};

struct RealLiveErrorScene final : Scene {
  RealLive::Impl &impl;
  RealLiveErrorScene(Engine &e, RealLive::Impl &impl) : Scene(e), impl(impl) {}
  // void render(RendererRef r, float scale) override;
  bool handleEvent(EventRef e) override { return false; }
  bool designSpace() const override { return false; }
};

void processSpeakerAndDialogue(const std::string &rawSpeaker,
                               const std::string &rawDialogue,
                               std::string &outSpeaker,
                               std::string &outDialogue) {
  outSpeaker = rawSpeaker;
  outDialogue = rawDialogue;

  if (outDialogue.empty())
    return;

  if (!outSpeaker.empty()) {
    if (outDialogue.rfind(outSpeaker, 0) == 0) {
      std::size_t pos = outSpeaker.size();
      while (pos < outDialogue.size() &&
             (outDialogue[pos] == ' ' || outDialogue[pos] == ':'))
        ++pos;
      if (pos < outDialogue.size() &&
          (outDialogue[pos] == '"' || outDialogue[pos] == '\'' ||
           outDialogue[pos] == ' ' || outDialogue[pos] == ':')) {
        outDialogue = outDialogue.substr(pos);
      }
    }
  }

  if (outSpeaker.empty()) {
    std::size_t quotePos = outDialogue.find_first_of("\"'「");
    std::size_t colonPos = outDialogue.find(": ");
    std::size_t sepPos = std::string::npos;
    if (quotePos != std::string::npos && quotePos <= 30)
      sepPos = quotePos;
    if (colonPos != std::string::npos && colonPos < sepPos && colonPos <= 30)
      sepPos = colonPos;

    if (sepPos != std::string::npos && sepPos > 0) {
      std::string cand = outDialogue.substr(0, sepPos);
      while (!cand.empty() && cand.back() == ' ')
        cand.pop_back();
      if (!cand.empty()) {
        outSpeaker = cand;
        std::size_t startPos = sepPos;
        if (outDialogue[sepPos] == ':') {
          ++startPos;
          while (startPos < outDialogue.size() && outDialogue[startPos] == ' ')
            ++startPos;
        }
        outDialogue = outDialogue.substr(startPos);
      }
    }
  }
}
} // namespace

struct Text : VNTextBox {
  Kotonoha::Providers::RealLiveAssets &assets;
  SurfacePtr frame, back, cursor, buttonSurface;
  Image buttonImage;
  ImageButtonBar controls;
  bool concealed = false;
  std::function<bool()> autoActive, skipActive;
  std::map<SDL_Surface *, TexturePtr> imageTextures;
  std::vector<Uint8> fontData;
  Image cursorImage;
  int cursorSizeW = 16, cursorSizeH = 16, cursorCont = 0, cursorSpeed = 850;
  int activeWindow = 0;
  bool hasLocate = false;
  int locateX = 0, locateY = 0;
  FontPtr font;
  int canvasW = 640, canvasH = 480;

  std::string windowKey(const char *suffix) const {
    std::ostringstream key;
    key << "WINDOW." << std::setw(3) << std::setfill('0')
        << std::max(0, activeWindow) << '.' << suffix;
    return key.str();
  }
  void setWindow(int w) {
    activeWindow = std::max(0, w);
    hasLocate = false;
    locateX = locateY = 0;
  }
  void setLocate(int x, int y) {
    hasLocate = true;
    locateX = x;
    locateY = y;
  }
  void clearLocate() {
    hasLocate = false;
    locateX = locateY = 0;
  }

  int boxWidth() const {
    if (frame && frame->w > 0)
      return frame->w;
    return canvasW;
  }
  int boxHeight() const {
    if (frame && frame->h > 0) {
      // AIR's original G00 frame may be a full-screen 640x480 canvas:
      // only the lower 122px are the actual dialogue box. Using its
      // full height as the box turns a window decoration into a scene overlay.
      if (frame->h >= canvasH / 2)
        return std::min(122, frame->h);
      return frame->h;
    }
    return 122;
  }
  int boxX() const {
    const std::string key = windowKey("POS");
    const auto &pos = assets.settings().get(key);
    const std::string &effKey =
        pos.size() >= 3 ? key : std::string("WINDOW.000.POS");
    const auto &effPos = assets.settings().get(effKey);
    if (effPos.size() >= 3) {
      const int mod = assets.settings().integer(effKey, 0, 0);
      const int px = assets.settings().integer(effKey, 0, 1);
      return (mod == 1 || mod == 3) ? (canvasW - boxWidth() - px) : px;
    }
    return (canvasW - boxWidth()) / 2;
  }
  int boxY() const {
    const std::string key = windowKey("POS");
    const auto &pos = assets.settings().get(key);
    const std::string &effKey =
        pos.size() >= 3 ? key : std::string("WINDOW.000.POS");
    const auto &effPos = assets.settings().get(effKey);
    if (effPos.size() >= 3) {
      const int mod = assets.settings().integer(effKey, 0, 0);
      const int py = assets.settings().integer(effKey, 0, 2);
      return (mod == 2 || mod == 3) ? (canvasH - boxHeight() - py) : py;
    }
    return canvasH - boxHeight();
  }
  float scaleX() const { return float(boxWidth()) / 640.0f; }
  float scaleY() const { return float(boxHeight()) / 122.0f; }
  int mojiTop() const {
    return assets.settings().integer("WINDOW.000.MOJI_POS", 19, 0);
  }
  int mojiLeft() const {
    return std::max(20,
                    assets.settings().integer("WINDOW.000.MOJI_POS", 53, 2));
  }
  float wrapWidth() const {
    return float(boxWidth()) - float(mojiLeft()) - 59.0f * scaleX();
  }

  void updateLayout(int w, int h) {
    canvasW = w;
    canvasH = h;
    const float bx = float(boxX()), by = float(boxY());
    const float bw = float(boxWidth()), bh = float(boxHeight());

    if (controls.buttons.size() >= 5) {
      const int wakuSet =
          std::max(0, assets.settings().integer("WINDOW.000.WAKU_SETNO", 0));
      const int wakuNo =
          std::max(0, assets.settings().integer("WINDOW.000.WAKU_NO", 0));
      std::ostringstream prefix;
      prefix << "WAKU." << std::setw(3) << std::setfill('0') << wakuSet << "."
             << std::setw(3) << std::setfill('0') << wakuNo;
      const std::string wakuBase = prefix.str();

      struct BtnSpec {
        const char *useKey;
        const char *boxSuffix;
        int defX, defY, defW, defH;
      };
      static constexpr BtnSpec specs[5] = {
          {"WINDOW.000.CLEAR_USE", ".CLEAR_BOX", 584, 4, 31, 7},
          {"WINDOW.000.MSGBKLEFT_USE", ".MSGBKLEFT_BOX", 616, 4, 12, 38},
          {"WINDOW.000.MSGBKRIGHT_USE", ".MSGBKRIGHT_BOX", 616, 43, 12, 38},
          {"WINDOW.000.AUTOMODE_USE", ".AUTOMODE_BOX", 616, 82, 12, 16},
          {"WINDOW.000.READJUMP_USE", ".READJUMP_BOX", 616, 99, 12, 16},
      };
      for (std::size_t i = 0; i < 5; ++i) {
        if (assets.settings().integer(specs[i].useKey, 1) == 0) {
          controls.buttons[i].area = {0, 0, 0, 0};
          continue;
        }
        const std::string boxKey = wakuBase + specs[i].boxSuffix;
        const auto &tokens = assets.settings().get(boxKey);
        if (tokens.size() >= 5) {
          const int mod = assets.settings().integer(boxKey, 0, 0);
          const int rx = assets.settings().integer(boxKey, 0, 1);
          const int ry = assets.settings().integer(boxKey, 0, 2);
          const int rw = assets.settings().integer(boxKey, 0, 3);
          const int rh = assets.settings().integer(boxKey, 0, 4);
          if (rw <= 0 || rh <= 0) {
            controls.buttons[i].area = {0, 0, 0, 0};
          } else {
            const float x =
                (mod == 1 || mod == 3) ? (bx + bw - rw - rx) : (bx + rx);
            const float y =
                (mod == 2 || mod == 3) ? (by + bh - rh - ry) : (by + ry);
            controls.buttons[i].area = {x, y, float(rw), float(rh)};
          }
        } else {
          controls.buttons[i].area = {
              bx + float(specs[i].defX), by + float(specs[i].defY),
              float(specs[i].defW), float(specs[i].defH)};
        }
      }
    }
  }

  Text(Engine &e, Kotonoha::Providers::RealLiveAssets &a, int w = 640,
       int h = 480)
      : VNTextBox(e, "", "", 40), assets(a), canvasW(w), canvasH(h) {
    hide();
    const int wakuSet =
        std::max(0, assets.settings().integer("WINDOW.000.WAKU_SETNO", 0));
    const int wakuNo =
        std::max(0, assets.settings().integer("WINDOW.000.WAKU_NO", 0));
    std::ostringstream prefix;
    prefix << "WAKU." << std::setw(3) << std::setfill('0') << wakuSet << "."
           << std::setw(3) << std::setfill('0') << wakuNo;
    const std::string wakuBase = prefix.str();

    const std::string btnName =
        assets.settings().text(wakuBase + ".BTN", "smw00c");
    if (!btnName.empty()) {
      auto smw00c = assets.graphic(btnName, false);
      if (!smw00c.empty()) {
        auto bytes = assets.read(smw00c);
        if (bytes.size() >= 5)
          buttonImage = decodeG00(bytes);
        buttonSurface = assets.loadSurface(smw00c);
      }
    }
    const std::string curName =
        assets.settings().text("CURSOR.000.NAME", "kcur00");
    cursorSizeW =
        std::max(1, assets.settings().integer("CURSOR.000.SIZE", 16, 0));
    cursorSizeH =
        std::max(1, assets.settings().integer("CURSOR.000.SIZE", 16, 1));
    cursorCont =
        std::max(0, assets.settings().integer("CURSOR.000.CONT", 0, 0));
    cursorSpeed =
        std::max(100, assets.settings().integer("CURSOR.000.SPEED", 850, 0));
    if (!curName.empty()) {
      auto kcur00 = assets.graphic(curName, false);
      if (!kcur00.empty()) {
        auto bytes = assets.read(kcur00);
        if (bytes.size() >= 5) {
          try {
            cursorImage = decodeG00(bytes);
          } catch (...) {
          }
        }
        cursor = assets.loadSurface(kcur00);
      }
    }
    const std::string frameName =
        assets.settings().text(wakuBase + ".NAME", "smw00a");
    if (!frameName.empty()) {
      auto smw00a = assets.graphic(frameName, false);
      if (!smw00a.empty()) {
        frame = assets.loadSurface(smw00a);
      }
    }
    // Before CLANNAD, AIR composed two separate G00s: smw00b provides the
    // semitransparent blue backing and smw00a provides the window frame.
    // Keep this AIR-specific backdrop separate from Clannad window artwork.
    if (frameName == "smw00a") {
      const auto backgroundName = assets.graphic("smw00b", false);
      if (!backgroundName.empty()) {
        back = assets.loadSurface(backgroundName);
        if (back && back->pixels && back->w > 0 && back->h > 0) {
          if (back->format != SDL_PIXELFORMAT_RGBA32) {
            SurfacePtr rgba(
                SDL_ConvertSurface(back.get(), SDL_PIXELFORMAT_RGBA32));
            if (rgba)
              back = std::move(rgba);
          }
          if (back->format == SDL_PIXELFORMAT_RGBA32) {
            for (int y = 0; y < back->h; ++y)
              for (int x = 0; x < back->w; ++x) {
                auto *pixel = static_cast<Uint8 *>(back->pixels) +
                              y * back->pitch + x * 4;
                pixel[0] = 100;
                pixel[1] = 100;
                pixel[2] = 160;
                pixel[3] = Uint8(int(pixel[3]) * 200 / 255);
              }
          }
        }
      }
    }
    const char *path = "/System/Library/Fonts/Supplemental/Verdana.ttf";
    std::error_code fontEc;
    if (std::filesystem::exists(path, fontEc) && !fontEc) {
      font.reset(TTF_OpenFont(path, 25));
      if (font)
        TTF_SetFontLineSkip(font.get(), 28);
    }
    if (!font) {
      fontData = FileResolver::loadFontMemory(e.GetSettings().fontPath);
      if (!fontData.empty()) {
        IOStreamPtr stream(
            SDL_IOFromConstMem(fontData.data(), fontData.size()));
        if (stream)
          font.reset(TTF_OpenFontIO(stream.release(), true, 25));
      }
    }
    if (font)
      TTF_SetFontLineSkip(font.get(), 28);
  }
  GlossCursor textCursor(const std::string &value) const {
    GlossCursor p;
    const float sx = scaleX(), sy = scaleY();
    const float wrapWidth = (float(boxWidth()) - 112.0f) * sx;
    auto width = [&](const std::string &v) {
      int w = 0, h = 0;
      if (font)
        TTF_GetStringSize(font.get(), v.data(), v.size(), &w, &h);
      else
        w = int(v.size()) * 13;
      return static_cast<float>(w);
    };
    const auto lines =
        Kotonoha::VNTextBoxUtility::WrapText(value, width, wrapWidth);
    if (!lines.empty()) {
      p.y = float(boxY()) + static_cast<float>((lines.size() - 1) * 28.0f * sy);
      p.x = float(boxX()) + 53.0f * sx + width(lines.back());
    } else {
      p.x = float(boxX()) + 53.0f * sx;
      p.y = float(boxY());
    }
    return p;
  }
  int wrappedLineCount(const std::string &value) const {
    if (value.empty())
      return 0;
    const float sx = scaleX();
    const float wrapWidth = (float(boxWidth()) - 112.0f) * sx;
    auto width = [&](const std::string &v) {
      int w = 0, h = 0;
      if (!engine.GetSettings().vnFontPath.empty())
        return engine.GetVNUI().measureSized(v, 10000, 25).x;
      if (font)
        TTF_GetStringSize(font.get(), v.data(), v.size(), &w, &h);
      else
        w = int(v.size()) * 13;
      return static_cast<float>(w);
    };
    return int(
        Kotonoha::VNTextBoxUtility::WrapText(value, width, wrapWidth).size());
  }
  bool designSpace() const override { return false; }
  void image(RendererRef r, SurfaceRef s, float x, float y,
             bool upscale = true) {
    auto &t = imageTextures[&s];
    if (!t) {
      t.reset(SDL_CreateTextureFromSurface(&r, &s));
      if (auto *pipeline = Upscaling::forRenderer(r))
        pipeline->process(*t, 0);
    }
    SDL_FRect dest{x, y, float(s.w), float(s.h)};
    if (upscale)
      RenderUpscaledTexture(&r, t.get(), nullptr, &dest);
    else
      SDL_RenderTexture(&r, t.get(), nullptr, &dest);
  }
  void print(RendererRef r, const std::string &s, float x, float y,
             int wrap = 528, bool speaker = false) {
    if (s.empty())
      return;
    if (!engine.GetSettings().vnFontPath.empty()) {
      const auto color = engine.GetSettings().vnTextColorOverride
                             ? (speaker ? engine.GetSettings().vnSpeakerColor
                                        : engine.GetSettings().vnTextColor)
                             : SDL_Color{255, 255, 255, 255};
      engine.GetVNUI().textSized(r, s, x + 1, y + 1, wrap, 25, {0, 0, 0, 255});
      engine.GetVNUI().textSized(r, s, x, y, wrap, 25, color);
      return;
    }
    if (!font) {
      engine.GetVNUI().text(r, s, x, y, wrap);
      return;
    }
    const float density = UI::rasterDensity(r);
    TTF_SetFontSize(font.get(), 25 * density);
    struct RestoreFont {
      TTF_Font *font;
      ~RestoreFont() { TTF_SetFontSize(font, 25); }
    } restore{font.get()};
    SurfacePtr shadow(TTF_RenderText_Blended_Wrapped(
        font.get(), s.data(), s.size(), {0, 0, 0, 255},
        int(std::ceil(wrap * density))));
    SurfacePtr text(TTF_RenderText_Blended_Wrapped(
        font.get(), s.data(), s.size(),
        engine.GetSettings().vnTextColorOverride
            ? (speaker ? engine.GetSettings().vnSpeakerColor
                       : engine.GetSettings().vnTextColor)
            : SDL_Color{255, 255, 255, 255},
        int(std::ceil(wrap * density))));
    if (shadow) {
      MarkTTFTextRaster(*shadow);
      drawRaster(r, *shadow, x + 1, y + 1, density);
    }
    if (text) {
      MarkTTFTextRaster(*text);
      drawRaster(r, *text, x, y, density);
    }
  }
  void drawRaster(RendererRef r, SurfaceRef surface, float x, float y,
                  float density) {
    TexturePtr texture(SDL_CreateTextureFromSurface(&r, &surface));
    if (!texture)
      throw std::runtime_error(SDL_GetError());
    MarkTTFTextRaster(*texture);
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    SDL_FRect destination{x, y, surface.w / density, surface.h / density};
    SDL_RenderTexture(&r, texture.get(), nullptr, &destination);
  }
  void update() override {
    if (concealed)
      clock.reset();
    else
      VNTextBox::update();
  }
  void render(RendererRef r, float scale) override {
    if (!concealed)
      VNTextBox::render(r, scale);
  }
  bool handleEvent(EventRef event) override {
    if (!isVisible() || concealed)
      return false;
    updateLayout(canvasW, canvasH);
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
      return false;
    return controls.handle(event);
  }
  void renderFrame(RendererRef r) override {
    updateLayout(canvasW, canvasH);
    const float bx = float(boxX()), by = float(boxY());
    // Draw the original translucent AIR backing, then the transparent G00
    // border. Neither image must be forced opaque or cropped arbitrarily.
    if (back)
      image(r, *back, bx, by);
    if (frame)
      image(r, *frame, bx, by);
    if (!buttonSurface)
      return;
    auto &texture = imageTextures[buttonSurface.get()];
    if (!texture)
      texture.reset(SDL_CreateTextureFromSurface(&r, buttonSurface.get()));
    constexpr int groups[]{8, 24, 32, 112, 104};
    for (std::size_t i = 0; i < controls.buttons.size(); ++i) {
      const bool active = (i == 3 && autoActive && autoActive()) ||
                          (i == 4 && skipActive && skipActive());
      // The native SMW00 button atlas stores five states per control:
      // normal, hover, pressed, hover-active and the red active toggle.
      // Region 116/108 (group + 4) is the red "auto/skip on" frame; +3 is
      // only the unfilled active frame and made enabled toggles look idle.
      const int index =
          groups[i] +
          (active && controls.state(int(i)) == 0 ? 4 : controls.state(int(i)));
      if (index < int(buttonImage.regions.size())) {
        const auto &region = buttonImage.regions.at(index);
        SDL_FRect source{float(region.atlasX), float(region.atlasY),
                         float(region.x2 - region.x1 + 1),
                         float(region.y2 - region.y1 + 1)};
        RenderUpscaledTexture(&r, texture.get(), &source,
                              &controls.buttons[i].area);
      }
    }
  }
  void renderSpeaker(RendererRef r) override {
    const float sx = scaleX(), sy = scaleY();
    print(r, speakerName, float(boxX()) + 33.0f * sx,
          float(boxY()) - 30.0f * sy, int(500 * sx), true);
  }
  void renderDialogue(RendererRef r) override {
    const float sx = scaleX(), sy = scaleY();
    const float wrapWidth = std::max(64.0f, float(boxWidth()) - 112.0f * sx);
    auto measure = [this](const std::string &text) {
      int w = 0, h = 0;
      if (!engine.GetSettings().vnFontPath.empty())
        return engine.GetVNUI().measureSized(text, 10000, 25).x;
      if (font)
        TTF_GetStringSize(font.get(), text.data(), text.size(), &w, &h);
      return float(w);
    };
    const float baseX =
        float(boxX()) + (hasLocate ? float(locateX) : 53.0f * sx);
    const float baseY =
        float(boxY()) + (hasLocate ? float(locateY) : 19.0f * sy);
    const auto lines = VNTextBoxUtility::RevealLines(
        fullDialogue, visibleDialogue.size(), measure, wrapWidth);
    for (std::size_t n = 0; n < lines.size(); ++n)
      print(r, lines[n], baseX, baseY + n * 28.0f * sy, int(wrapWidth));
  }
  void renderIndicator(RendererRef r) override {
    if (!cursor)
      return;
    auto &t = imageTextures[cursor.get()];
    if (!t) {
      t.reset(SDL_CreateTextureFromSurface(&r, cursor.get()));
      if (auto *pipeline = Upscaling::forRenderer(r))
        pipeline->process(*t, 0);
    }
    const int speed = std::max(100, cursorSpeed);
    SDL_FRect src{0, 0, float(cursor->w), float(cursor->h)};
    // A G00/PDT cursor with a single region spanning the whole surface is
    // still an animation strip: RealLive stores every frame side by side and
    // uses CURSOR.000.CONT/SIZE to select one. Treating that sole region as a
    // single frame drew the entire texture map in the textbox corner.
    const bool singleFullRegion =
        cursorImage.regions.size() == 1 &&
        cursorImage.regions[0].x1 == 0 && cursorImage.regions[0].y1 == 0 &&
        cursorImage.regions[0].x2 + 1 >= int(cursorImage.width) &&
        cursorImage.regions[0].y2 + 1 >= int(cursorImage.height);
    if (!cursorImage.regions.empty() && !singleFullRegion) {
      const std::size_t frameCount = cursorImage.regions.size();
      const std::size_t frameIdx =
          (std::size_t(SDL_GetTicks() % Uint64(speed)) * frameCount) /
          std::size_t(speed);
      const auto &reg = cursorImage.regions[frameIdx % frameCount];
      src = {float(reg.atlasX), float(reg.atlasY),
             float(std::max(1, reg.x2 - reg.x1 + 1)),
             float(std::max(1, reg.y2 - reg.y1 + 1))};
    } else {
      const int cw = std::min(cursor->w, std::max(1, cursorSizeW));
      const int ch = std::min(cursor->h, std::max(1, cursorSizeH));
      const bool horizStrip = cursor->w >= cw * 2 && cursor->h <= ch * 2;
      const bool vertStrip = cursor->h >= ch * 2 && cursor->w <= cw * 2;
      int frameCount = cursorCont > 0 ? cursorCont
                       : horizStrip   ? (cursor->w / cw)
                       : vertStrip    ? (cursor->h / ch)
                                      : 1;
      frameCount = std::max(1, frameCount);
      const int frameIdx = int((SDL_GetTicks() % Uint64(speed)) *
                               Uint64(frameCount) / Uint64(speed)) %
                           frameCount;
      if (horizStrip && (frameIdx + 1) * cw <= cursor->w)
        src = {float(frameIdx * cw), 0.0f, float(cw), float(ch)};
      else if (vertStrip && (frameIdx + 1) * ch <= cursor->h)
        src = {0.0f, float(frameIdx * ch), float(cw), float(ch)};
      else
        src = {0.0f, 0.0f, float(cw), float(ch)};
    }
    const float sx = scaleX(), sy = scaleY();
    float dstX = float(boxX() + boxWidth()) - 49.0f * sx;
    float dstY = float(boxY() + boxHeight()) - 44.0f * sy;
    const std::string keyPosKey = windowKey("KEYCUR_POS");
    const auto &keyPos = assets.settings().get(keyPosKey);
    if (keyPos.size() >= 2) {
      dstX = float(boxX() + assets.settings().integer(keyPosKey, 0, 0));
      dstY = float(boxY() + assets.settings().integer(keyPosKey, 0, 1));
    }
    SDL_FRect dst{dstX, dstY, src.w, src.h};
    RenderUpscaledTexture(&r, t.get(), &src, &dst);
  }
};
struct Object {
  std::string file, text;
  int x = 0, y = 0, pattern = 0, alpha = 255;
  bool visible = false;
  int width = 0, height = 0;
  SDL_Color colour{0, 0, 0, 0};
  bool clipped = false;
  SDL_Rect clip{};
  std::array<SDL_Point, 16> adjustments{};
  int frameMs = 0;
  Uint64 animationElapsed = 0;
  // ANM file objects (objOfFile on an .Anm): unlike sprites they ignore the
  // object position and draw a timed sequence of crops at absolute positions.
  std::shared_ptr<Anm> anm;
  Uint64 anmStart = 0;
  int anmSet = 0;
  bool anmPlaying = false;
  enum class Kind { Sprite, Drift };
  Kind kind = Kind::Sprite;
  // Drift / particle object parameters (objDriftOfFile + objDriftOpts).
  int driftCount = 1;
  bool driftUseAnimation = false;
  int driftStartPattern = 0;
  int driftEndPattern = 0;
  int driftAnimationTime = 0;
  int driftYSpeed = 0;
  int driftPeriod = 0;
  int driftAmplitude = 0;
  bool driftUseDrift = false;
  int driftUnknown = 3;
  int driftDriftSpeed = 0;
  SDL_Rect driftArea{-1, -1, -1, -1};
  // Drift particle instances (lazily spawned, persisted while object lives).
  struct DriftParticle {
    int x = 0, y = 0, alpha = 255;
    Uint64 startTime = 0;
  };
  std::vector<DriftParticle> driftParticles;
  Uint64 driftLastSpawn = 0;
  size_t currentPattern(size_t count) const {
    return frameMs > 0 ? (animationElapsed / Uint64(frameMs)) % count
                       : size_t(std::clamp(pattern, 0, int(count - 1)));
  }
};
struct Graphic {
  struct Raster {
    TexturePtr original, filtered, display;
  };
  Image image;
  SurfacePtr surface;
  std::map<size_t, SurfacePtr> scaledPatterns;
  std::map<std::array<int, 4>, Raster> crops;
};
// Preserve native DC operations for saves and cache each completed raster.
struct DCImage {
  enum class Kind { Pixels, Graphic, Copy, Invert, Gray };
  Kind kind = Kind::Pixels;
  int width = 0, height = 0, alpha = 255;
  bool mask = false;
  SDL_Rect crop{}, destination{};
  std::string graphic;
  SurfacePtr native, raster;
  TexturePtr texture;
  std::shared_ptr<DCImage> source, previous;
  size_t generation = 0;
};
SurfacePtr blank(int w = 640, int h = 480) {
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
    throw std::runtime_error("Invalid REALLIVE surface size");
  SurfacePtr s(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    throw std::runtime_error(SDL_GetError());
  SDL_FillSurfaceRect(s.get(), nullptr,
                      SDL_MapSurfaceRGBA(s.get(), 0, 0, 0, 255));
  return s;
}
} // namespace
struct RealLiveRuntime::Impl {
  RealLiveRuntime &runtime;
  std::shared_ptr<Kotonoha::Providers::RealLiveAssets> assets;
  ScriptArchive archive;
  Session vm;
  VoiceIndex voices;
  AudioConfig audio;
  Engine *engine = nullptr;
  SDL_Cursor *mouseCursor = nullptr;
  int mouseCursorNumber = -1;
  bool mouseCursorApplied = false;
  std::unique_ptr<Stage> stage;
  std::unique_ptr<Text> textbox;
  std::unique_ptr<ChoiceQuestionScene> choice;
  std::unique_ptr<VNSaveMenu> saves;
  struct EditField {
    SDL_FRect area{};
    SDL_Color color{0, 0, 0, 255};
    SDL_Color background{255, 255, 255, 255};
    std::string raw, text;
  };
  std::map<int, EditField> editFields;
  int focusedEdit = -1;
  bool autoMode = false, skipMode = false, skipHeld = false;
  RealLiveInput input;
  Uint64 autoDeadline = 0;
  bool ctrlSkipEnabled = true, currentRead = false;
  std::pair<int, std::size_t> currentPage;
  std::set<std::pair<int, std::size_t>> readPages;
  bool readHistoryLoaded = false;
  bool refreshRequested = false;
  std::string speakerName, musicID, saveName;
  std::map<std::string, std::string> selectionLabels;
  int selectionWindow = 0;
  bool musicLoop = false;
  int64_t musicEnd = 0, musicLoopStart = 0, musicFrames = 0;
  int musicRate = 44100;
  std::unique_ptr<MediaPlayer> movie;
  VideoSkipGesture videoSkip;
  PcmPlayer bgm, voice;
  float bgmGain = 1, fadeFrom = 1, fadeTo = 1;
  int fadeElapsed = 0, fadeDuration = 0;
  bool fadeStops = false;
  bool bgmScriptPaused = false; // independent of gameplay/menu suspension
  // RealLive opcode numbers vary by generation; default to the working
  // Kotonoha table unless the game's config explicitly selects rlvm's table.
  BgmCommandDialect bgmDialect = BgmCommandDialect::KotonohaLegacy;
  AudioWait audioWait;
  ChannelLevels soundLevels, voiceLevels;
  Uint64 audioTick = 0;
  std::map<int, std::unique_ptr<PcmPlayer>> sounds;
  std::map<std::string, Graphic> graphics;
  TexturePtr canvasTexture;
  SurfacePtr canvasPixels;
  TexturePtr compositionTexture, backgroundTexture;
  TexturePtr filteredScene;
  std::string sceneKey;
  std::shared_ptr<DCImage> sceneBackground;
  TexturePtr zoomTexture, transitionTexture;
  TexturePtr ditherTexture, maskedTransitionTexture;
  std::optional<bool> gpuDither;
  int ditherStep = -1, ditherStyle = -1;
  int maxDialogueLines = 3; // #WINDOW.<w>.MOJI_CNT[1], default 3
  int currentDialogueLine = 0;
  SurfacePtr backgroundPixels;
  std::map<std::pair<std::string, int>, TexturePtr> objectTextTextures;
  RenderStats renderStats;
  UpscaleSettings imageUpscaling;
  std::map<int, SurfacePtr> dc;
  std::map<int, std::shared_ptr<DCImage>> dcImages;
  size_t dcGeneration = 1;
  std::map<int, Object> fg, bg;
  std::string dialogue, error, lastGraphic;
  bool pause = false, clearText = true;
  int pauseKind = 17;
  Uint64 deadline = 0;
  float mouseX = -1, mouseY = -1;
  int button1 = 0, button2 = 0;
  std::array<Uint8, 32> syscomEntries{};
  TitleInput titleInput;
  // Set by the native refresh command while the title script is in the menu
  // state. Planetarian and CLANNAD idle at different script indices than the
  // AIR title, so input readiness follows the refresh instead of a hardcoded
  // instruction offset.
  bool titleReady = false;
  int titleScenario = 9032;
  std::vector<int> titleHitObjects;
  std::map<std::pair<int, int>, int> settings;
  std::map<int, Uint64> timers;
  std::vector<std::array<int, 3>> shake;
  Uint64 shakeStart = 0;
  SurfacePtr transitionOld;
  Uint64 transitionStart = 0;
  int transitionMs = 0;
  int transitionStyle = 0;
  int transitionDirection = 0, transitionInterpolation = 0;
  int transitionXSize = 0, transitionYSize = 0;
  int canvasW = 800, canvasH = 600;
  struct ZoomEffect {
    SurfacePtr source;
    std::shared_ptr<DCImage> image;
    SDL_Rect to{}, destinationRect{};
    TransformAnimation animation;
    Uint64 start = 0;
  };
  std::optional<ZoomEffect> zoom;
  SDL_Color flashColour{0, 0, 0, 255};
  Uint64 flashStart = 0;
  int flashDuration = 0;
  struct Frame {
    int from = 0, to = 0, duration = 0;
    Uint64 start = 0;
    bool loop = false;
    double value = 0;
    Uint64 lastCheck = 0;
  };
  std::map<int, Frame> frames;
  Impl(RealLiveRuntime &r,
       std::shared_ptr<Kotonoha::Providers::RealLiveAssets> a, int scene)
      : runtime(r), assets(std::move(a)), archive(assets->scripts()),
        vm(archive,
           scene < 0 ? assets->settings().integer("SEEN_START", 9030) : scene),
        voices(
            assets->list("KOE/"),
            [this](const std::string &id) { return assets->read(id); },
            [this](const std::string &id) { return assets->localPath(id); },
            [this](const std::string &id) { return assets->openStream(id); }),
        audio(parseAudioConfig(assets->settingsSource())) {
    syscomEntries.fill(1);
    if (assets->settings().integer("KOTONOHA_BGM_OPCODE_DIALECT", 0) == 1)
      bgmDialect = BgmCommandDialect::Rlvm;
    mouseCursorNumber = assets->settings().integer("MOUSE_CURSOR", -1);
    // Each RealLive release declares its own front-end in Gameexe.ini.
    titleScenario = assets->settings().integer("SEEN_MENU", 9032);
    // rlBabel Bug #2: Pre-load #WINDOW.<w>.NAME_MOD from Gameexe.ini so the
    // rlBabel name-display mode matches the retail rlBabel.dll default of
    // reading the INI at load time. Without this, babelNameModes_ defaults
    // to 0 for all windows, breaking speaker-name rendering in titles that
    // set NAME_MOD=1 (e.g. AIR_PT-BR #WINDOW.000.NAME_MOD=1).
    {
      std::ostringstream key;
      for (int w = 0; w < 64; ++w) {
        key.str("");
        key << "WINDOW." << std::setw(3) << std::setfill('0') << w
            << ".NAME_MOD";
        int mode = assets->settings().integer(key.str(), -1);
        if (mode >= 0)
          vm.setBabelNameMode(w, mode);
      }
    }
    vm.speaker = [this](std::string t) {
      speakerName = decodeText(t, archive.Scenario(vm.scenario()).encoding());
    };
    vm.resolveName = [this](bool global, int n) {
      return systemName(vm.machine(), global, n);
    };
    for (int n = 0; n < 26; ++n) {
      auto name = assets->settings().text(std::string("NAME.") + char('A' + n));
      vm.machine().memory().strings[{26, n}] = name;
      vm.machine().memory().strings[{27, n}] = name;
    }
    vm.glossCursor = [this] {
      return textbox ? textbox->textCursor(dialogue) : GlossCursor{};
    };
    vm.glyphPosition = [this](const std::string &raw) {
      auto glyph = decodeText(raw, archive.Scenario(vm.scenario()).encoding());
      return textbox ? textbox->textCursor(dialogue + glyph).x - 53 : 0;
    };
    vm.babelControl = [this](int code) {
      if (code == 1 || code == 4 || code == 10 || code == 13)
        currentDialogueLine = 0;
    };
    vm.lineBreak = [this] {
      std::ostringstream key;
      key << "WINDOW." << std::setw(3) << std::setfill('0') << vm.currentWindow
          << ".MOJI_CNT";
      maxDialogueLines =
          std::max(1, assets->settings().integer(key.str(), 3, 1));
      std::string finalSpeaker, finalDialogue;
      processSpeakerAndDialogue(speakerName, dialogue, finalSpeaker,
                                finalDialogue);
      const int wrapped =
          textbox ? std::max(1, textbox->wrappedLineCount(finalDialogue)) : 1;
      const int curLine = std::max(currentDialogueLine, wrapped - 1);
      if (curLine + 1 >= maxDialogueLines) {
        currentDialogueLine = 0;
        return 4;
      }
      currentDialogueLine = curLine + 1;
      return 3;
    };
    vm.pageOverflow = [this](const std::string &rawWord) {
      if (!textbox || dialogue.empty())
        return false;
      std::ostringstream key;
      key << "WINDOW." << std::setw(3) << std::setfill('0') << vm.currentWindow
          << ".MOJI_CNT";
      maxDialogueLines =
          std::max(1, assets->settings().integer(key.str(), 3, 1));
      auto word =
          decodeText(rawWord, archive.Scenario(vm.scenario()).encoding());
      std::string simulated = dialogue + word;
      std::string finalSpeaker, finalDialogue;
      processSpeakerAndDialogue(speakerName, simulated, finalSpeaker,
                                finalDialogue);
      const int lines = textbox->wrappedLineCount(finalDialogue);
      if (lines > maxDialogueLines) {
        currentDialogueLine = 0;
        return true;
      }
      return false;
    };
    vm.selectionText = [this](int window,
                              const std::vector<std::string> &labels) {
      selectionWindow =
          window < 0 ? assets->settings().integer("DEFAULT_SEL_WINDOW", 0)
                     : window;
      // rlBabel func=200 may be called multiple times (batched selections in
      // Clannad_PT-BR with 4-5 options across two calls). Accumulate labels
      // instead of clearing; clear only when SelectElement consumes them.
      for (const auto &raw : labels)
        selectionLabels[raw] =
            decodeText(raw, archive.Scenario(vm.scenario()).encoding());
    };
    vm.command = [this](auto &c) { return command(c); };
    vm.onUnhandled = [](const UnhandledCommand &c) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "RealLive unhandled command scene=%d pc=%zu type=%d "
                  "module=%d opcode=%d overload=%d",
                  c.scenario, c.instruction, c.moduleType, c.module, c.opcode,
                  c.overload);
    };
    vm.text = [this](std::string t) {
      dialogue += decodeText(t, archive.Scenario(vm.scenario()).encoding());
    };
    auto resolution = assets->settings().detectResolution();
    canvasW = resolution.width;
    canvasH = resolution.height;
  }
  struct SelEffect {
    SDL_Rect src{};
    SDL_Rect dst{};
    int durationMs = 0;
    int style = 0;
    int direction = 0, interpolation = 0;
    int xSize = 0, ySize = 0;
    int opacity = 255;
  };
  void setTransitionEffect(const SelEffect &effect) {
    transitionMs = std::max(0, effect.durationMs);
    transitionStyle = effect.style;
    transitionDirection = effect.direction;
    transitionInterpolation = effect.interpolation;
    transitionXSize = effect.xSize;
    transitionYSize = effect.ySize;
  }
  void setTransitionParametersFromCommand(const libreallive::CommandElement &c,
                                          int first = 8) {
    transitionStyle = value(c, first);
    transitionDirection = value(c, first + 1);
    transitionInterpolation = value(c, first + 2);
    transitionXSize = value(c, first + 3);
    transitionYSize = value(c, first + 4);
  }
  SelEffect parseSelEffect(int index, int defaultDurationMs = 0) const {
    std::ostringstream selrKey, selKey;
    selrKey << "SELR." << std::setw(3) << std::setfill('0') << index;
    selKey << "SEL." << std::setw(3) << std::setfill('0') << index;
    const auto &cfg = assets->settings();
    SelEffect out;
    if (!cfg.get(selrKey.str()).empty()) {
      const auto k = selrKey.str();
      out.src = {cfg.integer(k, 0, 0), cfg.integer(k, 0, 1),
                 cfg.integer(k, canvasW, 2), cfg.integer(k, canvasH, 3)};
      out.dst = {cfg.integer(k, 0, 4), cfg.integer(k, 0, 5), 0, 0};
      out.durationMs = cfg.integer(k, defaultDurationMs, 6);
      out.style = cfg.integer(k, 0, 7);
      out.direction = cfg.integer(k, 0, 8);
      out.interpolation = cfg.integer(k, 0, 9);
      out.xSize = cfg.integer(k, 0, 10);
      out.ySize = cfg.integer(k, 0, 11);
      out.opacity = cfg.integer(k, 255, 14);
    } else {
      const auto k = selKey.str();
      const int x1 = cfg.integer(k, 0, 0);
      const int y1 = cfg.integer(k, 0, 1);
      const int x2 = cfg.integer(k, canvasW - 1, 2);
      const int y2 = cfg.integer(k, canvasH - 1, 3);
      out.src = {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
      out.dst = {cfg.integer(k, 0, 4), cfg.integer(k, 0, 5), 0, 0};
      out.durationMs = cfg.integer(k, defaultDurationMs, 6);
      out.style = cfg.integer(k, 0, 7);
      out.direction = cfg.integer(k, 0, 8);
      out.interpolation = cfg.integer(k, 0, 9);
      out.xSize = cfg.integer(k, 0, 10);
      out.ySize = cfg.integer(k, 0, 11);
      out.opacity = cfg.integer(k, 255, 14);
    }
    return out;
  }
  std::filesystem::path slotSavePath(int slot) const {
    return Kotonoha::saveDirectory() /
           ("slot_" + std::to_string(std::max(0, slot)) + ".vn");
  }
  SurfaceRef surface(int n) {
    if (n < 0 || n > 15)
      throw std::runtime_error("Invalid REALLIVE DC index");
    auto &i = dc[n];
    if (!i)
      i = blank(canvasW, canvasH);
    return *i;
  }
  void ensureDC(int n, int w, int h) {
    auto &old = surface(n);
    if (n <= 1 || (old.w >= w && old.h >= h))
      return;
    auto grown = blank(std::max(old.w, w), std::max(old.h, h));
    auto image = dcImage(n);
    const SDL_Rect crop{0, 0, old.w, old.h};
    blit(old, *grown, nullptr, nullptr, 255, false);
    dc[n] = std::move(grown);
    auto extended = pixelImage(*dc[n]);
    extended->kind = DCImage::Kind::Copy;
    extended->source = std::move(image);
    extended->crop = crop;
    extended->destination = extended->crop;
    dcImages[n] = std::move(extended);
  }
  Graphic &graphic(const std::string &name) {
    auto id = assets->graphic(name);
    auto i = graphics.find(id);
    if (i == graphics.end()) {
      Graphic g;
      auto bytes = assets->read(id);
      if (bytes.size() >= 5) {
        g.image =
            canonical(std::filesystem::path(id).extension().string()) == ".PDT"
                ? decodePDT(bytes)
                : decodeG00(bytes);
      } else {
        g.image = Image{};
      }
      g.surface = assets->loadSurface(id);
      i = graphics.emplace(id, std::move(g)).first;
    }
    return i->second;
  }
  // objOfFile can name either a graphic (G00/PDT) or an ANM animation that
  // references one. Resolve the ANM up front so a missing animation never
  // reaches the blitter with a null surface.
  void loadObjectFile(Object &o) {
    if (o.file.empty())
      return;
    if (!assets->graphic(o.file).empty()) {
      prepareGraphic(graphic(o.file));
      return;
    }
    const auto anmId = assets->animation(o.file);
    if (anmId.empty()) {
      o.file.clear();
      return;
    }
    try {
      auto bytes = assets->read(anmId);
      auto anm = std::make_shared<Anm>(decodeANM(bytes));
      const auto imageId = assets->graphic(anm->image);
      if (imageId.empty())
        throw std::runtime_error("ANM: referenced image not found");
      prepareGraphic(graphic(imageId));
      o.anm = std::move(anm);
    } catch (const std::exception &e) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Warning: REALLIVE ANM %s unavailable: %s", o.file.c_str(),
                  e.what());
      o.anm.reset();
      o.file.clear();
    }
  }
  Uint64 anmSetDuration(const Anm &anm, int set) const {
    Uint64 total = 0;
    if (set < 0 || set >= int(anm.sets.size()))
      return 0;
    for (int listIdx : anm.sets[set]) {
      if (listIdx < 0 || listIdx >= int(anm.frameLists.size()))
        continue;
      for (int frameIdx : anm.frameLists[listIdx]) {
        if (frameIdx < 0 || frameIdx >= int(anm.frames.size()))
          continue;
        const int frameTime = anm.frames[frameIdx].time;
        total += frameTime > 0 ? Uint64(frameTime) : 0;
      }
    }
    return total;
  }
  const AnmFrame *anmCurrentFrame(const Object &o) {
    if (!o.anm || !o.anmPlaying || o.anm->sets.empty())
      return nullptr;
    const Anm &anm = *o.anm;
    const int set = std::clamp(o.anmSet, 0, int(anm.sets.size()) - 1);
    const Uint64 elapsed = SDL_GetTicks() - o.anmStart;
    const AnmFrame *last = nullptr;
    Uint64 t = 0;
    for (int listIdx : anm.sets[set]) {
      if (listIdx < 0 || listIdx >= int(anm.frameLists.size()))
        continue;
      for (int frameIdx : anm.frameLists[listIdx]) {
        if (frameIdx < 0 || frameIdx >= int(anm.frames.size()))
          continue;
        const AnmFrame &frame = anm.frames[frameIdx];
        last = &frame;
        const Uint64 frameTime = frame.time > 0 ? Uint64(frame.time) : 0;
        if (elapsed < t + frameTime)
          return &frame;
        t += frameTime;
      }
    }
    return last;
  }
  void blit(SurfaceRef source, SurfaceRef dest, const SDL_Rect *sr = nullptr,
            SDL_Rect *dr = nullptr, int alpha = 255, bool mask = true) {
    if (alpha <= 0) {
      BlitSurfaceOpacity(source, dest, sr, dr, alpha, mask);
      return;
    }
    const SDL_Rect requested = sr ? *sr : SDL_Rect{0, 0, source.w, source.h};
    const SDL_Rect bounds{0, 0, source.w, source.h};
    SDL_Rect crop{};
    if (!SDL_GetRectIntersection(&requested, &bounds, &crop)) {
      BlitSurfaceOpacity(source, dest, sr, dr, alpha, mask);
      return;
    }
    const SDL_Rect destination{(dr ? dr->x : 0) + crop.x - requested.x,
                               (dr ? dr->y : 0) + crop.y - requested.y, crop.w,
                               crop.h};
    for (const auto &[id, pixels] : dc) {
      if (pixels.get() != &dest)
        continue;
      auto result = std::make_shared<DCImage>();
      result->kind = DCImage::Kind::Copy;
      result->width = dest.w;
      result->height = dest.h;
      result->crop = crop;
      result->destination = destination;
      result->alpha = alpha;
      result->mask = mask;
      result->previous = dcImage(id);
      for (const auto &[name, graphic] : graphics)
        if (graphic.surface.get() == &source) {
          result->source = std::make_shared<DCImage>();
          result->source->kind = DCImage::Kind::Graphic;
          result->source->graphic = name;
          result->source->crop = crop;
          result->source->width = crop.w;
          result->source->height = crop.h;
          result->crop = {0, 0, crop.w, crop.h};
          break;
        }
      if (!result->source)
        for (const auto &[sourceID, pixels] : dc)
          if (pixels.get() == &source) {
            result->source = dcImage(sourceID);
            break;
          }
      if (!result->source && zoom && zoom->source.get() == &source)
        result->source = zoom->image;
      if (!result->source)
        result->source = pixelImage(source);
      if (alpha >= 255 && destination.x <= 0 && destination.y <= 0 &&
          destination.x + destination.w >= dest.w &&
          destination.y + destination.h >= dest.h) {
        bool opaque = !mask || source.format == SDL_PIXELFORMAT_RGBX32;
        if (mask && source.format == SDL_PIXELFORMAT_RGBA32) {
          opaque = true;
          for (int y = crop.y; opaque && y < crop.y + crop.h; ++y) {
            const auto *row =
                static_cast<const Uint8 *>(source.pixels) + y * source.pitch;
            for (int x = crop.x; x < crop.x + crop.w; ++x)
              if (row[x * 4 + 3] != 255) {
                opaque = false;
                break;
              }
          }
        }
        if (opaque)
          result->previous.reset();
      }
      dcImages[id] = std::move(result);
      break;
    }
    BlitSurfaceOpacity(source, dest, sr, dr, alpha, mask);
  }
  std::shared_ptr<DCImage> pixelImage(SurfaceRef pixels) {
    auto image = std::make_shared<DCImage>();
    image->width = pixels.w;
    image->height = pixels.h;
    image->native.reset(SDL_DuplicateSurface(&pixels));
    if (!image->native)
      throw std::runtime_error(SDL_GetError());
    return image;
  }
  std::shared_ptr<DCImage> dcImage(int id) {
    auto &image = dcImages[id];
    if (!image)
      image = pixelImage(surface(id));
    return image;
  }
  Graphic::Raster &prepareRaster(Graphic &g, const SDL_Rect &crop) {
    auto &cached = g.crops[{crop.x, crop.y, crop.w, crop.h}];
    if (!g.surface || crop.w <= 0 || crop.h <= 0)
      return cached;
    auto &renderer = engine->GetRenderer();
    if (!cached.original) {
      SurfacePtr pixels(
          SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
      if (!pixels)
        throw std::runtime_error(SDL_GetError());
      BlitSurfaceOpacity(*g.surface, *pixels, &crop, nullptr, 255, false);
      cached.original.reset(
          SDL_CreateTextureFromSurface(&renderer, pixels.get()));
      if (!cached.original ||
          !SDL_SetTextureScaleMode(cached.original.get(), SDL_SCALEMODE_LINEAR))
        throw std::runtime_error(SDL_GetError());
      ++renderStats.textureUploads;
      ++renderStats.originalImageUploads;
    }
    if (presentationScale() > 1 && !cached.filtered) {
      if (!SDL_SetTextureAlphaMod(cached.original.get(), 255))
        throw std::runtime_error(SDL_GetError());
      cached.filtered =
          engine->GetUpscaler().scaled(*cached.original, imageUpscaling.factor);
      ++renderStats.filteredPatterns;
    }
    return cached;
  }
  TextureRef dcTexture(const std::shared_ptr<DCImage> &image,
                       SurfaceRef native) {
    refreshImageUpscaling();
    if (image->texture && image->generation == dcGeneration)
      return *image->texture;
    image->raster.reset();
    auto &renderer = engine->GetRenderer();
    TexturePtr original(SDL_CreateTextureFromSurface(&renderer, &native));
    if (!original)
      throw std::runtime_error(SDL_GetError());
    ++renderStats.textureUploads;
    image->texture =
        engine->GetUpscaler().scaled(*original, imageUpscaling.factor);
    ++renderStats.filteredDCFrames;
    const int scale = presentationScale();
    if (image->texture->w != native.w * scale ||
        image->texture->h != native.h * scale) {
      TexturePtr display(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_TARGET,
                                           native.w * scale, native.h * scale));
      struct Restore {
        SDL_Renderer &renderer;
        SDL_Texture *target;
        ~Restore() { SDL_SetRenderTarget(&renderer, target); }
      } restore{renderer, SDL_GetRenderTarget(&renderer)};
      if (!display ||
          !SDL_SetTextureBlendMode(image->texture.get(), SDL_BLENDMODE_NONE) ||
          !SDL_SetTextureScaleMode(image->texture.get(),
                                   SDL_SCALEMODE_LINEAR) ||
          !SDL_SetRenderTarget(&renderer, display.get()) ||
          !SDL_RenderTexture(&renderer, image->texture.get(), nullptr, nullptr))
        throw std::runtime_error(SDL_GetError());
      image->texture = std::move(display);
    }
    if (!SDL_SetTextureScaleMode(image->texture.get(), SDL_SCALEMODE_LINEAR) ||
        !SDL_SetTextureBlendMode(image->texture.get(), SDL_BLENDMODE_NONE))
      throw std::runtime_error(SDL_GetError());
    image->generation = dcGeneration;
    for (const auto &child : {image->source, image->previous}) {
      if (!child || (zoom && zoom->image == child))
        continue;
      const bool resident =
          std::any_of(dcImages.begin(), dcImages.end(),
                      [&](const auto &entry) { return entry.second == child; });
      if (!resident) {
        child->raster.reset();
        child->texture.reset();
      }
    }
    return *image->texture;
  }
  SurfaceRef dcRaster(const std::shared_ptr<DCImage> &image,
                      SurfaceRef native) {
    auto &texture = dcTexture(image, native);
    if (!image->raster) {
      auto &renderer = engine->GetRenderer();
      struct Restore {
        SDL_Renderer &renderer;
        SDL_Texture *target;
        ~Restore() { SDL_SetRenderTarget(&renderer, target); }
      } restore{renderer, SDL_GetRenderTarget(&renderer)};
      if (!SDL_SetRenderTarget(&renderer, &texture))
        throw std::runtime_error(SDL_GetError());
      image->raster.reset(SDL_RenderReadPixels(&renderer, nullptr));
      if (!image->raster)
        throw std::runtime_error(SDL_GetError());
      if (image->raster->format != SDL_PIXELFORMAT_RGBA32) {
        SurfacePtr converted(
            SDL_ConvertSurface(image->raster.get(), SDL_PIXELFORMAT_RGBA32));
        if (!converted)
          throw std::runtime_error(SDL_GetError());
        image->raster = std::move(converted);
      }
    }
    return *image->raster;
  }
  int presentationScale() const {
    // REALLIVE script scenes compose natively; only the completed scene is
    // filtered.
    return 1;
  }
  void refreshImageUpscaling() {
    const auto &settings = engine->GetUpscaler().getSettings();
    if (settings.backend == imageUpscaling.backend &&
        settings.preset == imageUpscaling.preset &&
        settings.factor == imageUpscaling.factor &&
        settings.shaderFiles == imageUpscaling.shaderFiles)
      return;
    for (auto &[id, g] : graphics) {
      g.scaledPatterns.clear();
      for (auto &[crop, raster] : g.crops) {
        raster.filtered.reset();
        raster.display.reset();
      }
    }
    ++dcGeneration;
    filteredScene.reset();
    sceneKey.clear();
    zoomTexture.reset();
    imageUpscaling = settings;
    objectTextTextures.clear();
  }
  SDL_Rect patternRect(const Graphic &g, size_t pattern) const {
    if (g.image.regions.empty())
      return {0, 0, int(g.image.width), int(g.image.height)};
    const auto &r = g.image.regions.at(pattern);
    return {r.atlasX, r.atlasY, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1};
  }
  TextureRef patternTexture(Graphic &g, size_t pattern) {
    const auto crop = patternRect(g, pattern);
    auto &cached = g.crops.at({crop.x, crop.y, crop.w, crop.h});
    SDL_Texture *texture = presentationScale() == 1 ? cached.original.get()
                           : cached.display         ? cached.display.get()
                                                    : cached.filtered.get();
    if (!texture)
      throw std::runtime_error(
          "REALLIVE graphic pattern has no prepared texture");
    return *texture;
  }
  void prepareGraphic(Graphic &g) {
    refreshImageUpscaling();
    const int scale = presentationScale();
    const size_t count = std::max(size_t(1), g.image.regions.size());
    auto &renderer = engine->GetRenderer();
    for (size_t pattern = 0; pattern < count; ++pattern) {
      const auto crop = patternRect(g, pattern);
      auto &cached = prepareRaster(g, crop);
      if (scale == 1 || cached.display)
        continue;
      auto &texture = cached.filtered;
      if (texture->w != crop.w * scale || texture->h != crop.h * scale) {
        TexturePtr raster(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET,
                                            crop.w * scale, crop.h * scale));
        if (!raster)
          throw std::runtime_error(SDL_GetError());
        struct Restore {
          SDL_Renderer &renderer;
          SDL_Texture *target;
          ~Restore() { SDL_SetRenderTarget(&renderer, target); }
        } restore{renderer, SDL_GetRenderTarget(&renderer)};
        if (!SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE) ||
            !SDL_SetRenderTarget(&renderer, raster.get()) ||
            !SDL_RenderTexture(&renderer, texture.get(), nullptr, nullptr) ||
            !SDL_SetTextureScaleMode(raster.get(), SDL_SCALEMODE_LINEAR) ||
            !SDL_SetTextureBlendMode(raster.get(), SDL_BLENDMODE_BLEND))
          throw std::runtime_error(SDL_GetError());
        cached.display = std::move(raster);
      }
    }
  }
  bool prepareDither(RendererRef renderer) {
    if (gpuDither)
      return *gpuDither;
    ditherTexture.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, 8, 8));
    if (!ditherTexture)
      throw std::runtime_error(SDL_GetError());
    const auto maskBlend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
    gpuDither = SDL_SetTextureBlendMode(ditherTexture.get(), maskBlend);
    if (!*gpuDither) {
      SDL_LogWarn(
          SDL_LOG_CATEGORY_APPLICATION,
          "REALLIVE renderer cannot blend dither masks; using native effect "
          "pixels: %s",
          SDL_GetError());
      ditherTexture.reset();
    } else if (!SDL_SetTextureScaleMode(ditherTexture.get(),
                                        SDL_SCALEMODE_NEAREST)) {
      throw std::runtime_error(SDL_GetError());
    }
    return *gpuDither;
  }
  void drawHardware(RendererRef r, int scale) {
    SDL_Texture *preparedBackground = nullptr;
    if (scale > 1)
      preparedBackground = &dcTexture(dcImage(0), surface(0));
    else {
      auto &background = surface(0);
      // DC0 is the authoritative native raster. Upload only on pixel changes,
      // but always upload after a texture has been recreated (device/resize).
      const bool recreate = !backgroundTexture ||
                            backgroundTexture->w != background.w ||
                            backgroundTexture->h != background.h;
      bool changed = recreate || !backgroundPixels ||
                     backgroundPixels->w != background.w ||
                     backgroundPixels->h != background.h;
      for (int y = 0; !changed && y < background.h; ++y)
        changed =
            std::memcmp(static_cast<const Uint8 *>(backgroundPixels->pixels) +
                            y * backgroundPixels->pitch,
                        static_cast<const Uint8 *>(background.pixels) +
                            y * background.pitch,
                        std::size_t(background.w) * 4) != 0;
      if (recreate) {
        backgroundTexture.reset(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  background.w, background.h));
        if (!backgroundTexture ||
            !SDL_SetTextureBlendMode(backgroundTexture.get(),
                                     SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(backgroundTexture.get(),
                                     SDL_SCALEMODE_LINEAR))
          throw std::runtime_error(std::string("REALLIVE DC0 texture: ") +
                                   SDL_GetError());
      }
      if (changed) {
        if (!SDL_UpdateTexture(backgroundTexture.get(), nullptr,
                               background.pixels, background.pitch))
          throw std::runtime_error(std::string("REALLIVE DC0 upload: ") +
                                   SDL_GetError());
        backgroundPixels.reset(SDL_DuplicateSurface(&background));
        if (!backgroundPixels)
          throw std::runtime_error(std::string("REALLIVE DC0 snapshot: ") +
                                   SDL_GetError());
        ++renderStats.textureUploads;
      }
      preparedBackground = backgroundTexture.get();
    }
    // Prepare all animation patterns before entering the composition target.
    for (auto &[id, o] : fg)
      if (o.visible && !o.file.empty() && o.text.empty() &&
          !(o.width > 0 && o.height > 0)) {
        if (o.anm) {
          if (const auto *frame = anmCurrentFrame(o)) {
            auto &g = graphic(o.anm->image);
            prepareRaster(g, {frame->x1, frame->y1, frame->x2 - frame->x1 + 1,
                              frame->y2 - frame->y1 + 1});
          }
        } else {
          prepareGraphic(graphic(o.file));
        }
      }
    if (!compositionTexture || compositionTexture->w != canvasW * scale ||
        compositionTexture->h != canvasH * scale) {
      compositionTexture.reset(SDL_CreateTexture(
          &r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, canvasW * scale,
          canvasH * scale));
      // A completed scene is a replacement raster, never a translucent
      // overlay over the engine's black clear. RGBA alpha of intermediary DCs
      // must not erase otherwise visible script backgrounds.
      if (!configureSceneTexture(compositionTexture.get()))
        throw std::runtime_error(SDL_GetError());
    }
    struct RestoreTarget {
      SDL_Renderer &renderer;
      SDL_Texture *target;
      SDL_Color color{};
      SDL_BlendMode blend = SDL_BLENDMODE_NONE;
      ~RestoreTarget() {
        SDL_SetRenderTarget(&renderer, target);
        SDL_SetRenderDrawColor(&renderer, color.r, color.g, color.b, color.a);
        SDL_SetRenderDrawBlendMode(&renderer, blend);
      }
    } restore{r, SDL_GetRenderTarget(&r)};
    if (!SDL_GetRenderDrawColor(&r, &restore.color.r, &restore.color.g,
                                &restore.color.b, &restore.color.a) ||
        !SDL_GetRenderDrawBlendMode(&r, &restore.blend) ||
        !SDL_SetRenderTarget(&r, compositionTexture.get()) ||
        !SDL_SetRenderScale(&r, 1, 1) || !SDL_SetRenderViewport(&r, nullptr) ||
        !SDL_SetRenderClipRect(&r, nullptr) ||
        !SDL_RenderTexture(&r, preparedBackground, nullptr, nullptr))
      throw std::runtime_error(SDL_GetError());
    for (const auto &[id, o] : fg) {
      if (!o.visible || !o.text.empty())
        continue;
      int x = o.x, y = o.y;
      for (const auto &adjustment : o.adjustments) {
        x += adjustment.x;
        y += adjustment.y;
      }
      const SDL_Rect clip{o.clip.x * scale, o.clip.y * scale, o.clip.w * scale,
                          o.clip.h * scale};
      if (!SDL_SetRenderClipRect(&r, o.clipped ? &clip : nullptr))
        throw std::runtime_error(SDL_GetError());
      if (o.anm) {
        const auto *frame = anmCurrentFrame(o);
        if (!frame)
          continue;
        auto &g = graphic(o.anm->image);
        const SDL_Rect source{frame->x1, frame->y1, frame->x2 - frame->x1 + 1,
                              frame->y2 - frame->y1 + 1};
        auto &raster = prepareRaster(g, source);
        if (!raster.original)
          continue;
        const SDL_FRect area{float(frame->destX * scale),
                             float(frame->destY * scale),
                             float(source.w * scale), float(source.h * scale)};
        if (!SDL_SetTextureAlphaMod(raster.original.get(),
                                    Uint8(std::clamp(o.alpha, 0, 255))) ||
            !SDL_RenderTexture(&r, raster.original.get(), nullptr, &area))
          throw std::runtime_error(SDL_GetError());
      } else if (o.width > 0 && o.height > 0) {
        const SDL_FRect area{float(x * scale), float(y * scale),
                             float(o.width * scale), float(o.height * scale)};
        if (!SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND) ||
            !SDL_SetRenderDrawColor(
                &r, o.colour.r, o.colour.g, o.colour.b,
                Uint8(o.colour.a * std::clamp(o.alpha, 0, 255) / 255)) ||
            !SDL_RenderFillRect(&r, &area))
          throw std::runtime_error(SDL_GetError());
      } else if (!o.file.empty() && o.text.empty()) {
        auto &g = graphic(o.file);
        const auto pattern = g.image.regions.empty()
                                 ? 0
                                 : o.currentPattern(g.image.regions.size());
        if (!g.image.regions.empty()) {
          const auto &reg = g.image.regions[pattern];
          if (reg.originX != 0 || reg.originY != 0) {
            x -= reg.originX;
            y -= reg.originY;
          } else if (g.image.regions.size() == 1 &&
                     (reg.x1 != 0 || reg.y1 != 0)) {
            x += reg.x1;
            y += reg.y1;
          }
        }
        auto &texture = patternTexture(g, pattern);
        const SDL_FRect area{float(x * scale), float(y * scale),
                             float(texture.w), float(texture.h)};
        if (!SDL_SetTextureAlphaMod(&texture,
                                    Uint8(std::clamp(o.alpha, 0, 255))) ||
            !SDL_RenderTexture(&r, &texture, nullptr, &area))
          throw std::runtime_error(SDL_GetError());
      }
    }
    if (!SDL_SetRenderClipRect(&r, nullptr))
      throw std::runtime_error(SDL_GetError());
    if (flashDuration > 0 &&
        SDL_GetTicks() - flashStart < Uint64(flashDuration)) {
      const int step =
          std::min(16, int((SDL_GetTicks() - flashStart) * 16 / flashDuration));
      if (!SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND) ||
          !SDL_SetRenderDrawColor(&r, flashColour.r, flashColour.g,
                                  flashColour.b,
                                  Uint8((16 - step) * 255 / 16)) ||
          !SDL_RenderFillRect(&r, nullptr))
        throw std::runtime_error(SDL_GetError());
    }
    if (zoom) {
      if (scale == 1 && !zoomTexture) {
        zoomTexture.reset(SDL_CreateTextureFromSurface(&r, zoom->source.get()));
        if (!zoomTexture ||
            !SDL_SetTextureScaleMode(zoomTexture.get(), SDL_SCALEMODE_LINEAR))
          throw std::runtime_error(SDL_GetError());
        ++renderStats.textureUploads;
      }
      auto *preparedZoom = scale == 1 ? zoomTexture.get()
                                      : &dcTexture(zoom->image, *zoom->source);
      const auto elapsed = SDL_GetTicks() - zoom->start;
      const auto crop = zoom->animation.sample(double(elapsed));
      const SDL_FRect source{float(std::lround(crop.x) * scale),
                             float(std::lround(crop.y) * scale),
                             float(std::lround(crop.scaleX) * scale),
                             float(std::lround(crop.scaleY) * scale)};
      const SDL_FRect destination{float(zoom->destinationRect.x * scale),
                                  float(zoom->destinationRect.y * scale),
                                  float(zoom->destinationRect.w * scale),
                                  float(zoom->destinationRect.h * scale)};
      if (source.w > 0 && source.h > 0 &&
          !SDL_RenderTexture(&r, preparedZoom, &source, &destination))
        throw std::runtime_error(SDL_GetError());
      if (elapsed >= zoom->animation.duration()) {
        blit(*zoom->source, surface(0), &zoom->to, &zoom->destinationRect, 255,
             true);
        zoom.reset();
        zoomTexture.reset();
      }
    }
    if (transitionOld) {
      const auto elapsed = SDL_GetTicks() - transitionStart;
      if (elapsed < Uint64(transitionMs)) {
        if (!transitionTexture) {
          transitionTexture.reset(
              SDL_CreateTextureFromSurface(&r, transitionOld.get()));
          if (!transitionTexture ||
              !SDL_SetTextureScaleMode(transitionTexture.get(),
                                       SDL_SCALEMODE_LINEAR))
            throw std::runtime_error(SDL_GetError());
          ++renderStats.textureUploads;
        }
        const double progress = double(elapsed) / transitionMs;
        if (transitionStyle == 4 || transitionStyle == 5) {
          const int step = int(std::ceil(progress * 64));
          if (step != ditherStep || transitionStyle != ditherStyle) {
            std::array<Uint8, 8 * 8 * 4> mask{};
            for (int y = 0; y < 8; ++y)
              for (int x = 0; x < 8; ++x)
                mask[(y * 8 + x) * 4 + 3] =
                    transitionRevealsNewPixel(transitionStyle, progress, x, y)
                        ? 0
                        : 255;
            if (!SDL_UpdateTexture(ditherTexture.get(), nullptr, mask.data(),
                                   8 * 4))
              throw std::runtime_error(SDL_GetError());
            ditherStep = step;
            ditherStyle = transitionStyle;
          }
          if (!maskedTransitionTexture ||
              maskedTransitionTexture->w != compositionTexture->w ||
              maskedTransitionTexture->h != compositionTexture->h) {
            maskedTransitionTexture.reset(SDL_CreateTexture(
                &r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                compositionTexture->w, compositionTexture->h));
            if (!maskedTransitionTexture ||
                !SDL_SetTextureBlendMode(maskedTransitionTexture.get(),
                                         SDL_BLENDMODE_BLEND))
              throw std::runtime_error(SDL_GetError());
          }
          if (!SDL_SetTextureAlphaMod(transitionTexture.get(), 255) ||
              !SDL_SetTextureBlendMode(transitionTexture.get(),
                                       SDL_BLENDMODE_NONE) ||
              !SDL_SetRenderTarget(&r, maskedTransitionTexture.get()) ||
              !SDL_RenderTexture(&r, transitionTexture.get(), nullptr,
                                 nullptr) ||
              !SDL_RenderTextureTiled(&r, ditherTexture.get(), nullptr,
                                      float(scale), nullptr) ||
              !SDL_SetRenderTarget(&r, compositionTexture.get()) ||
              !SDL_RenderTexture(&r, maskedTransitionTexture.get(), nullptr,
                                 nullptr))
            throw std::runtime_error(SDL_GetError());
        } else {
          const auto alpha = 255 - int(255.0 * progress);
          if (!SDL_SetTextureBlendMode(transitionTexture.get(),
                                       SDL_BLENDMODE_BLEND) ||
              !SDL_SetTextureAlphaMod(transitionTexture.get(), Uint8(alpha)) ||
              !SDL_RenderTexture(&r, transitionTexture.get(), nullptr, nullptr))
            throw std::runtime_error(SDL_GetError());
        }
      } else {
        transitionOld.reset();
        transitionTexture.reset();
      }
    }
    ++renderStats.gpuFrames;
  }
  void scaledBlit(SurfaceRef source, SurfaceRef dest, const SDL_Rect *src,
                  SDL_Rect target, int alpha = 255, bool mask = true) {
    if (target.w <= 0 || target.h <= 0)
      return;
    const int width = src ? src->w : source.w;
    const int height = src ? src->h : source.h;
    if (width == target.w && height == target.h) {
      SDL_Rect point{target.x, target.y, 0, 0};
      blit(source, dest, src, &point, alpha, mask);
      return;
    }
    SurfacePtr resized(
        SDL_CreateSurface(target.w, target.h, SDL_PIXELFORMAT_RGBA32));
    SDL_BlendMode blend;
    if (!SDL_GetSurfaceBlendMode(&source, &blend))
      throw std::runtime_error(SDL_GetError());
    struct RestoreBlend {
      SDL_Surface &source;
      SDL_BlendMode blend;
      ~RestoreBlend() { SDL_SetSurfaceBlendMode(&source, blend); }
    } restore{source, blend};
    if (!resized || !SDL_SetSurfaceBlendMode(&source, SDL_BLENDMODE_NONE) ||
        !SDL_BlitSurfaceScaled(&source, src, resized.get(), nullptr,
                               SDL_SCALEMODE_LINEAR))
      throw std::runtime_error(SDL_GetError());
    SDL_Rect point{target.x, target.y, 0, 0};
    blit(*resized, dest, nullptr, &point, alpha, mask);
  }
  void drawDriftObject(Object &o, SurfaceRef dest, int scale) {
    if (o.file.empty())
      return;
    auto &g = graphic(o.file);
    const auto surface = g.surface.get();
    if (!surface)
      return;

    SDL_Rect bb = o.driftArea;
    if (bb.w < 0 || bb.h < 0)
      bb = {0, 0, canvasW, canvasH};
    const int bw = std::max(1, bb.w * scale), bh = std::max(1, bb.h * scale);

    const Uint64 now = SDL_GetTicks();
    // Lazily spawn particles up to driftCount, persisting while the object
    // lives (they are re-seeded on each object creation via 1300).
    while (o.driftParticles.size() < size_t(o.driftCount)) {
      Object::DriftParticle p;
      p.x = rand() % bw;
      p.y = rand() % bh;
      p.alpha = 255;
      p.startTime = now;
      o.driftParticles.push_back(p);
    }

    const Uint64 elapsedBase = now - o.driftLastSpawn;
    o.driftLastSpawn = now;

    for (auto &p : o.driftParticles) {
      const Uint64 elapsed = now - p.startTime;

      int pattern = o.driftStartPattern;
      if (o.driftUseAnimation && o.driftEndPattern > o.driftStartPattern) {
        const int numPatterns = o.driftEndPattern - o.driftStartPattern + 1;
        const int frameTime =
            o.driftAnimationTime > 0 ? o.driftAnimationTime / numPatterns : 0;
        if (frameTime > 0)
          pattern =
              o.driftStartPattern + int((elapsed / frameTime) % numPatterns);
      }

      SDL_Rect src{0, 0, int(g.image.width), int(g.image.height)};
      if (!g.image.regions.empty() && pattern < g.image.regions.size()) {
        const auto &r = g.image.regions.at(pattern);
        src = {r.atlasX, r.atlasY, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1};
      }

      int destX = p.x;
      int destY = p.y;

      // Base vertical drift (top -> bottom over yspeed ms).
      if (o.driftYSpeed > 0)
        destY +=
            bh * int((elapsed % Uint64(o.driftYSpeed)) / Uint64(o.driftYSpeed));

      // Sine wave horizontal offset.
      if (o.driftPeriod > 0 && o.driftAmplitude != 0) {
        const double scaledAmplitude =
            bw * (1.0 / std::max(1, o.driftAmplitude / 100));
        const double result = std::sin(
            (double(elapsed) / double(o.driftPeriod)) * (2.0 * 3.14159));
        destX += int(scaledAmplitude * result);
      }

      // Leftward drift.
      if (o.driftUseDrift && o.driftDriftSpeed > 0)
        destX -= bw * int((elapsed % Uint64(o.driftDriftSpeed)) /
                          Uint64(o.driftDriftSpeed));

      // Wrap around.
      if (destX < 0)
        destX += bw;
      else
        destX %= bw;
      if (destY < 0)
        destY += bh;
      else
        destY %= bh;

      SDL_Rect dst{bb.x * scale + destX, bb.y * scale + destY, src.w, src.h};
      blit(*surface, dest, &src, &dst, std::clamp(p.alpha, 0, 255));
    }
  }

  void drawObject(Object &o, SurfaceRef dest, int scale) {
    if (o.kind == Object::Kind::Drift) {
      drawDriftObject(o, dest, scale);
      return;
    }
    int adjustX = 0, adjustY = 0;
    for (const auto &adjustment : o.adjustments) {
      adjustX += adjustment.x;
      adjustY += adjustment.y;
    }
    if (!o.visible)
      return;
    SDL_Rect oldClip;
    SDL_GetSurfaceClipRect(&dest, &oldClip);
    struct Restore {
      SDL_Surface &s;
      SDL_Rect r;
      ~Restore() { SDL_SetSurfaceClipRect(&s, &r); }
    } restore{dest, oldClip};
    if (o.clipped) {
      SDL_Rect clip{o.clip.x * scale, o.clip.y * scale, o.clip.w * scale,
                    o.clip.h * scale};
      SDL_SetSurfaceClipRect(&dest, &clip);
    }
    // ANM file objects ignore o.x/o.y and draw timed frame crops at their
    // absolute destination coordinates.
    if (o.anm) {
      if (const AnmFrame *frame = anmCurrentFrame(o)) {
        auto &g = graphic(o.anm->image);
        SDL_Rect src{frame->x1, frame->y1, frame->x2 - frame->x1 + 1,
                     frame->y2 - frame->y1 + 1};
        SDL_Rect dst{frame->destX * scale, frame->destY * scale, src.w * scale,
                     src.h * scale};
        blit(*g.surface, dest, &src, &dst, o.alpha);
        return;
      }
    }
    if (o.width > 0 && o.height > 0) {
      auto fill = blank(o.width, o.height);
      SDL_FillSurfaceRect(fill.get(), nullptr,
                          SDL_MapSurfaceRGBA(fill.get(), o.colour.r, o.colour.g,
                                             o.colour.b, o.colour.a));
      SDL_Rect dst{o.x + adjustX, o.y + adjustY, 0, 0};
      if (scale == 1)
        blit(*fill, dest, nullptr, &dst, o.alpha);
      else
        scaledBlit(
            *fill, dest, nullptr,
            {dst.x * scale, dst.y * scale, o.width * scale, o.height * scale},
            o.alpha);
      return;
    }
    if (!o.text.empty() && textbox && textbox->font) {
      auto decoded =
          decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
      const auto oldSize = TTF_GetFontSize(textbox->font.get());
      if (!TTF_SetFontSize(textbox->font.get(), oldSize * scale))
        throw std::runtime_error(SDL_GetError());
      SurfacePtr rendered(TTF_RenderText_Blended(textbox->font.get(),
                                                 decoded.data(), decoded.size(),
                                                 {255, 255, 255, 255}));
      TTF_SetFontSize(textbox->font.get(), oldSize);
      if (!rendered)
        throw std::runtime_error(SDL_GetError());
      SDL_Rect dst{(o.x + adjustX) * scale, (o.y + adjustY) * scale, 0, 0};
      blit(*rendered, dest, nullptr, &dst, o.alpha);
      return;
    }
    if (!o.file.empty()) {
      auto &g = graphic(o.file);
      SDL_Rect src{0, 0, int(g.image.width), int(g.image.height)},
          dst{o.x + adjustX, o.y + adjustY, 0, 0};
      const size_t pattern = g.image.regions.empty()
                                 ? 0
                                 : o.currentPattern(g.image.regions.size());
      if (!g.image.regions.empty()) {
        auto &r = g.image.regions.at(pattern);
        src = {r.atlasX, r.atlasY, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1};
        if (r.originX != 0 || r.originY != 0) {
          dst.x -= r.originX;
          dst.y -= r.originY;
        } else if (g.image.regions.size() == 1 && (r.x1 != 0 || r.y1 != 0)) {
          dst.x += r.x1;
          dst.y += r.y1;
        }
      }
      if (scale == 1)
        blit(*g.surface, dest, &src, &dst, o.alpha);
      else {
        auto &pixels = g.scaledPatterns[pattern];
        if (!pixels) {
          prepareGraphic(g);
          auto &renderer = engine->GetRenderer();
          struct Restore {
            SDL_Renderer &renderer;
            SDL_Texture *target;
            ~Restore() { SDL_SetRenderTarget(&renderer, target); }
          } restore{renderer, SDL_GetRenderTarget(&renderer)};
          if (!SDL_SetRenderTarget(&renderer, &patternTexture(g, pattern)))
            throw std::runtime_error(SDL_GetError());
          pixels.reset(SDL_RenderReadPixels(&renderer, nullptr));
          if (!pixels)
            throw std::runtime_error(SDL_GetError());
          if (pixels->format != SDL_PIXELFORMAT_RGBA32) {
            SurfacePtr converted(
                SDL_ConvertSurface(pixels.get(), SDL_PIXELFORMAT_RGBA32));
            if (!converted)
              throw std::runtime_error(SDL_GetError());
            pixels = std::move(converted);
          }
        }
        scaledBlit(*pixels, dest, nullptr,
                   {dst.x * scale, dst.y * scale, src.w * scale, src.h * scale},
                   o.alpha);
      }
    }
  }
  SurfacePtr compose(int scale) {
    refreshImageUpscaling();
    auto output = blank(canvasW * scale, canvasH * scale);
    scaledBlit(scale == 1 ? surface(0) : dcRaster(dcImage(0), surface(0)),
               *output, nullptr, {0, 0, output->w, output->h}, 255, false);
    for (auto &[id, o] : fg)
      if (o.text.empty())
        drawObject(o, *output, scale);
    if (flashDuration > 0 &&
        SDL_GetTicks() - flashStart < Uint64(flashDuration)) {
      auto colour = blank(output->w, output->h);
      SDL_FillSurfaceRect(colour.get(), nullptr,
                          SDL_MapSurfaceRGBA(colour.get(), flashColour.r,
                                             flashColour.g, flashColour.b,
                                             255));
      const int step =
          std::min(16, int((SDL_GetTicks() - flashStart) * 16 / flashDuration));
      blit(*colour, *output, nullptr, nullptr, (16 - step) * 255 / 16, true);
    }
    if (zoom) {
      const auto elapsed = SDL_GetTicks() - zoom->start;
      if (elapsed >= zoom->animation.duration()) {
        blit(*zoom->source, surface(0), &zoom->to, &zoom->destinationRect, 255,
             true);
        const SDL_Rect crop{zoom->to.x * scale, zoom->to.y * scale,
                            zoom->to.w * scale, zoom->to.h * scale};
        scaledBlit(
            scale == 1 ? *zoom->source : dcRaster(zoom->image, *zoom->source),
            *output, &crop,
            {zoom->destinationRect.x * scale, zoom->destinationRect.y * scale,
             zoom->destinationRect.w * scale, zoom->destinationRect.h * scale});
        zoom.reset();
      } else {
        const auto source = zoom->animation.sample(double(elapsed));
        SDL_Rect sourceCrop{int(std::lround(source.x)) * scale,
                            int(std::lround(source.y)) * scale,
                            int(std::lround(source.scaleX)) * scale,
                            int(std::lround(source.scaleY)) * scale};
        if (sourceCrop.w > 0 && sourceCrop.h > 0)
          scaledBlit(
              scale == 1 ? *zoom->source : dcRaster(zoom->image, *zoom->source),
              *output, &sourceCrop,
              {zoom->destinationRect.x * scale, zoom->destinationRect.y * scale,
               zoom->destinationRect.w * scale,
               zoom->destinationRect.h * scale});
      }
    }
    return output;
  }
  void drawSoftwareEffects(RendererRef r) {
    const int scale = presentationScale();
    auto output = compose(scale);
    if (transitionOld &&
        SDL_GetTicks() - transitionStart < Uint64(transitionMs)) {
      const double progress =
          double(SDL_GetTicks() - transitionStart) / transitionMs;
      auto old = blank(output->w, output->h);
      scaledBlit(*transitionOld, *old, nullptr, {0, 0, old->w, old->h}, 255,
                 false);
      if (isScrollTransitionStyle(transitionStyle)) {
        // Scroll/squash/slide require moving and scaling both frames, rather
        // than masking the old frame in place. Compose in native coordinates
        // and apply the shared upscaler only after this finished raster.
        auto mixed = blank(output->w, output->h);
        const int nativeWidth = output->w / scale;
        const int nativeHeight = output->h / scale;
        for (int y = 0; y < output->h; ++y) {
          auto *destination =
              static_cast<Uint8 *>(mixed->pixels) + y * mixed->pitch;
          for (int x = 0; x < output->w; ++x) {
            const auto sample = sampleScrollTransition(
                transitionStyle, transitionDirection, progress, x / scale,
                y / scale, nativeWidth, nativeHeight);
            const auto &surface = sample.newScene ? *output : *old;
            const auto *row = static_cast<const Uint8 *>(surface.pixels) +
                              (sample.y * scale + y % scale) * surface.pitch;
            std::memcpy(destination + x * 4,
                        row + (sample.x * scale + x % scale) * 4, 4);
          }
        }
        output = std::move(mixed);
      } else if (transitionStyle == 4 || transitionStyle == 5 ||
                 transitionStyle == 10 || transitionStyle == 120) {
        const TransitionParameters effect{transitionStyle, transitionDirection,
                                          transitionInterpolation,
                                          transitionXSize, transitionYSize};
        for (int y = 0; y < output->h; ++y) {
          const auto *oldRow =
              static_cast<const Uint8 *>(old->pixels) + y * old->pitch;
          auto *newRow =
              static_cast<Uint8 *>(output->pixels) + y * output->pitch;
          for (int x = 0; x < output->w; ++x) {
            const double reveal =
                transitionNewCoverage(effect, progress, x / scale, y / scale,
                                      output->w / scale, output->h / scale);
            if (reveal <= 0.0) {
              std::memcpy(newRow + x * 4, oldRow + x * 4, 4);
            } else if (reveal < 1.0) {
              for (int channel = 0; channel < 4; ++channel) {
                const auto offset = x * 4 + channel;
                newRow[offset] = Uint8(
                    std::clamp(std::lround(oldRow[offset] * (1.0 - reveal) +
                                           newRow[offset] * reveal),
                               0L, 255L));
              }
            }
          }
        }
      } else {
        auto mixed = std::move(old);
        blit(*output, *mixed, nullptr, nullptr, int(255.0 * progress));
        output = std::move(mixed);
      }
    } else
      transitionOld.reset();
    bool changed = !canvasPixels || canvasPixels->w != output->w ||
                   canvasPixels->h != output->h;
    for (int y = 0; !changed && y < output->h; ++y)
      changed =
          std::memcmp(static_cast<Uint8 *>(canvasPixels->pixels) +
                          y * canvasPixels->pitch,
                      static_cast<Uint8 *>(output->pixels) + y * output->pitch,
                      output->w * 4) != 0;
    if (changed) {
      if (canvasTexture &&
          (canvasTexture->w != output->w || canvasTexture->h != output->h))
        canvasTexture.reset();
      if (!canvasTexture)
        canvasTexture.reset(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              output->w, output->h));
      if (!canvasTexture || !SDL_UpdateTexture(canvasTexture.get(), nullptr,
                                               output->pixels, output->pitch))
        throw std::runtime_error(SDL_GetError());
      canvasPixels = std::move(output);
    }
    ++renderStats.softwareEffectFrames;
  }
  std::string sceneFingerprint() {
    std::ostringstream key;
    key << dcGeneration << ' ' << dcImage(0).get();
    for (const auto &[id, o] : fg) {
      if (!o.visible || !o.text.empty())
        continue;
      key << ' ' << id << ' ' << std::quoted(o.file) << ' '
          << std::quoted(o.text) << ' ' << o.x << ' ' << o.y << ' ' << o.alpha
          << ' ' << o.width << ' ' << o.height << ' ' << int(o.colour.r) << ' '
          << int(o.colour.g) << ' ' << int(o.colour.b) << ' ' << int(o.colour.a)
          << ' ' << o.clipped << ' ' << o.clip.x << ' ' << o.clip.y << ' '
          << o.clip.w << ' ' << o.clip.h;
      for (const auto &adjustment : o.adjustments)
        key << ' ' << adjustment.x << ' ' << adjustment.y;
      if (!o.file.empty()) {
        const auto count = graphic(o.file).image.regions.size();
        key << ' ' << o.currentPattern(std::max(size_t(1), count));
      }
    }
    const auto now = SDL_GetTicks();
    if (transitionOld && transitionMs > 0 &&
        now - transitionStart < Uint64(transitionMs)) {
      const auto elapsed =
          std::min<Uint64>(now - transitionStart, transitionMs);
      const auto step =
          transitionStyle == 4 || transitionStyle == 5
              ? (elapsed * 64 + transitionMs - 1) / transitionMs
          : isScrollTransitionStyle(transitionStyle)
              ? elapsed * std::max(canvasW, canvasH) / transitionMs
              : elapsed * 255 / transitionMs;
      key << " transition " << transitionStart << ' ' << transitionStyle << ' '
          << step;
    }
    if (zoom) {
      const auto elapsed = double(now - zoom->start);
      const auto crop = zoom->animation.sample(elapsed);
      key << " zoom " << zoom->start << ' ' << zoom->image.get() << ' '
          << zoom->destinationRect.x << ' ' << zoom->destinationRect.y << ' '
          << zoom->destinationRect.w << ' ' << zoom->destinationRect.h << ' '
          << std::lround(crop.x) << ' ' << std::lround(crop.y) << ' '
          << std::lround(crop.scaleX) << ' ' << std::lround(crop.scaleY) << ' '
          << (elapsed >= zoom->animation.duration());
    }
    if (flashDuration > 0)
      key << " flash " << flashStart << ' ' << int(flashColour.r) << ' '
          << int(flashColour.g) << ' ' << int(flashColour.b) << ' '
          << std::min<Uint64>(16, (now - flashStart) * 16 / flashDuration);
    return key.str();
  }
  void drawScriptText(RendererRef r, const SDL_FRect &scene) {
    if (!textbox || !textbox->font)
      return;
    SDL_Rect oldClip{};
    const bool clipped = SDL_RenderClipEnabled(&r);
    SDL_GetRenderClipRect(&r, &oldClip);
    struct RestoreClip {
      RendererRef renderer;
      SDL_Rect clip;
      bool enabled;
      ~RestoreClip() {
        SDL_SetRenderClipRect(&renderer, enabled ? &clip : nullptr);
      }
    } restore{r, oldClip, clipped};
    for (const auto &[id, o] : fg) {
      if (!o.visible || o.text.empty())
        continue;
      const auto decoded =
          decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
      auto &texture = objectTextTextures[{decoded, 1}];
      if (!texture) {
        SurfacePtr pixels(TTF_RenderText_Blended(textbox->font.get(),
                                                 decoded.data(), decoded.size(),
                                                 {255, 255, 255, 255}));
        if (!pixels)
          throw std::runtime_error(SDL_GetError());
        texture.reset(SDL_CreateTextureFromSurface(&r, pixels.get()));
        if (!texture)
          throw std::runtime_error(SDL_GetError());
        ++renderStats.textureUploads;
      }
      SDL_FRect area{scene.x + o.x, scene.y + o.y, float(texture->w),
                     float(texture->h)};
      for (const auto &adjustment : o.adjustments) {
        area.x += adjustment.x;
        area.y += adjustment.y;
      }
      SDL_Rect clip = oldClip;
      if (o.clipped) {
        clip = {int(scene.x) + o.clip.x, int(scene.y) + o.clip.y, o.clip.w,
                o.clip.h};
        if (clipped)
          SDL_GetRectIntersection(&oldClip, &clip, &clip);
      }
      if (!SDL_SetRenderClipRect(&r, o.clipped || clipped ? &clip : nullptr) ||
          !SDL_SetTextureAlphaMod(texture.get(),
                                  Uint8(std::clamp(o.alpha, 0, 255))) ||
          !SDL_RenderTexture(&r, texture.get(), nullptr, &area))
        throw std::runtime_error(SDL_GetError());
    }
  }
  void draw(RendererRef r) {
    refreshImageUpscaling();
    const bool softwareEffect =
        transitionOld &&
        (transitionStyle == 10 || transitionStyle == 120 ||
         isScrollTransitionStyle(transitionStyle) ||
         ((transitionStyle == 4 || transitionStyle == 5) && !prepareDither(r)));
    const bool upscale = imageUpscaling.enabled();
    const auto key = sceneFingerprint();
    if (!upscale || !filteredScene || sceneKey != key) {
      if (softwareEffect)
        drawSoftwareEffects(r);
      else
        drawHardware(r, 1);
      if (upscale) {
        auto *native =
            softwareEffect ? canvasTexture.get() : compositionTexture.get();
        filteredScene =
            engine->GetUpscaler().scaled(*native, imageUpscaling.factor);
        if (!configureSceneTexture(filteredScene.get()))
          throw std::runtime_error(std::string("REALLIVE filtered scene: ") +
                                   SDL_GetError());
        ++renderStats.filteredSceneFrames;
        sceneBackground = dcImage(0);
        sceneKey = key;
      }
    }
    SDL_FRect destination{0, 0, (float)canvasW, (float)canvasH};
    auto elapsed = SDL_GetTicks() - shakeStart;
    for (const auto &step : shake) {
      if (elapsed < Uint64(step[2])) {
        destination.x = step[0];
        destination.y = step[1];
        break;
      }
      elapsed -= step[2];
    }
    auto *display = upscale          ? filteredScene.get()
                    : softwareEffect ? canvasTexture.get()
                                     : compositionTexture.get();
    if (!SDL_RenderTexture(&r, display, nullptr, &destination))
      throw std::runtime_error(SDL_GetError());
    drawScriptText(r, destination);
    if (movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, {0, 0, (float)canvasW, (float)canvasH});
    }
    for (const auto &[id, field] : editFields) {
      SDL_Rect oldClip{};
      const bool clipped = SDL_RenderClipEnabled(&r);
      SDL_GetRenderClipRect(&r, &oldClip);
      SDL_Rect clip{int(field.area.x), int(field.area.y), int(field.area.w),
                    int(field.area.h)};
      if (clipped)
        SDL_GetRectIntersection(&oldClip, &clip, &clip);
      SDL_SetRenderClipRect(&r, &clip);
      engine->GetUI().panel(r, field.area, field.background);
      engine->GetUI().text(r, field.text + (focusedEdit == id ? "|" : ""),
                           field.area.x, field.area.y, field.area.w,
                           field.color);
      SDL_SetRenderClipRect(&r, clipped ? &oldClip : nullptr);
    }
    if (!error.empty()) {
      engine->GetUI().panel(r, {12, 15, 616, 155});
      engine->GetUI().text(r, error, 24, 26, 590);
    }
  }
  void playClip(PcmPlayer &p, Kotonoha::AudioClip a, const std::string &id,
                const std::string &channel, bool loop = false,
                int64_t start = 0, int64_t end = 0, int64_t loopStart = 0,
                int64_t offsetMilliseconds = 0) {
    SDL_AudioSpec target{SDL_AUDIO_S16, a.spec.channels, a.spec.freq};
    Uint8 *converted = nullptr;
    int size = 0;
    if (!SDL_ConvertAudioSamples(&a.spec, a.data.data(), int(a.data.size()),
                                 &target, &converted, &size))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::SDLMemoryPtr<Uint8> convertedOwner(converted);
    PcmPlayer::Clip clip;
    clip.samples.resize(size / 2);
    std::memcpy(clip.samples.data(), converted, size);

    clip.channels = target.channels;
    clip.rate = target.freq;
    auto count = clip.samples.size() / clip.channels;
    if (end > 0 && uint64_t(end) < count)
      clip.samples.resize(size_t(end) * clip.channels);
    if (&p == &bgm) {
      // Track changes preserve the script's gain, but cancel an old fade.
      fadeDuration = fadeElapsed = 0;
      fadeStops = false;
      bgmScriptPaused = false;
      musicID = id;
      musicLoop = loop;
      musicEnd = end;
      musicLoopStart = loopStart;
      musicFrames = clip.samples.size() / clip.channels;
      musicRate = target.freq;
    }
    clip.loop = loop;
    clip.startFrame = bgmSeekFrame(start, offsetMilliseconds, target.freq,
                                   clip.samples.size() / clip.channels);
    clip.loopStart = std::min<size_t>(std::max<int64_t>(0, loopStart),
                                      clip.samples.size() / clip.channels);
    if (!p.Play(engine->GetAudio().channel(channel), std::move(clip)))
      throw std::runtime_error(SDL_GetError());
    if (&p == &bgm)
      p.SetGain(bgmGain);
    else if (&p == &voice)
      p.SetGain(voiceLevels.gain(0));
  }
  void play(PcmPlayer &p, const std::string &id, const std::string &channel,
            bool loop = false, int64_t start = 0, int64_t end = 0,
            int64_t loopStart = 0, int64_t offsetMilliseconds = 0) {
    playClip(p, assets->loadAudio(id), id, channel, loop, start, end, loopStart,
             offsetMilliseconds);
  }
  void setSoundVolume(int channel, int nativeVolume, int duration = 0,
                      bool stopAtEnd = false) {
    soundLevels.set(channel, nativeVolume, duration, stopAtEnd);
    auto found = sounds.find(channel);
    if (duration <= 0 && found != sounds.end() && found->second) {
      if (stopAtEnd)
        found->second->Stop();
      else
        found->second->SetGain(soundLevels.gain(channel));
    }
  }
  void setVoiceVolume(int nativeVolume, int duration = 0) {
    voiceLevels.set(0, nativeVolume, duration);
    if (duration <= 0)
      voice.SetGain(voiceLevels.gain(0));
  }
  void setBgmVolume(int nativeVolume, int duration = 0,
                    bool stopAtSilence = false) {
    fadeFrom = bgmGain;
    fadeTo = nativeBgmToGain(nativeVolume);
    fadeElapsed = 0;
    fadeDuration = std::max(0, duration);
    fadeStops = stopAtSilence && fadeTo == 0.0f;
    if (fadeDuration == 0) {
      bgmGain = fadeTo;
      bgm.SetGain(bgmGain);
      if (fadeStops) {
        bgm.Stop();
        musicID.clear();
      }
    }
  }
  void stopBgm() {
    bgm.Stop();
    // A fade-out leaves bgmGain at zero; without this reset the next
    // explicitly requested track would inherit silence because playClip()
    // reapplies the stored gain.
    bgmGain = bgmGainAfterStop(bgmGain);
    fadeFrom = fadeTo = 1.0f;
    fadeElapsed = fadeDuration = 0;
    fadeStops = false;
    bgmScriptPaused = false;
    musicID.clear();
  }
  int value(const libreallive::CommandElement &c, int n, int fallback = 0) {
    return n < c.GetParamCount() ? IntegerParameter(vm.machine(), c, n)
                                 : fallback;
  }
  std::string str(const libreallive::CommandElement &c, int n) {
    return StringParameter(vm.machine(), c, n);
  }
  void assign(const libreallive::CommandElement &c, int n, int v) {
    try {
      auto param = Parameter(c, n);
      if (!param.IsLValue()) {
        return;
      }
      param.SetIntegerValue(vm.machine(), v);
    } catch (const libreallive::Error &e) {
      // Silently ignore assignment errors to invalid lvalues
    }
  }
  void wait(int ms) { deadline = SDL_GetTicks() + std::max(0, ms); }
  int frameValue(int n) {
    auto i = frames.find(n);
    if (i == frames.end())
      return 0;
    auto &f = i->second;
    if (f.loop) {
      if (f.duration <= 0)
        return f.to;
      const int distance = std::abs(f.to - f.from);
      if (distance == 0)
        return f.from;
      const Uint64 now = SDL_GetTicks();
      const double interval = double(f.duration) / distance;
      const double elapsed = double(now - f.lastCheck);
      f.value += (f.to >= f.from ? 1 : -1) * elapsed / interval;
      if ((f.to >= f.from && f.value >= f.to) ||
          (f.to < f.from && f.value <= f.to))
        f.value = f.from;
      f.lastCheck = now;
      return int(f.value);
    }
    double t = f.duration > 0
                   ? std::min(1., double(SDL_GetTicks() - f.start) / f.duration)
                   : 1.;
    return int(f.from + (f.to - f.from) * t);
  }
  bool command(const libreallive::CommandElement &c);

  // RealLive sys 1:4:207 selects a configured mouse cursor image.
  // Keep the requested number for sys 1:4:206 even when no image exists.
  void selectMouseCursor(int number) {
    if (mouseCursorApplied && mouseCursorNumber == number)
      return;
    mouseCursorNumber = number;
    if (!engine)
      return;
    SDL_Cursor *next = nullptr;
    if (number >= 0 && number <= 999) {
      std::ostringstream key;
      key << "MOUSE_CURSOR." << std::setw(3) << std::setfill('0') << number;
      const auto name = assets->settings().text(key.str() + ".NAME");
      if (!name.empty()) {
        const auto id = assets->graphic(name);
        if (!id.empty()) {
          auto atlas = assets->loadSurface(id);
          auto frame = mouseCursorFrame(atlas.get());
          if (frame.surface)
            next = SDL_CreateColorCursor(frame.surface.get(), frame.hotspotX,
                                         frame.hotspotY);
        }
      }
    }
    // Always switch before releasing the previous cursor. Missing images
    // use the SDL default without changing cursor visibility (204/205).
    SDL_SetCursor(next ? next : SDL_GetDefaultCursor());
    SDL_Cursor *old = mouseCursor;
    mouseCursor = next;
    if (old)
      SDL_DestroyCursor(old);
    mouseCursorApplied = true;
  }
  std::filesystem::path readHistoryPath() const {
    // REGNAME is stable when an installation is relocated; use its actual
    // data mount only for editions that do not declare a registry identity.
    const auto regname = assets->settings().text("REGNAME");
    std::string identity =
        regname.empty()
            ? "mount:" + assets->directory().lexically_normal().generic_string()
            : "reg:" + regname;
    if (identity == "mount:")
      throw std::runtime_error("REALLIVE missing read-history identity");
    return Kotonoha::saveDirectory() / readHistoryFilename(identity);
  }
  void loadReadHistory() {
    try {
      const auto path = readHistoryPath();
      std::error_code ec;
      if (!std::filesystem::exists(path, ec))
        return;
      if (ec || !std::filesystem::is_regular_file(path, ec) ||
          std::filesystem::file_size(path, ec) > 32u * 1024u * 1024u || ec)
        throw std::runtime_error("Invalid REALLIVE read-history file");
      std::ifstream input(path, std::ios::binary);
      if (!input)
        throw std::runtime_error("Cannot open REALLIVE read-history file");
      const auto restored = readReadHistory(input);
      readPages.insert(restored.begin(), restored.end());
    } catch (const std::exception &error) {
      // A damaged journal must not prevent running the original game.
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "REALLIVE read-history load: %s", error.what());
    }
  }
  void persistReadHistory() const {
    if (readPages.empty())
      return;
    try {
      std::ostringstream out;
      writeReadHistory(out, readPages);
      Kotonoha::writeTextFileAtomically(readHistoryPath().string(), out.str());
    } catch (const std::exception &error) {
      // Retain the session's read flags even when the filesystem is readonly.
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "REALLIVE read-history save: %s", error.what());
    }
  }
  void bind(Engine &e) {
    engine = &e;
    if (!readHistoryLoaded) {
      readHistoryLoaded = true;
      loadReadHistory();
    }
    selectMouseCursor(mouseCursorNumber);
    if (!stage) {
      stage = std::make_unique<Stage>(e);
      textbox = std::make_unique<Text>(e, *assets, canvasW, canvasH);
      textbox->autoActive = [this] { return autoMode; };
      textbox->skipActive = [this] { return skipMode || skipHeld; };
      textbox->controls.buttons = {
          {{584, 349, 31, 7}, [this] { textbox->concealed = true; }},
          {{616, 349, 12, 38}, [this] { GetVNBacklogRuntime(*engine).open(); }},
          {{616, 388, 12, 38}, [this] { advanceText(); }},
          {{616, 427, 12, 16},
           [this] {
             autoMode = !autoMode;
             if (autoMode)
               skipMode = false;
             autoDeadline = 0;
           }},
          {{616, 444, 12, 16}, [this] {
             skipMode = !skipMode;
             if (skipMode)
               autoMode = false;
           }}};
      textbox->updateLayout(canvasW, canvasH);
      choice = std::make_unique<RealLiveSelection>(e, *assets);
      stage->draw = [this](auto &r) { draw(r); };
      stage->input = [this](auto &e) { return event(e); };
      movie = std::make_unique<MediaPlayer>(e.GetAudio());
    }
    runtime.AddScene(stage.get());
    runtime.AddScene(textbox.get());
    runtime.AddScene(choice.get());
    runtime.AddScene(&GetVNBacklogRuntime(e));
    auto &m = GetInGameMenuRuntime(e);
    m.SetOnSave([this] { openSaves(false); });
    m.SetOnLoad([this] { openSaves(true); });
    m.SetOnReturnToMenu([this] {
      GetInGameMenuRuntime(*engine).close();
      vm.jump(titleScenario);
      selectMouseCursor(assets->settings().integer("MOUSE_CURSOR", -1));
      autoMode = skipMode = skipHeld = false;
      input.resetSkip();
      autoDeadline = 0;
      textbox->controls.reset();
      textbox->concealed = false;
      fg.clear();
      bg.clear();
      dc.clear();
      transitionOld.reset();
      zoom.reset();
      shake.clear();
      flashDuration = 0;
      editFields.clear();
      focusedEdit = -1;
      SDL_StopTextInput(&engine->GetWindow());
      deadline = 0;
      choice->hide();
      movie->stop();
      audioWait.clear();
      voice.Stop();
      bgm.Stop();
      musicID.clear();
      fadeDuration = fadeElapsed = 0;
      bgmGain = 1;
      bgmScriptPaused = false;
      soundLevels.clear();
      voiceLevels.clear();
      sounds.clear();
      runtime.SetState(GameState::Menu);
      titleInput.reset();
      pause = false;
      error.clear();
      dialogue.clear();
      textbox->hide();
    });
    runtime.AddScene(&m);
  }
  void openSaves(bool load) {
    if (!saves)
      saves = std::make_unique<VNSaveMenu>(*engine);
    std::string dir = Kotonoha::saveDirectory().string();
    auto sanitize = [](std::string name) {
      for (char &ch : name) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|') {
          ch = '_';
        }
      }
      while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
      }
      return name.empty() ? "Save" : name;
    };
    saves->open(dir, load, load ? "" : sanitize(saveName));
    runtime.RemoveScene(saves.get());
    runtime.AddScene(saves.get());
  }
  void advanceText(bool manual = true, bool preserveReadSkip = false) {
    if (!pause)
      return;
    if (!textbox->isDialogueComplete())
      textbox->completeDialogue();
    else {
      if (readPages.insert(currentPage).second)
        persistReadHistory();
      pause = false;
      autoDeadline = 0;
      updateAdvanceModes(manual, preserveReadSkip, autoMode, skipMode);
      if (pauseKind != 205) {
        dialogue.clear();
        speakerName.clear();
        currentDialogueLine = 0;
        textbox->hide();
        voice.Stop();
      }
    }
  }
  // The title front-end idles after the native refresh command, so only then
  // is the script about to poll the mouse again. Different RealLive titles
  // stop at different instruction indices (AIR at 860, planetarian at 587),
  // therefore readiness is tracked from the refresh command itself.
  bool titleMenu() const {
    return titleReady && vm.scenario() == titleScenario &&
           runtime.GetState() == GameState::Menu && !pause &&
           !choice->isActive() && error.empty();
  }
  bool syscomAvailable() const {
    return std::any_of(syscomEntries.begin(), syscomEntries.end(),
                       [](Uint8 state) { return state != 0; });
  }
  // Other loops in the title scenario poll intA and must stay pointer-only; the
  // title menu is the poll that writes the button register intF[1003]. The
  // title script enumerates its native clickable regions through 1:84 before
  // every menu poll. Record those object ids and derive keyboard / gamepad
  // targets from their live positions, so each RealLive release keeps its own
  // layout instead of sharing AIR's hardcoded coordinates.
  void installTitleTargets() {
    std::vector<TitleInput::Target> targets;
    for (int id : titleHitObjects) {
      auto found = fg.find(id);
      if (found == fg.end())
        continue;
      auto &o = found->second;
      if (!o.visible)
        continue;
      int w = o.width, h = o.height;
      if (!o.file.empty()) {
        auto &g = graphic(o.file);
        w = g.image.sourceWidth;
        h = g.image.sourceHeight;
        if (!g.image.regions.empty()) {
          auto &r =
              g.image.regions.at(o.currentPattern(g.image.regions.size()));
          w = r.x2 - r.x1 + 1;
          h = r.y2 - r.y1 + 1;
        }
      }
      if (w <= 0 || h <= 0)
        continue;
      targets.push_back({o.x + w / 2, o.y + h / 2});
    }
    titleInput.setTargets(std::move(targets));
  }
  bool titlePoll(const libreallive::CommandElement &c) {
    if (vm.scenario() != titleScenario || c.GetParamCount() < 3)
      return false;
    // The menu poll stores the button state in an intF register. AIR uses
    // intF[1003], CLANNAD uses intF[1202] and planetarian intF[1003]; the
    // temporary intA polls elsewhere in the title script must not consume the
    // synthetic press.
    const auto reference =
        Parameter(c, 2).GetIntegerReferenceIterator(vm.machine());
    return reference.bank() == libreallive::INTF_LOCATION;
  }
  bool event(EventRef e) {
    if (titleMenu()) {
      if (e.type == SDL_EVENT_MOUSE_MOTION) {
        mouseX = e.motion.x;
        mouseY = e.motion.y;
        titleInput.hover(int(mouseX), int(mouseY));
        return true;
      }
      if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           e.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
          e.button.button == SDL_BUTTON_LEFT) {
        mouseX = e.button.x;
        mouseY = e.button.y;
        titleInput.pointer(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, int(mouseX),
                           int(mouseY));
        return true;
      }
      if (titleInput.handle(e)) {
        mouseX = titleInput.mouseX();
        mouseY = titleInput.mouseY();
        return true;
      }
    }
    if (syscomAvailable() && pause && e.type == SDL_EVENT_KEY_DOWN &&
        !e.key.repeat && e.key.key == SDLK_ESCAPE) {
      GetInGameMenuRuntime(*engine).open();
      return true;
    }
    if (pause && ShouldOpenBacklog(*engine, e)) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }
    // Controller Back/left shoulder maps to PAGEUP (see MapControllerEvent);
    // the REALLIVE text box overrides the shared VNTextBox handler, so accept
    // it here as well.
    if (pause && textbox->isVisible() && e.type == SDL_EVENT_KEY_DOWN &&
        !e.key.repeat && e.key.key == SDLK_PAGEUP) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }

    if (pause && textbox->isVisible()) {
      if (textbox->concealed) {
        const bool touchRelease = e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                                  e.button.which == SDL_TOUCH_MOUSEID;
        const bool mousePress = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                                e.button.which != SDL_TOUCH_MOUSEID;
        if (((touchRelease || mousePress) &&
             e.button.button == SDL_BUTTON_LEFT && e.button.x >= 0 &&
             e.button.y >= 0) ||
            (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
             (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN ||
              e.key.key == SDLK_KP_ENTER))) {
          textbox->concealed = false;
          return true;
        }
      } else if (textbox->handleEvent(e))
        return true;
      if (focusedEdit < 0 && !engine->isStoryPaused() &&
          e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          e.key.key == SDLK_A) {
        autoMode = !autoMode;
        if (autoMode)
          skipMode = false;
        autoDeadline = 0;
        return true;
      }
      if (focusedEdit < 0 && !engine->isStoryPaused() &&
          e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          e.key.key == SDLK_S) {
        skipMode = !skipMode;
        if (skipMode)
          autoMode = false;
        return true;
      }
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        e.button.button == SDL_BUTTON_LEFT) {
      SDL_FPoint point{e.button.x, e.button.y};
      for (const auto &[id, field] : editFields)
        if (SDL_PointInRectFloat(&point, &field.area)) {
          focusedEdit = id;
          SDL_SetTextInputArea(&engine->GetWindow(), nullptr, 0);
          SDL_StartTextInput(&engine->GetWindow());
          return true;
        }
    }
    auto edit = editFields.find(focusedEdit);
    if (edit != editFields.end()) {
      if (e.type == SDL_EVENT_TEXT_INPUT) {
        if (edit->second.text.size() + std::strlen(e.text.text) <= 255)
          edit->second.text += e.text.text;
        return true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_BACKSPACE) {
        eraseLastCharacter(edit->second.text);
        return true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB) {
        if (++edit == editFields.end())
          edit = editFields.begin();
        focusedEdit = edit->first;
        return true;
      }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
      mouseX = e.motion.x;
      mouseY = e.motion.y;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      mouseX = e.button.x;
      mouseY = e.button.y;
      auto &b = e.button.button == SDL_BUTTON_LEFT ? button1 : button2;
      b = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 2;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
      if (syscomAvailable())
        GetInGameMenuRuntime(*engine).toggle();
      return true;
    }
    // Do not read e.button on keyboard events. Engine touch cancellation
    // releases the synthetic pointer at (-1,-1); it is not a valid tap.
    const bool pointer = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                         e.type == SDL_EVENT_MOUSE_BUTTON_UP;
    const bool leftClick = pointer && e.button.button == SDL_BUTTON_LEFT &&
                           e.button.x >= 0 && e.button.y >= 0;
    const bool touch = leftClick && e.button.which == SDL_TOUCH_MOUSEID;
    const bool advance =
        (leftClick && e.type == ((pauseKind == 205 || touch)
                                     ? SDL_EVENT_MOUSE_BUTTON_UP
                                     : SDL_EVENT_MOUSE_BUTTON_DOWN)) ||
        (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
         (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER ||
          e.key.key == SDLK_SPACE));
    if (advance && pause) {
      advanceText();
      return true;
    }
    return false;
  }
};
bool RealLiveRuntime::Impl::command(const libreallive::CommandElement &c) {
  const int type = c.modtype(), m = c.module(), op = c.opcode(),
            over = c.overload();
  auto &machine = vm.machine();
  auto audioAsset = [this](const std::string &base) {
    if (assets->contains(base))
      return base;
    for (const char *extension : {".WAV", ".OGG", ".NWA"})
      if (assets->contains(base + extension))
        return base + extension;
    return std::string();
  };
  if (type == 0 && m == 4)
    return true;
  if (type == 0 && m == 3) {
    switch (op) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 16:
    case 17:
    case 200:
    case 205: {
      pauseKind = op;
      currentPage = {vm.scenario(), vm.index()};
      currentRead = readPages.count(currentPage) != 0;
      if (!currentRead)
        skipMode = false;
      autoDeadline = 0;
      pause = true;
      auto translatedSpeaker =
          engine->GetTranslation().text(vm.scenario(), speakerName);
      auto translatedDialogue =
          engine->GetTranslation().text(vm.scenario(), dialogue);
      if (translatedDialogue == dialogue && !dialogue.empty() &&
          dialogue.front() == ' ')
        translatedDialogue =
            engine->GetTranslation().text(vm.scenario(), dialogue.substr(1));

      std::string finalSpeaker, finalDialogue;
      processSpeakerAndDialogue(translatedSpeaker, translatedDialogue,
                                finalSpeaker, finalDialogue);
      textbox->concealed =
          (op == 205 && finalDialogue.empty() && finalSpeaker.empty());
      textbox->setDialogue(finalSpeaker, finalDialogue);
      textbox->show();
      return true;
    }
    case 120:
    case 151:
    case 152:
    case 161:
    case 162:
      dialogue.clear();
      currentDialogueLine = 0;
      textbox->hide();
      return true;
    case 201:
      dialogue += '\n';
      return true;
    case 210:
      dialogue.clear();
      currentDialogueLine = 0;
      if (textbox)
        textbox->clearLocate();
      return true;
    case 101:
      vm.currentWindow = value(c, 0);
      if (textbox)
        textbox->setWindow(vm.currentWindow);
      return true;
    case 300:
      if (textbox)
        textbox->setLocate(value(c, 0), value(c, 1));
      return true;
    case 102:
    case 103:
    case 104:
    case 105:
    case 301:
    case 310:
    case 311:
    case 330:
      return true;
    }
  }
  if (type == 0 && m == 2 && op >= 0 && op <= 3) {
    // Ordinary RealLive selections are SelectElements. The distinct
    // select_objbtn opcode 4 takes an object group, not a SelectElement.
    auto *s = dynamic_cast<const libreallive::SelectElement *>(&c);
    if (!s)
      throw std::runtime_error("Invalid REALLIVE select bytecode");
    std::vector<ChoiceOption> options;
    int id = 0;
    for (const auto &p : s->raw_params()) {
      SelectOptionState state;
      for (const auto &condition : p.cond_parsed) {
        bool value = false;
        const bool hasCondition = !condition.condition.empty();
        if (hasCondition) {
          const char *expr = condition.condition.c_str();
          value =
              libreallive::GetExpression(expr).GetIntegerValue(machine) != 0;
        }
        std::optional<int> colour;
        if (condition.effect == libreallive::SelectElement::OPTION_TITLE &&
            (!hasCondition || value) && !condition.effect_argument.empty()) {
          const char *expr = condition.effect_argument.c_str();
          colour = libreallive::GetExpression(expr).GetIntegerValue(machine);
        }
        applySelectCondition(state, condition.effect, hasCondition, value,
                             colour);
      }
      if (state.visible) {
        const auto raw = libreallive::EvaluatePRINT(machine, p.text);
        const auto cached = selectionLabels.find(raw);
        const auto decoded =
            cached == selectionLabels.end()
                ? decodeText(raw, archive.Scenario(vm.scenario()).encoding())
                : cached->second;
        ChoiceOption option{
            engine->GetTranslation().text(vm.scenario(), decoded), id};
        option.enabled = state.enabled;
        option.colourIndex = state.colourIndex;
        options.push_back(std::move(option));
      }
      ++id;
    }
    textbox->hide();
    // select_w embeds a text-window override; -1 uses DEFAULT_SEL_WINDOW.
    const int requestedWindow =
        op == 0 ? s->GetWindowExpression().GetIntegerValue(machine) : -1;
    const int activeWindow =
        selectWindowIndex(selectionWindow, requestedWindow);
    static_cast<RealLiveSelection &>(*choice).configure(activeWindow);
    if (options.empty()) {
      // A script may intentionally hide every branch until its conditions
      // change. Do not crash the engine or invent a selectable route.
      throw std::runtime_error("REALLIVE selection has no visible options");
    }
    choice->setQuestion("", options, [this](int n, const std::string &) {
      vm.machine().set_store_register(n);
      choice->hide();
    });
    // A batch of rlBabel translated choices belongs to exactly one select.
    selectionLabels.clear();
    return true;
  }
  if (type == 1 && m == 4) {
    switch (op) {
    case 0:
      saveName =
          decodeText(str(c, 0), archive.Scenario(vm.scenario()).encoding());
      SDL_SetWindowTitle(
          &engine->GetWindow(),
          ("REALLIVE — " +
           decodeText(str(c, 0), archive.Scenario(vm.scenario()).encoding()))
              .c_str());
      return true;
    case 100:
    case 101:
      wait(value(c, 0));
      return true;
    case 110:
    case 120:
      timers[(op == 120 ? 256 : 0) + value(c, 0)] = SDL_GetTicks();
      return true;
    case 112:
    case 114:
    case 122:
    case 124:
      machine.set_store_register(
          int(SDL_GetTicks() - timers[(op >= 120 ? 256 : 0) + value(c, 0)]));
      return true;
    case 116:
    case 126:
      timers[(op == 126 ? 256 : 0) + value(c, 1)] =
          SDL_GetTicks() - value(c, 0);
      return true;
    case 111:
    case 121: {
      const int target = value(c, 0);
      if (target < 0)
        throw std::runtime_error("Negative REALLIVE timer wait target");
      const int timer = value(c, 1) + (op == 121 ? 256 : 0);
      const Uint64 elapsed = SDL_GetTicks() - timers[timer];
      if (Uint64(target) > elapsed)
        wait(int(Uint64(target) - elapsed));
      return true;
    }
    case 130:
      button1 = button2 = 0;
      return true;
    case 131:
      machine.set_store_register(button1 != 0 ? 1 : 0);
      return true;
    case 132:
      machine.set_store_register(button2 != 0 ? 1 : 0);
      return true;
    case 134:
      machine.set_store_register(1);
      return true;
    case 133: {
      // Keyboard/gamepad title input is injected at the native poll so it
      // cannot be lost to the script's own 1:4:130 register clear.
      const int synthetic = titlePoll(c) ? titleInput.poll() : -1;
      assign(c, 0, int(mouseX));
      assign(c, 1, int(mouseY));
      assign(c, 2, synthetic >= 0 ? synthetic : button1);
      assign(c, 3, button2);
      button1 = button1 == 2 ? 0 : button1;
      button2 = button2 == 2 ? 0 : button2;
      return true;
    }
    case 200:
      machine.set_store_register(int(mouseX));
      return true;
    case 201:
      machine.set_store_register(int(mouseY));
      return true;
    case 202:
      assign(c, 0, int(mouseX));
      assign(c, 1, int(mouseY));
      return true;
    case 204:
      SDL_ShowCursor();
      return true;
    case 205:
      SDL_HideCursor();
      return true;
    case 206:
      machine.set_store_register(mouseCursorNumber);
      return true;
    case 207:
      selectMouseCursor(value(c, 0));
      return true;
    case 210:
      machine.set_store_register(1);
      return true;
    case 211:
      if (c.GetParamCount() > 0 && value(c, 0) == 0)
        SDL_HideCursor();
      else
        SDL_ShowCursor();
      return true;
    case 212:
    case 214:
    case 217:
    case 218:
    case 219:
      machine.set_store_register(mouseCursorNumber >= 0 ? mouseCursorNumber
                                                        : 0);
      return true;
    case 215:
    case 216: {
      if (c.GetParamCount() >= 2) {
        const int synthetic = titlePoll(c) ? titleInput.poll() : -1;
        assign(c, 0, int(mouseX));
        assign(c, 1, int(mouseY));
        if (c.GetParamCount() >= 3)
          assign(c, 2, synthetic >= 0 ? synthetic : button1);
        if (c.GetParamCount() >= 4)
          assign(c, 3, button2);
        if (c.GetParamCount() >= 5)
          assign(c, 4, 0);
        if (c.GetParamCount() >= 3) {
          button1 = button1 == 2 ? 0 : button1;
          button2 = button2 == 2 ? 0 : button2;
        }
      }
      machine.set_store_register(mouseCursorNumber >= 0 ? mouseCursorNumber
                                                        : 0);
      return true;
    }
    case 213:
      if (c.GetParamCount() >= 2) {
        mouseX = value(c, 0);
        mouseY = value(c, 1);
      }
      return true;
    case 220:
    case 221:
    case 222:
    case 223:
    case 224:
    case 225:
    case 226:
    case 227:
    case 228:
    case 229:
    case 230:
    case 231:
    case 232:
    case 233:
    case 234:
    case 235:
    case 236:
    case 237:
    case 238:
    case 239:
    case 240:
    case 241:
    case 242:
    case 243:
    case 244:
    case 245:
    case 246:
    case 247:
    case 248:
    case 249:
      machine.set_store_register(0);
      return true;
    case 330:
      machine.set_store_register(skipMode ? 1 : 0);
      return true;
    case 331:
      skipMode = value(c, 0) != 0;
      return true;
    case 332:
      machine.set_store_register(skipMode);
      return true;
    case 333:
    case 334:
      skipMode = op == 333;
      return true;
    case 350:
      machine.set_store_register(ctrlSkipEnabled);
      return true;
    case 353:
    case 354:
      machine.set_store_register(0);
      return true;
    case 351:
    case 352:
      ctrlSkipEnabled = op == 351;
      return true;
    case 360:
    case 363:
    case 364:
      machine.set_store_register(autoMode ? 1 : 0);
      return true;
    case 361:
    case 362:
      autoMode = op == 361;
      return true;
    case 365:
      autoMode = value(c, 0) != 0;
      return true;
    case 366:
    case 368:
    case 369:
      machine.set_store_register(
          assets->settings().integer("AUTO_CHAR_TIME", 30));
      return true;
    case 367:
      return true;
    case 370:
    case 371:
    case 372:
    case 373:
    case 374:
    case 375:
    case 376:
    case 377:
    case 378:
    case 379:
    case 380:
    case 381:
    case 382:
    case 383:
    case 384:
    case 385:
    case 386:
    case 387:
    case 388:
    case 389:
      if (op % 2 == 0)
        machine.set_store_register(0);
      return true;
    case 410:
      for (int i = 0; i < c.GetParamCount(); ++i)
        assign(c, i, assets->settings().integer("WINDOW_ATTR", 0, i));
      return true;
    case 500:
    case 501:
    case 520:
    case 521: {
      int key = value(c, 0) + (op >= 520 ? 256 : 0);
      frames[key] = {value(c, 1), value(c, 2), value(c, 3), SDL_GetTicks()};
      frames[key].loop = (op == 501 || op == 521);
      frames[key].value = frames[key].from;
      frames[key].lastCheck = frames[key].start;
      return true;
    }
    case 510:
    case 530: {
      int key = value(c, 0) + (op == 530 ? 256 : 0);
      machine.set_store_register(frameValue(key));
      return true;
    }
    case 600:
    case 601:
    case 620: {
      for (int i = 0; i < c.GetParamCount(); ++i) {
        auto raw = c.GetParam(i);
        const char *cursor = raw.c_str();
        auto p = libreallive::GetComplexParam(cursor).GetContainedPieces();
        if (p.size() != 4)
          throw std::runtime_error("Invalid REALLIVE frame counter");
        int key = p[0].GetIntegerValue(machine) + (op == 620 ? 256 : 0);
        frames[key] = {p[1].GetIntegerValue(machine),
                       p[2].GetIntegerValue(machine),
                       p[3].GetIntegerValue(machine), SDL_GetTicks()};
        frames[key].loop = op == 601;
        frames[key].value = frames[key].from;
        frames[key].lastCheck = frames[key].start;
      }
      return true;
    }
    case 610:
    case 630: {
      bool active = false;
      for (int i = 0; i < c.GetParamCount(); ++i) {
        auto raw = c.GetParam(i);
        const char *cursor = raw.c_str();
        auto p = libreallive::GetComplexParam(cursor).GetContainedPieces();
        int key = p.at(0).GetIntegerValue(machine) + (op == 630 ? 256 : 0);
        p.at(1).SetIntegerValue(machine, frameValue(key));
        auto f = frames.find(key);
        if (f != frames.end()) {
          active |= f->second.loop && f->second.duration > 0;
          active |=
              !f->second.loop && f->second.duration > 0 &&
              SDL_GetTicks() - f->second.start < Uint64(f->second.duration);
        }
      }
      machine.set_store_register(active);
      return true;
    }
    case 3000:
    case 3001:
      openSaves(op == 3001);
      refreshRequested = true;
      return true;
    case 3107: {
      const int slot = value(c, 0);
      if (pause && error.empty() && (!choice || !choice->isActive())) {
        try {
          runtime.saveGame(slotSavePath(slot).string());
        } catch (...) {
        }
      }
      return true;
    }
    case 3109: {
      const int slot = value(c, 0);
      const auto p = slotSavePath(slot);
      std::error_code ec;
      if (std::filesystem::is_regular_file(p, ec)) {
        try {
          runtime.loadGame(p.string());
        } catch (...) {
        }
      }
      return true;
    }
    case 1200: {
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
      pause = true;
      return true;
    case 1201:
    case 1203:
      vm.jump(titleScenario);
      return true;
    case 1204:
      return true;
    case 1211:
    case 1212:
      if (c.GetParamCount() == 0) {
        syscomEntries.fill(op == 1211 ? 1 : 0);
        return true;
      }
      for (int i = 0; i < c.GetParamCount(); ++i) {
        const int entry = value(c, i);
        if (entry < 0 || entry >= int(syscomEntries.size()))
          throw std::out_of_range("REALLIVE system menu entry");
        syscomEntries[entry] = op == 1211 ? 1 : 0;
      }
      return true;
    case 1214: {
      const int entry = value(c, 0);
      machine.set_store_register(
          (entry >= 0 && entry < int(syscomEntries.size()))
              ? int(syscomEntries[entry])
              : 0);
      return true;
    }
    case 1215: {
      const int entry = value(c, 0);
      if (entry == 0 || entry == 1) {
        openSaves(entry == 1);
        refreshRequested = true;
      }
      machine.set_store_register(0);
      return true;
    }
    case 1218: {
      if (c.GetParamCount() > 0) {
        const int entry = value(c, 0);
        if (entry >= 0 && entry < int(syscomEntries.size()))
          syscomEntries[entry] = 0;
      }
      return true;
    }
    case 1409: {
      const int slot = value(c, 0);
      std::error_code ec;
      machine.set_store_register(
          std::filesystem::is_regular_file(slotSavePath(slot), ec) ? 1 : 0);
      return true;
    }
    case 1413: {
      const int slot = value(c, 0);
      const auto p = slotSavePath(slot);
      std::error_code ec;
      const bool exists = std::filesystem::is_regular_file(p, ec);
      for (int i = 1; i <= 8 && i < c.GetParamCount(); ++i)
        assign(c, i, 0);
      if (c.GetParamCount() >= 10) {
        try {
          auto param = Parameter(c, 9);
          if (param.IsLValue())
            param.SetStringValue(machine, "");
        } catch (...) {
        }
      }
      machine.set_store_register(exists ? 1 : 0);
      return true;
    }
    case 1414:
      machine.set_store_register(0);
      return true;
    case 1421: {
      int latestSlot = -1;
      std::filesystem::file_time_type latestTime{};
      std::error_code ec;
      const auto dir = Kotonoha::saveDirectory();
      if (std::filesystem::is_directory(dir, ec)) {
        for (int s = 0; s < 100; ++s) {
          auto p = slotSavePath(s);
          if (std::filesystem::is_regular_file(p, ec)) {
            auto t = std::filesystem::last_write_time(p, ec);
            if (latestSlot < 0 || t > latestTime) {
              latestSlot = s;
              latestTime = t;
            }
          }
        }
      }
      machine.set_store_register(latestSlot);
      return true;
    }
    case 301:
      button1 = button2 = 0;
      return true;
    case 302:
      machine.set_store_register(button1 != 0 ? 1 : 0);
      return true;
    case 300:
    case 1231:
    case 3500:
    case 3501:
    case 3502:
    case 3503:
    case 3504:
    case 3505:
    case 3506:
    case 3507:
    case 3508:
    case 3509:
    case 3510:
      return true;
    case 1000: {
      // RealLive integer domains are signed 32-bit. rand() % width can
      // overflow for INT_MIN..INT_MAX and excludes large portions of ranges
      // wider than RAND_MAX. Use unbiased rejection sampling instead.
      auto next = [] { return std::rand(); };
      if (over == 1 || c.GetParamCount() >= 2) {
        const int lo = value(c, 0), hi = value(c, 1);
        machine.set_store_register(randomInclusive(lo, hi, RAND_MAX, next));
      } else {
        const int limit = value(c, 0);
        machine.set_store_register(
            limit > 0 ? randomInclusive(0, limit - 1, RAND_MAX, next) : 0);
      }
      return true;
    }
    case 1001:
      machine.set_store_register(safePercent(value(c, 0), value(c, 1)));
      return true;
    case 1002: {
      auto v = static_cast<long long>(value(c, 0, 0));
      machine.set_store_register(
          static_cast<int>(std::min(v < 0 ? -v : v, 2147483647LL)));
      return true;
    }
    case 1003:
      machine.set_store_register(safeNegate(value(c, 0, 0)));
      return true;
    case 1004:
      machine.set_store_register(safeMod(value(c, 0, 0), value(c, 1, 1)));
      return true;
    case 1005:
    case 1006:
      machine.set_store_register(0);
      return true;
    case 1502:
      machine.set_store_register(0);
      return true;
    case 1504:
      machine.set_store_register(0);
      return true;
    case 2375:
      machine.set_store_register(
          (SDL_GetWindowFlags(&engine->GetWindow()) & SDL_WINDOW_FULLSCREEN)
              ? 1
              : 0);
      return true;
    case 1700: {
      EditField field;
      field.area = {float(value(c, 1)), float(value(c, 2)), float(value(c, 3)),
                    float(value(c, 4))};
      field.color = {Uint8(value(c, 6)), Uint8(value(c, 7)), Uint8(value(c, 8)),
                     255};
      field.background = {Uint8(value(c, 9, 255)), Uint8(value(c, 10, 255)),
                          Uint8(value(c, 11, 255)), 255};
      editFields[value(c, 0)] = std::move(field);
      return true;
    }
    case 1701:
      editFields.erase(value(c, 0));
      if (focusedEdit == value(c, 0)) {
        focusedEdit = -1;
        SDL_StopTextInput(&engine->GetWindow());
      }
      return true;
    case 1703:
      focusedEdit = value(c, 0);
      // A debug jump or an old save can enter after the matching 1700. Native
      // RealLive tolerates this; retain the text in a default field until the
      // script supplies its geometry.
      editFields.try_emplace(focusedEdit);
      SDL_StartTextInput(&engine->GetWindow());
      return true;
    case 1710: {
      auto &field = editFields[value(c, 0)];
      field.raw = str(c, 1);
      field.text =
          decodeText(field.raw, archive.Scenario(vm.scenario()).encoding());
      return true;
    }
    case 1711: {
      auto &field = editFields[value(c, 0)];
      auto raw = field.raw;
      const int encoding = archive.Scenario(vm.scenario()).encoding();
      if (field.text != decodeText(raw, encoding)) {
        raw = ConvertEncoding(field.text, "UTF-8",
                              encoding == 0 ? "CP932" : "CP1252");
        if (encoding == 2) {
          std::string mapped;
          for (unsigned char ch : raw) {
            if (ch < 128)
              mapped += char(ch);
            else if (ch <= 191) {
              mapped += char(0x89);
              mapped += char(ch);
            } else if (ch <= 254)
              mapped += char(ch - 31);
            else {
              mapped += char(0x89);
              mapped += char(0xc0);
            }
          }
          raw = std::move(mapped);
        }
      }
      Parameter(c, 1).SetStringValue(machine, raw);
      return true;
    }
    case 624:
      // Unknown system command; no side effect needed in this port.
      return true;
    default:
      // Try system module handler for module 4 commands (e.g., 2207)
      if (systemCommand(machine, c,
                        [this](const std::string &key, int fallback) {
                          return assets->settings().integer(key, fallback);
                        }))
        return true;
      return false;
    }
  }
  if (type == 2 && m == 1 && op == 12 && value(c, 1) == 104) {
    int window = value(c, 2);
    if (window < 0)
      window = vm.currentWindow;
    std::ostringstream key;
    key << "WINDOW." << std::setw(3) << std::setfill('0') << window
        << ".R_COMMAND_MOD";
    machine.set_store_register(assets->settings().integer(key.str(), 0));
    return true;
  }
  if (type == 1 && m == 12 && op == 3300) {
    for (int i = 0; i < c.GetParamCount() && i < int(syscomEntries.size()); ++i)
      syscomEntries[i] = value(c, i) != 0 ? 1 : 0;
    return true;
  }
  if (type == 1 && m == 40 && (op == 10 || op == 100)) {
    const bool blankBackground = op == 10 && over == 0;
    std::string name = blankBackground ? std::string() : str(c, 0);
    if (name == "???")
      name = lastGraphic;
    transitionOld = compose(presentationScale());
    transitionTexture.reset();
    transitionStart = SDL_GetTicks();
    const int sel = blankBackground ? value(c, 0) : value(c, 1);
    const auto effect = parseSelEffect(sel, 0);
    setTransitionEffect(effect);
    wait(transitionMs);
    if (blankBackground) {
      auto &destination = surface(0);
      SDL_FillSurfaceRect(&destination, nullptr,
                          SDL_MapSurfaceRGBA(&destination, 0, 0, 0, 0));
      lastGraphic.clear();
    } else if (!name.empty() && name != "?") {
      auto &g = graphic(name);
      if (g.surface)
        blit(*g.surface, surface(0));
      lastGraphic = name;
    }
    fg = std::move(bg);
    bg.clear();
    textbox->hide();
    return true;
  }
  if (type == 1 && (m == 12 || m == 13 || m == 40 || m == 41 || m == 255))
    return true;
  if (type == 1 && m == 5) {
    if (op == 0) {
      // File/resource query (statement form); no side effect needed in this
      // port.
      return true;
    }
    if (op == 10 || op == 114) {
      // RealLive-style availability query: (name, id, resource) -> 1/0.
      std::string resName = c.GetParamCount() > 2 ? str(c, 2) : str(c, 0);
      machine.set_store_register(assets->contains(resName) ? 1 : 0);
      return true;
    }
  }
  if (type == 1 && m == 20) {
    // Music starts here and nowhere else: boot/logo scenarios contain no
    // module 20 command, so no track is loaded before this opcode executes.
    if (bgmStartsPlayback(op)) {
      auto name = str(c, 0);
      for (auto &ch : name)
        if (ch >= 'A' && ch <= 'Z')
          ch += 32;
      auto i = audio.tracks.find(name);
      if (i == audio.tracks.end())
        throw std::runtime_error("REALLIVE BGM missing: " + name);
      auto &t = i->second;
      std::string id = "BGM/" + t.file;
      if (!assets->contains(id))
        id += ".NWA";
      play(bgm, id, "BGM", op == 0 || op == 1, t.from, t.to, t.loop);
      if (op == 1 || op == 3) {
        fadeTo = bgmGain;
        fadeFrom = 0.0f;
        fadeElapsed = 0;
        fadeDuration = std::max(0, value(c, 1, 1000));
        fadeStops = false;
        bgmGain = sampleBgmFade(fadeFrom, fadeTo, 0, fadeDuration);
        bgm.SetGain(bgmGain);
      }
      return true;
    }
    switch (bgmCommandKind(op, c.GetParamCount(), bgmDialect)) {
    case BgmCommandKind::Playing:
      machine.set_store_register(bgm.Playing() ? 1 : 0);
      return true;
    case BgmCommandKind::Status:
      machine.set_store_register(bgm.Playing() ? (bgmScriptPaused ? 2 : 1) : 0);
      return true;
    case BgmCommandKind::Gain:
      machine.set_store_register(gainToNativeBgm(bgmGain));
      return true;
    case BgmCommandKind::HasTrack:
      // Earlier Kotonoha versions returned whether a track was selected.
      // A volume reading (255/0) here changes script control flow.
      machine.set_store_register(musicID.empty() ? 0 : 1);
      return true;
    case BgmCommandKind::TrackName:
      Parameter(c, 0).SetStringValue(machine, musicID);
      return true;
    case BgmCommandKind::SetGain:
      setBgmVolume(value(c, 0), value(c, 1, 0));
      return true;
    case BgmCommandKind::Pause:
      bgmScriptPaused = true;
      bgm.SetPaused(true);
      return true;
    case BgmCommandKind::Resume:
      bgmScriptPaused = false;
      bgm.SetPaused(engine->isStoryPaused());
      return true;
    case BgmCommandKind::Stop:
      stopBgm();
      return true;
    case BgmCommandKind::Unmute:
      setBgmVolume(255, value(c, 0, 0));
      return true;
    case BgmCommandKind::Mute:
      // Native BGM mute silences the stream without terminating its loop.
      setBgmVolume(0, value(c, 0, 0));
      return true;
    case BgmCommandKind::Other:
      break;
    }
    if (op == 1002) {
      auto name = str(c, 0);
      for (auto &ch : name)
        if (ch >= 'A' && ch <= 'Z')
          ch += 32;
      auto track = audio.tracks.find(name);
      if (track == audio.tracks.end())
        throw std::runtime_error("REALLIVE BGM missing: " + name);
      std::string id = "BGM/" + track->second.file;
      if (!assets->contains(id))
        id += ".NWA";
      play(bgm, id, "BGM", false, track->second.from, track->second.to,
           track->second.loop, value(c, 1));
      return true;
    }
    if (op == 101 || op == 102 || op == 103) {
      fadeFrom = bgmGain;
      fadeTo = nativeBgmToGain(value(c, 0, 0));
      fadeElapsed = 0;
      fadeDuration = std::max(0, value(c, 1, 1000));
      fadeStops = fadeTo == 0.0f;
      if (fadeDuration == 0) {
        bgmGain = fadeTo;
        bgm.SetGain(bgmGain);
        if (fadeStops) {
          bgm.Stop();
          musicID.clear();
        }
      }
      if (op == 102 || op == 103)
        wait(fadeDuration);
      return true;
    }
    if (op == 105 || op == 106 || op == 107) {
      fadeFrom = bgmGain;
      fadeTo = 0.0f;
      fadeElapsed = 0;
      fadeDuration = std::max(0, value(c, 0, 1000));
      fadeStops = true;
      if (!fadeDuration) {
        bgm.Stop();
        musicID.clear();
        bgmGain = 0.0f;
        bgm.SetGain(bgmGain);
      }
      if (op == 106 || op == 107)
        wait(fadeDuration);
      return true;
    }
  }
  if (type == 1 && m == 23) {
    if (op == 0 || op == 2) {
      const int voiceId = value(c, 0);
      const std::string overlayVoice =
          "voice/" + std::to_string(voiceId) + ".wav";
      if (!assets->overlayPath(overlayVoice).empty()) {
        auto clip = assets->loadAudio(overlayVoice);
        playClip(voice, std::move(clip), "voice:" + std::to_string(voiceId),
                 "Voice");
        return true;
      }
      auto bytes = voices.read(voiceId);
      if (bytes) {
        auto encoded = bytes->size() >= 4 && (*bytes)[0] == 'O' &&
                               (*bytes)[1] == 'g' && (*bytes)[2] == 'g' &&
                               (*bytes)[3] == 'S'
                           ? std::move(*bytes)
                           : nwaToWav(*bytes);
        Kotonoha::AudioClip clip;
        if (Kotonoha::MediaDecoder::tryDecodeAudio(encoded, clip)) {
          assets->noteAudio(overlayVoice, clip);
          playClip(voice, std::move(clip), "voice:" + std::to_string(voiceId),
                   "Voice");
        }
      } else if (voiceId > 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Warning: REALLIVE voice missing for index %d", voiceId);
      }
      return true;
    }
    if (op == 1) {
      voice.Stop();
      return true;
    }
    if (op == 8) {
      machine.set_store_register(voice.Playing() ? 1 : 0);
      return true;
    }
    if (op == 9 || op == 11) {
      machine.set_store_register(voiceLevels.volume(0));
      return true;
    }
    if ((op == 10 || op == 12) && c.GetParamCount() > 0) {
      setVoiceVolume(value(c, 0), value(c, 1, 0));
      return true;
    }
    if (op == 13 || op == 14) {
      setVoiceVolume(op == 13 ? 255 : 0, value(c, 0, 0));
      return true;
    }
    if (op == 10)
      return true;
    if (op == 3 || op == 6 || op == 101) {
      // A voice wait must preserve the current script PC progression until
      // the decoder's PCM stream has actually drained. Unlike wait(ms), this
      // works for arbitrary sample rates and pauses with the audio device.
      audioWait.forVoice(voice.Playing());
      return true;
    }
  }
  if (type == 1 && m == 22) {
    if (op == 0 || op == 2) {
      auto mapping = audio.soundEffects.find(value(c, 0));
      if (mapping != audio.soundEffects.end()) {
        auto &sound = sounds[mapping->second.channel];
        if (!sound)
          sound = std::make_unique<PcmPlayer>();
        std::string id = audioAsset("WAV/" + mapping->second.file);
        if (id.empty()) {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "Warning: Sound effect not found: WAV/%s",
                      mapping->second.file.c_str());
          return true; // Skip missing sound effect
        }
        play(*sound, id, "Se", op == 2);
        sound->SetGain(soundLevels.gain(mapping->second.channel));
      }
      return true;
    }
    if (op == 1) {
      auto mapping = audio.soundEffects.find(value(c, 0));
      if (mapping != audio.soundEffects.end()) {
        sounds.erase(mapping->second.channel);
      }
      return true;
    }
    if (op == 8 || op == 11) {
      auto mapping = audio.soundEffects.find(value(c, 0));
      bool playing = false;
      if (mapping != audio.soundEffects.end()) {
        auto it = sounds.find(mapping->second.channel);
        playing = (it != sounds.end() && it->second && it->second->Playing());
      }
      machine.set_store_register(playing ? 1 : 0);
      return true;
    }
    if (op == 9) {
      auto mapping = audio.soundEffects.find(value(c, 0));
      machine.set_store_register(
          mapping == audio.soundEffects.end()
              ? 255
              : soundLevels.volume(mapping->second.channel));
      return true;
    }
    if (op == 10 && c.GetParamCount() >= 2) {
      auto mapping = audio.soundEffects.find(value(c, 0));
      if (mapping != audio.soundEffects.end())
        setSoundVolume(mapping->second.channel, value(c, 1), value(c, 2, 0));
      return true;
    }
    if (op == 10)
      return true;
  }
  if (type == 1 && m == 21) {
    if (op == 3) {
      const int channel = value(c, 0, 0);
      const auto found = sounds.find(channel);
      audioWait.forPcm(channel, found != sounds.end() && found->second &&
                                    found->second->Playing());
      return true;
    }
    if (op == 4) {
      const int channel = value(c, 0, 0);
      const auto found = sounds.find(channel);
      machine.set_store_register(found != sounds.end() && found->second &&
                                         found->second->Playing()
                                     ? 1
                                     : 0);
      return true;
    }
    if (op == 0 || op == 2) {
      int channel = value(c, 1);
      auto &p = sounds[channel];
      if (!p)
        p = std::make_unique<PcmPlayer>();
      std::string id = audioAsset("WAV/" + str(c, 0));
      if (id.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Warning: PCM audio not found: %s", str(c, 0).c_str());
        return true; // Skip missing PCM
      }
      play(*p, id, "PCM", op == 2);
      p->SetGain(soundLevels.gain(channel));
      return true;
    }
    if (op == 1) {
      if (c.GetParamCount() >= 2) {
        // Native wavPlayEx(filename, channel[, fade]): synchronous PCM
        // playback in older RealLive editions. A one-parameter variant is
        // retained as the existing channel-stop compatibility alias.
        const int channel = value(c, 1);
        auto &player = sounds[channel];
        if (!player)
          player = std::make_unique<PcmPlayer>();
        const auto id = audioAsset("WAV/" + str(c, 0));
        if (!id.empty()) {
          play(*player, id, "PCM", false);
          player->SetGain(soundLevels.gain(channel));
          audioWait.forPcm(channel, player->Playing());
        } else {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "REALLIVE missing wavPlayEx audio on channel %d",
                      channel);
        }
        return true;
      }
      int channel = value(c, 0);
      sounds.erase(channel);
      return true;
    }
    if (op == 8 || op == 4) {
      int channel = value(c, 0);
      auto it = sounds.find(channel);
      machine.set_store_register(
          (it != sounds.end() && it->second && it->second->Playing()) ? 1 : 0);
      return true;
    }
    if (op == 9 || op == 11) {
      machine.set_store_register(soundLevels.volume(value(c, 0)));
      return true;
    }
    if (op == 10 && c.GetParamCount() >= 2) {
      setSoundVolume(value(c, 0), value(c, 1), value(c, 2, 0));
      return true;
    }
    if (op == 12 && c.GetParamCount() >= 2) {
      setSoundVolume(value(c, 0), value(c, 1), value(c, 2, 0));
      return true;
    }
    if (op == 13 || op == 14) {
      setSoundVolume(value(c, 0), op == 13 ? 255 : 0, value(c, 1, 0));
      return true;
    }
    if (op == 105) {
      setSoundVolume(value(c, 0), 0, value(c, 1, 1000), true);
      return true;
    }
    if (op == 5 || op == 20 || (op == 12 && c.GetParamCount() < 2)) {
      if (op == 5 && over == 0 && c.GetParamCount() > 0)
        sounds.erase(value(c, 0));
      else
        sounds.clear();
      machine.set_store_register(1);
      return true;
    }
    if (op == 10)
      return true;
  }
  if (type == 1 && m == 26) {
    if (op == 1 || op == 20)
      return true;
    if (op == 0) {
      // Retail AIR calls 1:26:0("op_air.mpg", ...); other editions ship the
      // same name without an extension or with uppercase extensions. Resolve
      // through the public AssetProvider API so overlays and VFS mounts work,
      // and never build a doubled ".mpg.mpg" identifier.
      std::string id = "MOV/" + str(c, 0);
      if (!assets->contains(id)) {
        const std::string withMpg = id + ".mpg";
        const std::string withUpperMpg = id + ".MPG";
        if (assets->contains(withMpg))
          id = withMpg;
        else if (assets->contains(withUpperMpg))
          id = withUpperMpg;
      }
      movie->open(*assets, id, false, "BGM");
      videoSkip.reset();
      return true;
    }
  }
  if (type == 1 && m == 60) {
    if (op == 0 || op == 100 || op == 110)
      return true;
  }
  if (type == 1 && (m == 73 || m == 74)) {
    if (op == 1006 && over == 0) {
      // ganPlayEx(object, set): plays an ANM object's set and blocks for the
      // total set duration; static file objects are unaffected.
      const int object = value(c, 0);
      const int set = value(c, 1, 0);
      auto &objects = m == 73 ? fg : bg;
      auto found = objects.find(object);
      if (found != objects.end() && found->second.anm &&
          !found->second.anm->sets.empty()) {
        auto &o = found->second;
        o.anmSet = std::clamp(set, 0, int(o.anm->sets.size()) - 1);
        o.anmStart = SDL_GetTicks();
        o.anmPlaying = true;
        const auto duration = anmSetDuration(*o.anm, o.anmSet);
        wait(static_cast<int>(std::min(
            duration, static_cast<Uint64>(std::numeric_limits<int>::max()))));
        return true;
      }
      value(c, 0);
      value(c, 1);
      return true;
    }
    if (op == 0 || op == 3 || op == 4 || op == 104 || op == 2003 ||
        op == 3005 || op == 3103)
      return true;
    if (op == 2001 || op == 3001 || op == 3003) {
      const int object = value(c, 0);
      if (object >= 0 && object <= 30000) {
        const int frameMs = value(c, 1, 100);
        auto &objects = m == 73 ? fg : bg;
        auto found = objects.find(object);
        if (found != objects.end() && !found->second.file.empty()) {
          auto &o = found->second;
          o.frameMs = frameMs > 0 ? frameMs : 100;
          o.animationElapsed = 0;
        }
      }
      return true;
    }
  }
  if (type == 2 && (m == 73 || m == 74)) {
    return true;
  }
  if (type == 1 && (m == 71 || m == 72)) {
    auto &objects = m == 71 ? fg : bg;
    auto &o = objects[value(c, 0)];
    if (op == 1000) {
      o.width = o.height = 0;
      o.frameMs = 0;
      o.animationElapsed = 0;
      o.anm.reset();
      o.anmStart = 0;
      o.anmPlaying = false;
      o.file = str(c, 1);
      o.kind = Object::Kind::Sprite;
      loadObjectFile(o);
      if (over >= 1)
        o.visible = value(c, 2);
      if (over >= 2) {
        o.x = value(c, 3);
        o.y = value(c, 4);
      }
      if (over >= 3)
        o.pattern = value(c, 5);
      return true;
    }
    if (op == 1001) {
      // objName: assigns a logical name to an object; not tracked by this port.
      return true;
    }
    if (op == 1003) {
      // objNameEx: assigns name/label strings to an object; not tracked.
      return true;
    }
    if (op == 1500) {
      // Unknown object configuration; no side effect needed in this port.
      return true;
    }
    if (op == 1101) {
      o.file.clear();
      o.text.clear();
      o.anm.reset();
      o.anmPlaying = false;
      o.kind = Object::Kind::Sprite;
      o.frameMs = 0;
      o.animationElapsed = 0;
      if (c.GetParamCount() >= 6) {
        o.x = value(c, 1);
        o.y = value(c, 2);
        o.width = value(c, 3);
        o.height = value(c, 4);
        o.visible = value(c, 5) != 0;
      } else if (c.GetParamCount() == 5) {
        o.x = value(c, 1);
        o.y = value(c, 2);
        o.width = value(c, 3);
        o.height = value(c, 4);
      } else if (c.GetParamCount() == 4) {
        o.width = value(c, 1);
        o.height = value(c, 2);
        o.x = (canvasW - o.width) / 2;
        o.y = (canvasH - o.height) / 2;
        o.visible = value(c, 3) != 0;
      } else {
        o.x = 0;
        o.y = 0;
        o.width = canvasW;
        o.height = canvasH;
      }
      return true;
    }
    if (op == 1200) {
      o.file.clear();
      o.kind = Object::Kind::Sprite;
      o.frameMs = 0;
      o.animationElapsed = 0;
      o.width = o.height = 0;
      o.text = str(c, 1);
      return true;
    }
    if (op == 1300) {
      o.kind = Object::Kind::Drift;
      o.file = str(c, 1);
      o.driftCount = 1;
      o.driftUseAnimation = false;
      o.driftStartPattern = 0;
      o.driftEndPattern = 0;
      o.driftAnimationTime = 0;
      o.driftYSpeed = 0;
      o.driftPeriod = 0;
      o.driftAmplitude = 0;
      o.driftUseDrift = false;
      o.driftUnknown = 3;
      o.driftDriftSpeed = 0;
      o.driftArea = {-1, -1, -1, -1};
      o.visible = true;
      return true;
    }
  }
  if (type == 1 && (m == 81 || m == 82)) {
    auto &objects = m == 81 ? fg : bg;
    auto &o = objects[value(c, 0)];
    switch (op) {
    case 1000:
      o.x = value(c, 1);
      o.y = value(c, 2);
      return true;
    case 1006: {
      int id = value(c, 0), slot = value(c, 1);
      if (id < 0 || id > 30000 || slot < 0 || slot >= 16)
        throw std::out_of_range("REALLIVE object adjustment");
      o.adjustments[slot] = {value(c, 2), value(c, 3)};
      return true;
    }
    case 1003:
      o.alpha = value(c, 1);
      return true;
    case 1004:
      o.visible = value(c, 1);
      return true;
    case 1039:
      o.pattern = value(c, 1);
      return true;
    case 1011:
      o.x = value(c, 1);
      o.y = value(c, 2);
      return true;
    case 1031: {
      // objDriftOpts: configure particle/drift behaviour on an existing object.
      o.driftCount = value(c, 1);
      o.driftUseAnimation = value(c, 2) != 0;
      o.driftStartPattern = value(c, 3);
      o.driftEndPattern = value(c, 4);
      o.driftAnimationTime = value(c, 5);
      o.driftYSpeed = value(c, 6);
      o.driftPeriod = value(c, 7);
      o.driftAmplitude = value(c, 8);
      o.driftUseDrift = value(c, 9) != 0;
      o.driftUnknown = value(c, 10);
      o.driftDriftSpeed = value(c, 11);
      if (c.GetParamCount() >= 16) {
        o.driftArea = {value(c, 12), value(c, 13), value(c, 14), value(c, 15)};
      }
      return true;
    }
    case 1001: {
      if (c.GetParamCount() >= 4) {
        int slot = value(c, 1);
        if (slot >= 0 && slot < 16)
          o.adjustments[slot] = {value(c, 2), value(c, 3)};
      } else {
        o.x = value(c, 1);
      }
      return true;
    }
    case 1002:
      o.y = value(c, 1);
      return true;
    case 1007: {
      int slot = value(c, 1);
      if (slot >= 0 && slot < 16)
        o.adjustments[slot].x = value(c, 2);
      return true;
    }
    case 1008: {
      int slot = value(c, 1);
      if (slot >= 0 && slot < 16)
        o.adjustments[slot].y = value(c, 2);
      return true;
    }
    case 1040:
      o.clipped = over == 1 || c.GetParamCount() >= 5;
      if (o.clipped)
        o.clip = {value(c, 1), value(c, 2), value(c, 3), value(c, 4)};
      return true;
    case 1034:
      // objDispRect(buf, x, y, w, h): the last two parameters are a width
      // and a height, NOT inclusive x2/y2. AIR 9031:97 passes
      // (240, 170, 160, 120); the old subtraction produced negative extents,
      // so SDL ignored the clip and the entire KEYLOGO0 texture map (all
      // blur/color stages) was drawn at once.
      o.clipped = over == 1 || c.GetParamCount() >= 5;
      if (o.clipped)
        o.clip = {value(c, 1), value(c, 2), value(c, 3), value(c, 4)};
      return true;
    case 1016:
      o.colour = {Uint8(value(c, 1)), Uint8(value(c, 2)), Uint8(value(c, 3)),
                  Uint8(value(c, 4))};
      return true;
    case 1009:
    case 1021:
    case 1025:
    case 1046:
    case 1048:
    case 1049:
    case 1050:
    case 1052:
    case 1056:
      // Object property setters not yet modeled; no side effect needed.
      return true;
    }
  }
  if (type == 2 && (m == 81 || m == 82)) {
    auto &objects = m == 81 ? fg : bg;
    int first = value(c, 0);
    int last = value(c, 1);
    if (first > last)
      std::swap(first, last);
    last = std::min(last, first + 1024);
    for (int id = first; id <= last; ++id) {
      auto &o = objects[id];
      switch (op) {
      case 1000:
      case 1011:
        o.x = value(c, 2);
        o.y = value(c, 3);
        break;
      case 1001:
        o.x = value(c, 2);
        break;
      case 1002:
        o.y = value(c, 2);
        break;
      case 1003:
        o.alpha = value(c, 2);
        break;
      case 1004:
        o.visible = value(c, 2);
        break;
      case 1006: {
        int slot = value(c, 2);
        if (slot >= 0 && slot < 16)
          o.adjustments[slot] = {value(c, 3), value(c, 4)};
        break;
      }
      case 1007: {
        int slot = value(c, 2);
        if (slot >= 0 && slot < 16)
          o.adjustments[slot].x = value(c, 3);
        break;
      }
      case 1008: {
        int slot = value(c, 2);
        if (slot >= 0 && slot < 16)
          o.adjustments[slot].y = value(c, 3);
        break;
      }
      case 1034:
        // Bulk form of objDispRect: first/last object range, then x,y,w,h.
        o.clipped = over == 1 || c.GetParamCount() >= 6;
        if (o.clipped)
          o.clip = {value(c, 2), value(c, 3), value(c, 4), value(c, 5)};
        break;
      case 1040:
        o.clipped = over == 1 || c.GetParamCount() >= 6;
        if (o.clipped)
          o.clip = {value(c, 2), value(c, 3), value(c, 4), value(c, 5)};
        break;
      case 1039:
        o.pattern = value(c, 2);
        break;
      case 1031:
        o.driftCount = value(c, 2);
        o.driftUseAnimation = value(c, 3) != 0;
        o.driftStartPattern = value(c, 4);
        o.driftEndPattern = value(c, 5);
        o.driftAnimationTime = value(c, 6);
        o.driftYSpeed = value(c, 7);
        o.driftPeriod = value(c, 8);
        o.driftAmplitude = value(c, 9);
        o.driftUseDrift = value(c, 10) != 0;
        o.driftUnknown = value(c, 11);
        o.driftDriftSpeed = value(c, 12);
        break;
      default:
        break;
      }
    }
    return true;
  }
  if (type == 1 && (m == 61 || m == 62)) {
    auto &objects = m == 61 ? fg : bg;
    int first = value(c, 0);
    int last = (over == 1 && c.GetParamCount() >= 2) ? value(c, 1) : first;
    if (first > last)
      std::swap(first, last);
    last = std::min(last, first + 1024);
    if (op == 0 || op == 1 || op == 4 || op == 10) {
      for (int id = first; id <= last; ++id)
        objects[id] = Object{};
      return true;
    }
    if (op == 2) {
      auto &dest = m == 61 ? bg : fg;
      dest[c.GetParamCount() > 1 ? value(c, 1) : first] = objects[first];
      return true;
    }
    if (op == 11) {
      for (int id = first; id <= last; ++id)
        objects.erase(id);
      return true;
    }
  }
  if (type == 2 && (m == 61 || m == 62)) {
    auto &objects = m == 61 ? fg : bg;
    int first = value(c, 0);
    int last = (c.GetParamCount() >= 2) ? value(c, 1) : first;
    if (first > last)
      std::swap(first, last);
    last = std::min(last, first + 1024);
    if (op == 0 || op == 1 || op == 4 || op == 10) {
      for (int id = first; id <= last; ++id)
        objects[id] = Object{};
      return true;
    }
    if (op == 11) {
      for (int id = first; id <= last; ++id)
        objects.erase(id);
      return true;
    }
  }
  if (type == 2 && (m == 71 || m == 72)) {
    auto &objects = m == 71 ? fg : bg;
    int first = value(c, 0);
    int last = value(c, 1);
    if (first > last)
      std::swap(first, last);
    last = std::min(last, first + 1024);
    if (op == 1000) {
      const std::string file = str(c, 2);
      const bool vis = c.GetParamCount() >= 4 ? (value(c, 3) != 0) : true;
      prepareGraphic(graphic(file));
      for (int id = first; id <= last; ++id) {
        auto &o = objects[id];
        o.width = o.height = 0;
        o.frameMs = 0;
        o.animationElapsed = 0;
        o.file = file;
        o.kind = Object::Kind::Sprite;
        o.visible = vis;
      }
      return true;
    }
    return true;
  }
  if (type == 1 && m == 84) {
    auto &o = fg[value(c, 0)];
    if (op == 1000) {
      if (vm.scenario() == titleScenario &&
          runtime.GetState() == GameState::Menu) {
        const int id = value(c, 0);
        if (std::find(titleHitObjects.begin(), titleHitObjects.end(), id) ==
            titleHitObjects.end())
          titleHitObjects.push_back(id);
      }
      assign(c, 1, o.x);
      assign(c, 2, o.y);
      return true;
    }
    if (op == 1001) {
      if (c.GetParamCount() >= 2) {
        int slot = value(c, 1);
        machine.set_store_register(
            slot >= 0 && slot < 16 ? o.adjustments[slot].y : 0);
      } else {
        machine.set_store_register(o.x);
      }
      return true;
    }
    if (op == 1002) {
      machine.set_store_register(o.y);
      return true;
    }
    if (op == 1003) {
      machine.set_store_register(o.alpha);
      return true;
    }
    if (op == 1004) {
      machine.set_store_register(o.visible ? 1 : 0);
      return true;
    }
    if (op == 1039) {
      machine.set_store_register(o.pattern);
      return true;
    }
    if (op == 1011) {
      assign(c, 1, o.x);
      assign(c, 2, o.y);
      return true;
    }
    if (op == 1016) {
      assign(c, 1, o.colour.r);
      assign(c, 2, o.colour.g);
      assign(c, 3, o.colour.b);
      assign(c, 4, o.colour.a);
      return true;
    }
    if (op == 1006) {
      int slot = value(c, 1);
      machine.set_store_register(slot >= 0 && slot < 16 ? o.adjustments[slot].x
                                                        : 0);
      return true;
    }
    if (op == 1100) {
      int w = o.width, h = o.height;
      if (!o.text.empty() && textbox->font) {
        auto decoded =
            decodeText(o.text, archive.Scenario(vm.scenario()).encoding());
        TTF_GetStringSize(textbox->font.get(), decoded.data(), decoded.size(),
                          &w, &h);
      }
      if (!o.file.empty()) {
        auto &g = graphic(o.file);
        w = g.image.sourceWidth;
        h = g.image.sourceHeight;
        if (!g.image.regions.empty()) {
          auto &r =
              g.image.regions.at(o.currentPattern(g.image.regions.size()));
          w = r.x2 - r.x1 + 1;
          h = r.y2 - r.y1 + 1;
        }
      }
      assign(c, 1, w);
      assign(c, 2, h);
      return true;
    }
  }
  if (type == 2 && m == 84) {
    auto &o = fg[value(c, 0)];
    if (op == 1000) {
      if (vm.scenario() == titleScenario &&
          runtime.GetState() == GameState::Menu) {
        const int id = value(c, 0);
        if (std::find(titleHitObjects.begin(), titleHitObjects.end(), id) ==
            titleHitObjects.end())
          titleHitObjects.push_back(id);
      }
      assign(c, 1, o.x);
      assign(c, 2, o.y);
      return true;
    }
    if (op == 1100) {
      int w = o.width, h = o.height;
      assign(c, 1, w);
      assign(c, 2, h);
      return true;
    }
    return true;
  }
  if ((type == 1 || type == 2) && (m == 85 || m == 86)) {
    auto &o = bg[value(c, 0)];
    if (op == 1000) {
      assign(c, 1, o.x);
      assign(c, 2, o.y);
      return true;
    }
    if (op == 1003) {
      machine.set_store_register(o.alpha);
      return true;
    }
    if (op == 1004) {
      machine.set_store_register(o.visible ? 1 : 0);
      return true;
    }
    if (op == 1039) {
      machine.set_store_register(o.pattern);
      return true;
    }
    if (op == 1011) {
      assign(c, 1, o.x);
      assign(c, 2, o.y);
      return true;
    }
    if (op == 1100) {
      assign(c, 1, o.width);
      assign(c, 2, o.height);
      return true;
    }
    return true;
  }
  if (type == 1 && m == 60 && op == 10) {
    int id = value(c, 0);
    std::swap(fg[id], bg[id]);
    return true;
  }
  if (type == 1 && m == 60 && op == 2) {
    const int first = value(c, 0);
    const int last = c.GetParamCount() > 1 ? value(c, 1) : first;
    if (first < 0 || last < 0 || first > 30000 || last > 30000)
      throw std::out_of_range("REALLIVE object copy range");
    for (int id = first; id <= last; ++id)
      bg[id] = fg[id];
    return true;
  }
  if (type == 1 && m == 60 && op == 1 && over == 0) {
    // REALLIVE's objEraseWipeCopy(0) matches the port's default promotion
    // behavior.
    value(c, 0);
    return true;
  }
  if (type == 1 && m == 60 && op == 111) {
    fg.clear();
    bg.clear();
    return true;
  }
  if (type == 1 && m == 61 && op == 111) {
    fg.clear(); // objFgFreeInitAll does not affect background objects.
    return true;
  }
  if (type == 1 && m == 30 && op == 31) {
    auto &image = surface(value(c, 2));
    Uint8 r = 0, g = 0, b = 0, a = 0;
    float x = value(c, 0), y = value(c, 1);
    if (x >= 0 && y >= 0 && x < image.w && y < image.h)
      SDL_ReadSurfacePixel(&image, int(x), int(y), &r, &g, &b, &a);
    assign(c, 3, r);
    assign(c, 4, g);
    assign(c, 5, b);
    return true;
  }
  if (type == 1 && m == 30 && op == 2) {
    // stackPop removes replay history, not the displayed DC. We save DC
    // pixels directly, without replaying the original graphics stack.
    value(c, 0);
    return true;
  }
  if (type == 1 && m == 30 &&
      (op == 0 || op == 3 || op == 4 || op == 20 || op == 22))
    return true;
  if (type == 1 && m == 31 && op == 0) {
    if (vm.scenario() == titleScenario &&
        runtime.GetState() == GameState::Menu) {
      installTitleTargets();
      titleReady = true;
    }
    refreshRequested = true;
    return true;
  }
  if (type == 1 && m == 33) {
    if (op == 201 && over == 3) {
      const int dcIndex = value(c, 4);
      auto &s = surface(dcIndex);
      SDL_Rect rect{value(c, 0), value(c, 1), value(c, 2) - value(c, 0) + 1,
                    value(c, 3) - value(c, 1) + 1};
      if (rect.w > 0 && rect.h > 0) {
        SDL_FillSurfaceRect(
            &s, &rect,
            SDL_MapSurfaceRGBA(&s, Uint8(value(c, 5)), Uint8(value(c, 6)),
                               Uint8(value(c, 7)), Uint8(value(c, 8))));
        dcImages.erase(dcIndex);
      }
      return true;
    }
    if (op == 1056 && over == 4) {
      std::string name = str(c, 0);
      if (name == "???")
        name = lastGraphic;
      transitionOld = compose(presentationScale());
      transitionTexture.reset();
      transitionStart = SDL_GetTicks();
      transitionMs = std::max(0, value(c, 7));
      setTransitionParametersFromCommand(c);
      // '?' presents the already prepared DC1; it is not a filename.
      if (name != "?") {
        blit(surface(0), surface(1));
        lastGraphic = name;
        auto &image = graphic(name);
        SDL_Rect source{value(c, 1), value(c, 2), value(c, 3), value(c, 4)};
        SDL_Rect dest{value(c, 5), value(c, 6), 0, 0};
        blit(*image.surface, surface(1), &source, &dest, value(c, 15), false);
      }
      blit(surface(1), surface(0), nullptr, nullptr, 255, false);
      fg = std::move(bg);
      bg.clear();
      textbox->hide();
      wait(transitionMs);
      return true;
    }
    // Native RealLive FUN_004544b0: flash(R,G,B,duration), sixteen
    // restoration steps, with a snapshot of the old visible framebuffer.
    if ((op == 404 || op == 405) && over == 1) {
      flashColour = {Uint8(std::clamp(value(c, 0), 0, 255)),
                     Uint8(std::clamp(value(c, 1), 0, 255)),
                     Uint8(std::clamp(value(c, 2), 0, 255)), 255};
      flashStart = SDL_GetTicks();
      flashDuration = std::max(0, value(c, 3));
      wait(flashDuration);
      return true;
    }
    if (op == 75 || op == 77) {
      transitionOld = compose(presentationScale());
      transitionTexture.reset();
      transitionStart = SDL_GetTicks();
      const auto mainEffect = parseSelEffect(value(c, 1), 0);
      setTransitionEffect(mainEffect);
      if (op == 75) {
        auto name = str(c, 0);
        if (name == "???")
          name = lastGraphic;
        if (name != "?" && !name.empty()) {
          auto &g = graphic(name);
          if (g.surface) {
            const bool useSelRect =
                mainEffect.src.w > 0 && mainEffect.src.h > 0;
            SDL_Rect sr = mainEffect.src;
            SDL_Rect dr = mainEffect.dst;
            blit(*g.surface, surface(1), useSelRect ? &sr : nullptr,
                 useSelRect ? &dr : nullptr, 255, false);
          }
        }
      } else if (value(c, 0) != 1)
        blit(surface(value(c, 0)), surface(1), nullptr, nullptr, 255, false);
      for (std::size_t i = 2; i < c.GetParamCount(); ++i) {
        auto raw = c.GetParam(static_cast<int>(i));
        const char *source = raw.c_str();
        auto command = libreallive::GetData(source);
        if (!command.IsSpecialParameter())
          throw std::runtime_error(
              "REALLIVE grpMulti requires tagged operation");
        auto &pieces = command.GetContainedPieces();
        int tag = command.GetOverloadTag();
        if (pieces.empty() || tag < 0 || tag > 4)
          throw std::runtime_error("REALLIVE grpMulti invalid operation");
        auto filename = pieces[0].GetStringValue(machine);
        if (filename.empty())
          continue;
        auto &image = graphic(filename);
        if (!image.surface)
          continue;
        SDL_Rect sr{0, 0, image.surface->w, image.surface->h}, dst{0, 0, 0, 0};
        int alpha = 255;
        auto n = [&](std::size_t k) {
          if (k >= pieces.size())
            throw std::runtime_error("REALLIVE grpMulti missing parameter");
          return pieces[k].GetIntegerValue(machine);
        };
        if (tag == 1 || tag == 2) {
          const auto subEffect = parseSelEffect(n(1), 0);
          sr = subEffect.src;
          dst = subEffect.dst;
          if (tag == 2)
            alpha = n(2);
        }
        if (tag == 3 || tag == 4) {
          sr = {n(1), n(2), n(3) - n(1) + 1, n(4) - n(2) + 1};
          dst = {n(5), n(6), 0, 0};
          if (tag == 4)
            alpha = n(7);
        }
        blit(*image.surface, surface(1), &sr, &dst, alpha, true);
      }
      blit(surface(1), surface(0), nullptr, nullptr, 255, false);
      fg = std::move(bg);
      bg.clear();
      textbox->hide();
      wait(transitionMs);
      return true;
    }
    if (op == 402) {
      if (c.GetParamCount() < 14)
        throw std::runtime_error("Malformed REALLIVE grpZoom");
      const auto bounds = [&](int first) {
        SDL_Rect rect{value(c, first), value(c, first + 1),
                      value(c, first + 2) - value(c, first),
                      value(c, first + 3) - value(c, first + 1)};
        if (rect.w <= 0 || rect.h <= 0)
          throw std::runtime_error("Invalid REALLIVE grpZoom rectangle");
        return rect;
      };
      const auto from = bounds(0);
      const auto to = bounds(4);
      const int sourceDC = value(c, 8);
      if (sourceDC < 0 || sourceDC > 15)
        throw std::out_of_range("REALLIVE grpZoom source DC");
      const auto sourceRect = bounds(9);
      auto destinationRect = sourceRect;
      const int duration = std::max(0, value(c, 13));
      SurfacePtr source(SDL_DuplicateSurface(&surface(sourceDC)));
      if (!source)
        throw std::runtime_error(SDL_GetError());
      if (!duration) {
        blit(surface(sourceDC), surface(0), &to, &destinationRect, 255, true);
        return true;
      }
      zoom = ZoomEffect{
          std::move(source),
          dcImage(sourceDC),
          to,
          destinationRect,
          TransformAnimation(
              {float(from.x), float(from.y), float(from.w), float(from.h)},
              {float(to.x), float(to.y), float(to.w), float(to.h)}, duration),
          SDL_GetTicks()};
      zoomTexture.reset();
      wait(duration);
      return true;
    }
    if (op == 1402) {
      // recZoom: same zoom as grpZoom, but rectangles are REC (x, y, w, h),
      // with no coordinate subtraction.
      if (c.GetParamCount() < 14)
        throw std::runtime_error("Malformed REALLIVE recZoom");
      const auto bounds = [&](int first) {
        SDL_Rect rect{value(c, first), value(c, first + 1), value(c, first + 2),
                      value(c, first + 3)};
        if (rect.w <= 0 || rect.h <= 0)
          throw std::runtime_error("Invalid REALLIVE recZoom rectangle");
        return rect;
      };
      const auto from = bounds(0);
      const auto to = bounds(4);
      const int sourceDC = value(c, 8);
      if (sourceDC < 0 || sourceDC > 15)
        throw std::out_of_range("REALLIVE recZoom source DC");
      SDL_Rect destinationRect = bounds(9);
      const int duration = std::max(0, value(c, 13));
      SurfacePtr source(SDL_DuplicateSurface(&surface(sourceDC)));
      if (!source)
        throw std::runtime_error(SDL_GetError());
      if (!duration) {
        blit(surface(sourceDC), surface(0), &to, &destinationRect, 255, true);
        return true;
      }
      zoom = ZoomEffect{
          std::move(source),
          dcImage(sourceDC),
          to,
          destinationRect,
          TransformAnimation(
              {float(from.x), float(from.y), float(from.w), float(from.h)},
              {float(to.x), float(to.y), float(to.w), float(to.h)}, duration),
          SDL_GetTicks()};
      zoomTexture.reset();
      wait(duration);
      return true;
    }
    if (op == 32) {
      std::ostringstream key;
      key << "SHAKE." << std::setw(3) << std::setfill('0') << value(c, 0);
      const auto &values = assets->settings().get(key.str());
      shake.clear();
      int time = 0;
      for (size_t n = 0; n + 2 < values.size(); n += 3) {
        std::array<int, 3> step{std::stoi(values[n]), std::stoi(values[n + 1]),
                                std::max(0, std::stoi(values[n + 2]))};
        shake.push_back(step);
        time += step[2];
      }
      shakeStart = SDL_GetTicks();
      wait(time);
      return true;
    }
    if (op == 303 || op == 403 || op == 1600 || op == 1700)
      return true;
    if (op == 300 || op == 301 || op == 1300 || op == 1301) {
      const int id = value(c, 4);
      auto &s = surface(id);
      SDL_Rect rect{value(c, 0), value(c, 1), value(c, 2), value(c, 3)};
      if (op < 1000) {
        rect.w -= rect.x;
        rect.h -= rect.y;
      }
      auto image = std::make_shared<DCImage>();
      image->kind =
          op == 301 || op == 1301 ? DCImage::Kind::Gray : DCImage::Kind::Invert;
      image->width = s.w;
      image->height = s.h;
      image->crop = rect;
      image->previous = dcImage(id);
      dcImages[id] = std::move(image);
      for (int y = std::max(0, rect.y); y < std::min(s.h, rect.y + rect.h); ++y)
        for (int x = std::max(0, rect.x); x < std::min(s.w, rect.x + rect.w);
             ++x) {
          auto *p = static_cast<Uint8 *>(s.pixels) + y * s.pitch + x * 4;
          if (op == 301 || op == 1301) {
            auto gray = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            p[0] = p[1] = p[2] = Uint8(gray);
          } else {
            p[0] = 255 - p[0];
            p[1] = 255 - p[1];
            p[2] = 255 - p[2];
          }
        }
      return true;
    }
    if (op == 1401) {
      auto &source = surface(value(c, 4));
      auto &dest = surface(value(c, 9));
      SDL_Rect sr{value(c, 0), value(c, 1), value(c, 2), value(c, 3)},
          dr{value(c, 5), value(c, 6), value(c, 7), value(c, 8)};
      auto image = std::make_shared<DCImage>();
      image->kind = DCImage::Kind::Copy;
      image->width = dest.w;
      image->height = dest.h;
      image->crop = sr;
      image->destination = dr;
      image->source = dcImage(value(c, 4));
      image->previous = dcImage(value(c, 9));
      dcImages[value(c, 9)] = std::move(image);
      SDL_SetSurfaceBlendMode(&source, SDL_BLENDMODE_NONE);
      SDL_BlitSurfaceScaled(&source, &sr, &dest, &dr, SDL_SCALEMODE_LINEAR);
      return true;
    }

    if (op == 70 || op == 1050 || op == 1051) {
      const auto graphicName = str(c, 0);
      // Dynamic graphic variables can be empty before their surrounding
      // scenario initializes them (notably AIR 163:1330). RealLive treats an
      // empty source as a completed no-op; do not manufacture a 0x0 DC.
      if (graphicName.empty())
        return true;
      auto &g = graphic(graphicName);
      int n = value(c, 1);
      if (n < 0 || n > 15)
        throw std::runtime_error("Invalid REALLIVE DC");
      if (n > 1 && over < 2) {
        dc[n] = blank(g.image.width, g.image.height);
        dcImages.erase(n);
      }
      SDL_Rect source{value(c, 2), value(c, 3), value(c, 4), value(c, 5)},
          dest{value(c, 6), value(c, 7), 0, 0};
      if (op == 70 && over >= 2) {
        source.w = source.w - source.x + 1;
        source.h = source.h - source.y + 1;
      }
      if (over >= 2)
        ensureDC(n, dest.x + source.w, dest.y + source.h);
      blit(*g.surface, surface(n), over >= 2 ? &source : nullptr,
           over >= 2 ? &dest : nullptr, value(c, over >= 2 ? 8 : 2, 255),
           op == 1051);
      return true;
    }
    if (op == 100 || op == 101 || op == 1100 || op == 1101) {
      int sourceDC = value(c, over >= 2 ? 4 : 0),
          targetDC = value(c, over >= 2 ? 7 : 1);
      SDL_Rect source{value(c, 0), value(c, 1), value(c, 2), value(c, 3)},
          dest{value(c, 5), value(c, 6), 0, 0};
      if (op < 1000) {
        source.w = source.w - source.x + 1;
        source.h = source.h - source.y + 1;
      }
      ensureDC(targetDC, over >= 2 ? dest.x + source.w : surface(sourceDC).w,
               over >= 2 ? dest.y + source.h : surface(sourceDC).h);
      if (sourceDC == targetDC) {
        auto temp = blank(surface(sourceDC).w, surface(sourceDC).h);
        blit(surface(sourceDC), *temp);
        blit(*temp, surface(targetDC), over >= 2 ? &source : nullptr,
             over >= 2 ? &dest : nullptr, value(c, over >= 2 ? 8 : 2, 255),
             op == 101 || op == 1101);
      } else {
        blit(surface(sourceDC), surface(targetDC),
             over >= 2 ? &source : nullptr, over >= 2 ? &dest : nullptr,
             value(c, over >= 2 ? 8 : 2, 255), op == 101 || op == 1101);
      }
      return true;
    }
    if (op == 16) {
      int n = value(c, 0);
      if (n == 0)
        throw std::runtime_error("Cannot free REALLIVE display DC");
      if (n == 1) {
        dc[1] = blank(canvasW, canvasH);
        dcImages.erase(1);
        return true;
      }
      if (n < 0 || n > 15)
        throw std::runtime_error("Invalid REALLIVE DC");
      dc.erase(n);
      dcImages.erase(n);
      return true;
    }
    if (op == 31) {
      auto &s = surface(value(c, 0));
      SDL_FillSurfaceRect(
          &s, nullptr,
          SDL_MapSurfaceRGBA(&s, value(c, 1), value(c, 2), value(c, 3), 255));
      dcImages.erase(value(c, 0));
      return true;
    }
    if (op == 1053 || op == 73 || op == 74) {
      std::string name = str(c, 0);
      if (name == "???")
        name = lastGraphic;
      transitionOld = compose(presentationScale());
      transitionTexture.reset();
      transitionStart = SDL_GetTicks();
      const int selIndex =
          (over == 2 || over == 3) ? value(c, 7, 0) : value(c, 1, 0);
      const auto selEffect = parseSelEffect(selIndex, 500);
      setTransitionEffect(selEffect);
      if (over == 4) {
        transitionMs = std::max(0, value(c, 7));
        setTransitionParametersFromCommand(c);
      }
      wait(transitionMs);
      // "?" reuses DC1; copying DC0 here would destroy that prepared image.
      if (name != "?")
        blit(surface(0), surface(1));
      if (name != "?" && !name.empty()) {
        if (op != 74)
          lastGraphic = name;
        auto &g = graphic(name);
        SDL_Rect sr{value(c, 1), value(c, 2), value(c, 3) + int(op < 1000),
                    value(c, 4) + int(op < 1000)},
            dst{value(c, 5), value(c, 6), 0, 0};
        if (op < 1000) {
          sr.w -= sr.x;
          sr.h -= sr.y;
        }
        int alpha = 255;
        if (over == 0 || over == 1) {
          sr = selEffect.src;
          dst = selEffect.dst;
          if (over == 1)
            alpha = value(c, 2, 255);
        } else if (over == 3) {
          alpha = value(c, 8, 255);
        } else if (over == 4) {
          alpha = value(c, 15, 255);
        }
        if (g.surface)
          blit(*g.surface, surface(1), &sr, &dst, alpha, op == 74);
      }
      blit(surface(1), surface(0), nullptr, nullptr, 255, false);
      fg = std::move(bg);
      bg.clear();
      textbox->hide();
      return true;
    }
    if (op == 1201) {
      auto &s = surface(c.GetParamCount() > 4 ? value(c, 4) : value(c, 0));
      int offset = c.GetParamCount() > 4 ? 5 : 1;
      SDL_Rect r{value(c, 0), value(c, 1), value(c, 2), value(c, 3)};
      auto fill = blank(c.GetParamCount() > 4 ? std::max(1, r.w) : s.w,
                        c.GetParamCount() > 4 ? std::max(1, r.h) : s.h);
      int alpha = value(c, offset + 3, 255);
      SDL_FillSurfaceRect(fill.get(), nullptr,
                          SDL_MapSurfaceRGBA(fill.get(), value(c, offset),
                                             value(c, offset + 1),
                                             value(c, offset + 2), 255));
      SDL_Rect dest{r.x, r.y, 0, 0};
      blit(*fill, s, nullptr, c.GetParamCount() > 4 ? &dest : nullptr, alpha);
      return true;
    }
  }
  if (systemCommand(machine, c, [this](const std::string &key, int fallback) {
        return assets->settings().integer(key, fallback);
      })) {
    if (type == 1 && m == 4 &&
        ((op >= 2230 && op <= 2233) || (op >= 2240 && op <= 2243))) {
      struct Bank {
        const char *channel;
        int volume, enabled;
        const char *volumeKey;
        const char *enabledKey;
      };
      const Bank banks[]{{"BGM", 2330, 2340, "BGM_VOLUME", "BGM_ENABLED"},
                         {"Voice", 2331, 2341, "KOE_VOLUME", "KOE_ENABLED"},
                         {"PCM", 2332, 2342, "PCM_VOLUME", "PCM_ENABLED"},
                         {"Se", 2333, 2343, "SE_VOLUME", "SE_ENABLED"}};
      for (const auto &bank : banks) {
        const auto &values = machine.memory().integers;
        auto setting = [&](int key, const char *name, int fallback) {
          auto found = values.find({26, key});
          return found == values.end()
                     ? assets->settings().integer(name, fallback)
                     : found->second;
        };
        auto channel = engine->GetAudio().channel(bank.channel);
        channel->setVolume(
            std::clamp(setting(bank.volume, bank.volumeKey, 255), 0, 255) /
            255.0f);
        channel->setMuted(setting(bank.enabled, bank.enabledKey, 1) == 0);
      }
    }
    if (type == 1 && m == 4 && op == 2275) {
      auto display = engine->GetSettings();
      display.mode =
          value(c, 0) ? WindowMode::Borderless : WindowMode::Windowed;
      engine->applySettings(display);
    }
    return true;
  }
  std::ostringstream message;
  message << "Comando REALLIVE pendente em " << vm.scenario() << ":"
          << vm.index() << " (" << type << ":" << m << ":" << op << "/" << over
          << ").";
  error = message.str();
  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
  return true;
}
RealLiveRuntime::RealLiveRuntime(
    std::shared_ptr<Kotonoha::Providers::RealLiveAssets> a, int scene)
    : Runtime("REALLIVE", "0.1.0", "me.hirameki.kotonoha.reallive"),
      impl(std::make_unique<Impl>(*this, std::move(a), scene)) {}
RealLiveRuntime::~RealLiveRuntime() = default;
std::string RealLiveRuntime::diagnostic() const { return impl->error; }
int RealLiveRuntime::debugMouseCursor() const {
  return impl->mouseCursorNumber;
}
RealLiveRuntime::RenderStats RealLiveRuntime::renderStats() const {
  return impl->renderStats;
}
void RealLiveRuntime::shutdown() {
  if (impl->mouseCursor) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    SDL_DestroyCursor(impl->mouseCursor);
    impl->mouseCursor = nullptr;
  }
  impl->bgm.Stop();
  impl->voice.Stop();
  impl->sounds.clear();
  impl->audioWait.clear();
  impl->soundLevels.clear();
  impl->voiceLevels.clear();
  if (impl->movie)
    impl->movie->stop();
  impl->graphics.clear();
  impl->dcImages.clear();
  impl->compositionTexture.reset();
  impl->backgroundTexture.reset();
  impl->zoomTexture.reset();
  impl->transitionTexture.reset();
  impl->ditherTexture.reset();
  impl->maskedTransitionTexture.reset();
  impl->objectTextTextures.clear();
  ClearScenes();
  impl->textbox.reset();
  impl->choice.reset();
  impl->saves.reset();
  impl->stage.reset();
  impl->movie.reset();
}
void RealLiveRuntime::initMenu(Engine &e) {
  impl->bind(e);
  impl->titleInput.reset();
}
void RealLiveRuntime::initGameplay(Engine &e) {
  impl->bind(e);
  impl->titleInput.reset();
}
void RealLiveRuntime::update() {
  Runtime::update();
  auto &i = *impl;
  if (!i.engine)
    return;
  static const bool trace = SDL_getenv("KOTONOHA_REALLIVE_TRACE") != nullptr;
  if (trace) {
    std::string scenes;
    for (auto *scene : GetActiveScenes())
      if (scene)
        scenes += (scenes.empty() ? "" : ",") +
                  std::string(typeid(*scene).name()) +
                  (scene->capturesInput() ? "(capture)" : "");
    std::string objects;
    for (int id : {10, 11, 12, 13, 14, 15}) {
      auto found = i.fg.find(id);
      if (found == i.fg.end())
        continue;
      const auto &o = found->second;
      objects += " o" + std::to_string(id) + "=" + std::to_string(o.x) + "," +
                 std::to_string(o.y) + "," + std::to_string(o.width) + "x" +
                 std::to_string(o.height) + (o.visible ? "v" : "-");
    }
    const auto &mem = i.vm.machine().memory().integers;
    auto get = [&](int location, int key) {
      auto it = mem.find({location, key});
      return it == mem.end() ? -9999 : it->second;
    };
    SDL_Log(
        "RLTRACE node=%s pause=%d storyPaused=%d choice=%d deadline=%llu "
        "refresh=%d ready=%d blocked=%d stack=%zu m1001=%d m1002=%d m1003=%d "
        "m1006=%d m1008=%d m1009=%d m1012=%d m1013=%d "
        "a1000=%d a1001=%d a1002=%d a1003=%d "
        "m1200=%d m1201=%d m1202=%d m1204=%d m1206=%d m1210=%d g1037=%d "
        "hits=%zu targets=%zu objects=[%s] scenes=[%s] err=%s",
        debugNode().c_str(), int(i.pause), int(isStoryPaused()),
        int(i.choice->isActive()), static_cast<unsigned long long>(i.deadline),
        int(i.refreshRequested), int(i.titleReady), int(i.vm.blocked()),
        i.vm.stackDepth(), get(5, 1001), get(5, 1002), get(5, 1003),
        get(5, 1006), get(5, 1008), get(5, 1009), get(5, 1012), get(5, 1013),
        get(0, 1000), get(0, 1001), get(0, 1002), get(0, 1003), get(5, 1200),
        get(5, 1201), get(5, 1202), get(5, 1204), get(5, 1206), get(5, 1210),
        get(6, 1037), i.titleHitObjects.size(), i.titleInput.targetCount(),
        objects.c_str(), scenes.c_str(), i.error.c_str());
  }
  bool paused = isStoryPaused();
  if (paused)
    i.button1 = i.button2 = 0;
  const auto audioNow = SDL_GetTicks();
  const auto delta = i.audioTick ? audioNow - i.audioTick : 0;
  i.audioTick = audioNow;
  if (!paused) {
    const auto animate = [&](std::map<int, Object> &objects) {
      for (auto &[id, o] : objects)
        if (o.frameMs > 0)
          o.animationElapsed += delta;
    };
    animate(i.fg);
    animate(i.bg);
  }
  if (paused && i.autoDeadline)
    i.autoDeadline += delta;
  if (!paused) {
    i.voiceLevels.advance(
        delta, [&](int, float gain, bool) { i.voice.SetGain(gain); });
    i.soundLevels.advance(delta, [&](int channel, float gain, bool stop) {
      const auto found = i.sounds.find(channel);
      if (found != i.sounds.end() && found->second) {
        if (stop)
          found->second->Stop();
        else
          found->second->SetGain(gain);
      }
    });
  }
  if (!paused && i.fadeDuration > 0) {
    i.fadeElapsed = advanceBgmFade(i.fadeElapsed, i.fadeDuration, delta);
    i.bgmGain =
        sampleBgmFade(i.fadeFrom, i.fadeTo, i.fadeElapsed, i.fadeDuration);
    i.bgm.SetGain(i.bgmGain);
    if (i.fadeElapsed == i.fadeDuration) {
      i.fadeDuration = 0;
      if (i.fadeStops) {
        i.bgm.Stop();
        i.musicID.clear();
      }
    }
  }
  i.bgm.SetPaused(paused || i.bgmScriptPaused);
  i.voice.SetPaused(paused);
  for (auto &[n, s] : i.sounds)
    s->SetPaused(paused);
  if (i.movie)
    i.movie->setPaused(paused);
  if (!paused && i.pause && !i.textbox->concealed) {
    if ((i.skipMode && i.currentRead) || (i.skipHeld && i.ctrlSkipEnabled)) {
      i.textbox->completeDialogue();
      // Automated read-skip must stay enabled until the first unread page.
      // A held Ctrl/gamepad skip is independent of that persistent mode.
      i.advanceText(false, i.skipMode && i.currentRead);
    } else if (i.autoMode && i.textbox->isDialogueComplete() &&
               !i.voice.Playing()) {
      if (!i.autoDeadline) {
        const auto &values = i.vm.machine().memory().integers;
        auto get = [&](int key, const char *name, int fallback) {
          auto entry = values.find({26, key});
          return entry == values.end()
                     ? i.assets->settings().integer(name, fallback)
                     : entry->second;
        };
        const auto duration =
            std::max(0, get(2352, "MESSAGE_KEY_WAIT_TIME", 1000)) +
            std::max(0, get(2351, "AUTO_CHAR_TIME", 30)) *
                VNTextBoxUtility::CountUtf8Chars(i.dialogue);
        i.autoDeadline = SDL_GetTicks() + duration;
      }
      if (SDL_GetTicks() >= i.autoDeadline)
        i.advanceText(false);
    }
  }
  const bool audioBlocked =
      i.audioWait.blocks(i.voice.Playing(), [&](int channel) {
        const auto found = i.sounds.find(channel);
        return found != i.sounds.end() && found->second &&
               found->second->Playing();
      });
  if (paused || i.pause || i.choice->isActive() || !i.error.empty() ||
      SDL_GetTicks() < i.deadline || audioBlocked)
    return;
  if (i.movie->isOpen()) {
    if (!i.movie->finished())
      return;
    i.movie->stop();
  }
  i.refreshRequested = false;
  for (int n = 0;
       n < 3000 && !i.refreshRequested && !i.pause && !i.choice->isActive() &&
       i.error.empty() && SDL_GetTicks() >= i.deadline && !i.movie->isOpen() &&
       !i.audioWait.active();
       ++n)
    try {
      if (!i.vm.step())
        break;
    } catch (const std::exception &e) {
      i.error = "REALLIVE " + debugNode() + ": " + e.what();
      SDL_Log("%s", i.error.c_str());
      break;
    }
  if (i.vm.scenario() < 8000 && GetState() == GameState::Menu) {
    i.titleReady = false;
    i.titleInput.reset();
    if (GetScenes(GameState::Gameplay).empty())
      for (auto *scene : GetScenes(GameState::Menu))
        AddScene(scene, GameState::Gameplay);
    SetState(GameState::Gameplay);
  }
}
void RealLiveRuntime::saveGame(const std::string &path) {
  auto &i = *impl;
  if (!i.pause || !i.error.empty() || i.choice->isActive())
    throw std::runtime_error(
        "Salve durante uma fala, após terminar a transição.");
  std::ostringstream out;
  out << "REALLIVE-PRESENTATION-7\n"
      << std::quoted(i.vm.snapshot()) << '\n'
      << std::quoted(i.dialogue) << ' ' << std::quoted(i.speakerName) << ' '
      << i.pauseKind << '\n';
  out << std::quoted(i.musicID) << ' ' << i.musicLoop << ' ' << i.musicEnd
      << ' ' << i.musicLoopStart << ' ' << std::setprecision(17) <<
      [&]() {
        // PcmPlayer::Position() returns seconds using the source clip rate.
        const auto rate = std::max(i.musicRate, 1);
        auto frame = i.bgm.Position() * rate;
        if (i.musicLoop && i.musicFrames > i.musicLoopStart &&
            frame >= i.musicFrames)
          frame = i.musicLoopStart +
                  std::fmod(frame - i.musicLoopStart,
                            double(i.musicFrames - i.musicLoopStart));
        return frame / rate;
      }()
      << '\n';
  out << i.bgmGain << ' ' << i.fadeFrom << ' ' << i.fadeTo << ' '
      << i.fadeElapsed << ' ' << i.fadeDuration << ' ' << i.fadeStops << '\n';
  auto objects = [&](const std::map<int, Object> &objects) {
    out << objects.size() << '\n';
    for (auto &[id, o] : objects) {
      out << id << ' ' << std::quoted(o.file) << ' ' << std::quoted(o.text)
          << ' ' << o.x << ' ' << o.y << ' ' << o.pattern << ' ' << o.alpha
          << ' ' << o.visible << ' ' << o.width << ' ' << o.height << ' '
          << int(o.colour.r) << ' ' << int(o.colour.g) << ' ' << int(o.colour.b)
          << ' ' << int(o.colour.a) << ' ' << o.clipped << ' ' << o.clip.x
          << ' ' << o.clip.y << ' ' << o.clip.w << ' ' << o.clip.h << ' '
          << o.frameMs << ' ' << o.animationElapsed;
      for (const auto &adjustment : o.adjustments)
        out << ' ' << adjustment.x << ' ' << adjustment.y;
      out << '\n';
    }
  };
  objects(i.fg);
  objects(i.bg);
  out << i.dc.size() << '\n';
  for (auto &[id, s] : i.dc) {
    std::string pixels;
    pixels.reserve(size_t(s->w) * s->h * 4);
    for (int y = 0; y < s->h; ++y)
      pixels.append(static_cast<const char *>(s->pixels) + y * s->pitch,
                    size_t(s->w) * 4);
    out << id << ' ' << s->w << ' ' << s->h << ' ' << std::quoted(pixels)
        << '\n';
  }
  out << std::quoted(i.saveName) << '\n';
  std::map<const DCImage *, size_t> indices;
  std::vector<std::shared_ptr<DCImage>> images;
  const auto collect = [&](const auto &self,
                           const std::shared_ptr<DCImage> &image) -> void {
    if (!image || indices.count(image.get()))
      return;
    self(self, image->source);
    self(self, image->previous);
    images.push_back(image);
    indices.emplace(image.get(), images.size());
  };
  for (const auto &[id, pixels] : i.dc)
    collect(collect, i.dcImage(id));
  out << images.size() << '\n';
  for (const auto &image : images) {
    const auto index = [&](const std::shared_ptr<DCImage> &child) {
      return child ? indices.at(child.get()) : 0;
    };
    std::string pixels;
    if (image->native)
      for (int y = 0; y < image->native->h; ++y)
        pixels.append(static_cast<const char *>(image->native->pixels) +
                          y * image->native->pitch,
                      size_t(image->native->w) * 4);
    out << int(image->kind) << ' ' << image->width << ' ' << image->height
        << ' ' << image->alpha << ' ' << image->mask << ' ' << image->crop.x
        << ' ' << image->crop.y << ' ' << image->crop.w << ' ' << image->crop.h
        << ' ' << image->destination.x << ' ' << image->destination.y << ' '
        << image->destination.w << ' ' << image->destination.h << ' '
        << index(image->source) << ' ' << index(image->previous) << ' '
        << std::quoted(image->graphic) << ' ' << std::quoted(pixels) << '\n';
  }
  out << i.dc.size() << '\n';
  for (const auto &[id, pixels] : i.dc)
    out << id << ' ' << indices.at(i.dcImage(id).get()) << '\n';
  out << i.mouseCursorNumber << '\n';
  writeReadProgress(
      out, ReadProgress{i.bgmScriptPaused, i.currentPage, i.readPages});
  VNSave save;
  save.nodeId = i.vm.scenario();
  save.gameData = out.str();
  if (!std::filesystem::path(path).parent_path().empty())
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
  save.write(path);
}
void RealLiveRuntime::loadGame(const std::string &path) {
  auto &i = *impl;
  auto data = VNSave::read(path);
  std::istringstream in(data.gameData);
  auto invalid = []() {
    throw std::runtime_error("Save REALLIVE inválido ou incompatível.");
  };
  std::string version, state, dialogue, speaker, music;
  bool loop;
  int64_t end, loopStart;
  double position;
  int pauseKind;
  int restoredMouseCursor = i.assets->settings().integer("MOUSE_CURSOR", -1);
  ReadProgress restoredProgress;
  if (!(in >> version >> std::quoted(state) >> std::quoted(dialogue) >>
        std::quoted(speaker) >> pauseKind >> std::quoted(music) >> loop >>
        end >> loopStart >> position) ||
      (version != "REALLIVE-PRESENTATION-1" &&
       version != "REALLIVE-PRESENTATION-2" &&
       version != "REALLIVE-PRESENTATION-3" &&
       version != "REALLIVE-PRESENTATION-4" &&
       version != "REALLIVE-PRESENTATION-5" &&
       version != "REALLIVE-PRESENTATION-6" &&
       version != "REALLIVE-PRESENTATION-7") ||
      (pauseKind != 17 && pauseKind != 205) || position < 0 ||
      (!std::isfinite(position) ||
       position > double(std::numeric_limits<std::int64_t>::max() / 1000)))
    invalid();
  float gain = 1, fadeFrom = 1, fadeTo = 1;
  int fadeElapsed = 0, fadeDuration = 0;
  bool fadeStops = false;
  if (version != "REALLIVE-PRESENTATION-1" &&
      (!(in >> gain >> fadeFrom >> fadeTo >> fadeElapsed >> fadeDuration >>
         fadeStops) ||
       !std::isfinite(gain) || !std::isfinite(fadeFrom) ||
       !std::isfinite(fadeTo) || gain < 0 || gain > 1 || fadeFrom < 0 ||
       fadeFrom > 1 || fadeTo < 0 || fadeTo > 1 || fadeElapsed < 0 ||
       fadeDuration < 0 || (fadeDuration > 0 && fadeElapsed > fadeDuration)))
    invalid();
  std::map<int, Object> fg, bg;
  std::map<int, SurfacePtr> dc;
  auto objects = [&](std::map<int, Object> &objects) {
    size_t count;
    if (!(in >> count) || count > 65536)
      invalid();
    for (size_t n = 0; n < count; ++n) {
      int id, r, g, b, a;
      Object o;
      if (!(in >> id >> std::quoted(o.file) >> std::quoted(o.text) >> o.x >>
            o.y >> o.pattern >> o.alpha >> o.visible >> o.width >> o.height >>
            r >> g >> b >> a >> o.clipped >> o.clip.x >> o.clip.y >> o.clip.w >>
            o.clip.h) ||
          id < 0 || id > 65535 || o.alpha < 0 || o.alpha > 255 ||
          o.pattern < 0 || o.width < 0 || o.height < 0 || r < 0 || r > 255 ||
          g < 0 || g > 255 || b < 0 || b > 255 || a < 0 || a > 255)
        invalid();
      o.colour = {Uint8(r), Uint8(g), Uint8(b), Uint8(a)};
      if ((version == "REALLIVE-PRESENTATION-3" ||
           version == "REALLIVE-PRESENTATION-4" ||
           (version == "REALLIVE-PRESENTATION-5" ||
            version == "REALLIVE-PRESENTATION-6" ||
            version == "REALLIVE-PRESENTATION-7")) &&
          (!(in >> o.frameMs >> o.animationElapsed) || o.frameMs < 0))
        invalid();
      if (version == "REALLIVE-PRESENTATION-4" ||
          version == "REALLIVE-PRESENTATION-5" ||
          version == "REALLIVE-PRESENTATION-6" ||
          version == "REALLIVE-PRESENTATION-7")
        for (auto &adjustment : o.adjustments)
          if (!(in >> adjustment.x >> adjustment.y))
            invalid();
      if (!o.file.empty())
        i.graphic(o.file);
      if (!objects.emplace(id, std::move(o)).second)
        invalid();
    }
  };
  objects(fg);
  objects(bg);
  size_t count, total = 0;
  if (!(in >> count) || count > 16)
    invalid();
  for (size_t n = 0; n < count; ++n) {
    int id, w, h;
    std::string pixels;
    if (!(in >> id >> w >> h >> std::quoted(pixels)) || id < 0 || id > 15 ||
        w <= 0 || h <= 0 || w > 8192 || h > 8192 ||
        pixels.size() != size_t(w) * h * 4)
      invalid();
    total += pixels.size();
    if (total > 128 * 1024 * 1024)
      invalid();
    auto s = blank(w, h);
    for (int y = 0; y < h; ++y)
      std::memcpy(static_cast<char *>(s->pixels) + y * s->pitch,
                  pixels.data() + size_t(y) * w * 4, size_t(w) * 4);
    if (!dc.emplace(id, std::move(s)).second)
      invalid();
  }
  std::string saveName;
  std::map<int, std::shared_ptr<DCImage>> dcImages;
  in >> std::ws;
  if ((version == "REALLIVE-PRESENTATION-5" ||
       version == "REALLIVE-PRESENTATION-6" ||
       version == "REALLIVE-PRESENTATION-7") &&
      in.eof())
    invalid();
  if (!in.eof()) {
    if (!(in >> std::quoted(saveName)))
      invalid();
    if (version == "REALLIVE-PRESENTATION-5" ||
        version == "REALLIVE-PRESENTATION-6" ||
        version == "REALLIVE-PRESENTATION-7") {
      size_t count;
      if (!(in >> count) || count > 65536)
        invalid();
      std::vector<std::shared_ptr<DCImage>> images(1);
      for (size_t n = 0; n < count; ++n) {
        auto image = std::make_shared<DCImage>();
        int kind;
        size_t source, previous;
        std::string pixels;
        if (!(in >> kind >> image->width >> image->height >> image->alpha >>
              image->mask >> image->crop.x >> image->crop.y >> image->crop.w >>
              image->crop.h >> image->destination.x >> image->destination.y >>
              image->destination.w >> image->destination.h >> source >>
              previous >> std::quoted(image->graphic) >> std::quoted(pixels)) ||
            kind < 0 || kind > int(DCImage::Kind::Gray) || image->width <= 0 ||
            image->height <= 0 || image->width > 8192 || image->height > 8192 ||
            image->alpha < 0 || image->alpha > 255 || source >= images.size() ||
            previous >= images.size())
          invalid();
        image->kind = DCImage::Kind(kind);
        image->source = images[source];
        image->previous = images[previous];
        if ((image->kind == DCImage::Kind::Copy && !image->source) ||
            ((image->kind == DCImage::Kind::Gray ||
              image->kind == DCImage::Kind::Invert) &&
             !image->previous) ||
            (image->kind == DCImage::Kind::Pixels && pixels.empty()))
          invalid();
        if (image->kind == DCImage::Kind::Graphic) {
          if (image->graphic.empty() || image->crop.w != image->width ||
              image->crop.h != image->height)
            invalid();
          i.graphic(image->graphic);
        }
        if (!pixels.empty()) {
          if (pixels.size() != size_t(image->width) * image->height * 4)
            invalid();
          total += pixels.size();
          if (total > 256 * 1024 * 1024)
            invalid();
          image->native = blank(image->width, image->height);
          for (int y = 0; y < image->height; ++y)
            std::memcpy(static_cast<char *>(image->native->pixels) +
                            y * image->native->pitch,
                        pixels.data() + size_t(y) * image->width * 4,
                        size_t(image->width) * 4);
        }
        images.push_back(std::move(image));
      }
      if (!(in >> count) || count != dc.size())
        invalid();
      for (size_t n = 0; n < count; ++n) {
        int id;
        size_t index;
        if (!(in >> id >> index) || !dc.count(id) || index == 0 ||
            index >= images.size() || images[index]->width != dc.at(id)->w ||
            images[index]->height != dc.at(id)->h ||
            !dcImages.emplace(id, images[index]).second)
          invalid();
      }
    }
    if ((version == "REALLIVE-PRESENTATION-6" ||
         version == "REALLIVE-PRESENTATION-7") &&
        !(in >> restoredMouseCursor))
      invalid();
    if (version == "REALLIVE-PRESENTATION-7") {
      try {
        restoredProgress = readReadProgress(in);
      } catch (const std::exception &) {
        invalid();
      }
    }
    in >> std::ws;
    if (!in.eof())
      invalid();
  }
  if (!music.empty() && !i.assets->contains(music))
    invalid();
  i.vm.restore(state);
  if (version != "REALLIVE-PRESENTATION-4" &&
      version != "REALLIVE-PRESENTATION-5" &&
      version != "REALLIVE-PRESENTATION-6" &&
      version != "REALLIVE-PRESENTATION-7") {
    const auto restoreAdjustments = [&](std::map<int, Object> &objects) {
      for (auto &[id, o] : objects)
        for (size_t slot = 0; slot < o.adjustments.size(); ++slot) {
          o.adjustments[slot].x = i.vm.machine().GetIntValue(
              libreallive::IntMemRef(27, 0, id * 32 + int(slot) * 2));
          o.adjustments[slot].y = i.vm.machine().GetIntValue(
              libreallive::IntMemRef(27, 0, id * 32 + int(slot) * 2 + 1));
        }
    };
    restoreAdjustments(fg);
    restoreAdjustments(bg);
  }
  if (saveName.empty()) {
    std::size_t index = 0;
    for (const auto &element : i.archive.Scenario(i.vm.scenario())) {
      if (index++ >= i.vm.index())
        break;
      auto *command =
          dynamic_cast<const libreallive::CommandElement *>(element.get());
      if (command && command->modtype() == 1 && command->module() == 4 &&
          command->opcode() == 0 && command->GetParamCount() > 0)
        saveName = decodeText(StringParameter(i.vm.machine(), *command, 0),
                              i.archive.Scenario(i.vm.scenario()).encoding());
    }
  }
  i.fg.swap(fg);
  i.bg.swap(bg);
  i.dc.swap(dc);
  i.dcImages = std::move(dcImages);
  i.dialogue = std::move(dialogue);
  i.speakerName = std::move(speaker);
  i.saveName = std::move(saveName);
  i.pause = true;
  i.pauseKind = pauseKind;
  if (version == "REALLIVE-PRESENTATION-7") {
    // Loading an older slot cannot undo pages read during this session.
    i.readPages.insert(restoredProgress.readPages.begin(),
                       restoredProgress.readPages.end());
    i.currentPage = restoredProgress.currentPage;
  } else {
    i.currentPage = {i.vm.scenario(), i.vm.index()};
  }
  i.currentRead = i.readPages.count(i.currentPage) != 0;
  if (!i.currentRead)
    i.skipMode = false;
  i.autoDeadline = 0;
  i.deadline = 0;
  i.error.clear();
  i.transitionOld.reset();
  i.transitionTexture.reset();
  i.zoom.reset();
  i.zoomTexture.reset();
  i.choice->hide();
  i.movie->stop();
  i.audioWait.clear();
  i.voice.Stop();
  i.sounds.clear();
  i.soundLevels.clear();
  i.voiceLevels.clear();
  i.bgm.Stop();
  i.musicID.clear();
  auto translatedSpeaker =
      i.engine->GetTranslation().text(i.vm.scenario(), i.speakerName);
  auto translatedDialogue =
      i.engine->GetTranslation().text(i.vm.scenario(), i.dialogue);
  if (translatedDialogue == i.dialogue && !i.dialogue.empty() &&
      i.dialogue.front() == ' ')
    translatedDialogue =
        i.engine->GetTranslation().text(i.vm.scenario(), i.dialogue.substr(1));

  std::string finalSpeaker, finalDialogue;
  processSpeakerAndDialogue(translatedSpeaker, translatedDialogue, finalSpeaker,
                            finalDialogue);
  i.textbox->setDialogue(finalSpeaker, finalDialogue);
  i.textbox->completeDialogue();
  i.textbox->show();
  if (!music.empty())
    i.play(i.bgm, music, "BGM", loop, 0, end, loopStart,
           bgmResumeMilliseconds(position));
  i.bgmGain = gain;
  i.fadeFrom = fadeFrom;
  i.fadeTo = fadeTo;
  i.fadeElapsed = fadeElapsed;
  i.fadeDuration = fadeDuration;
  i.fadeStops = fadeStops;
  i.audioTick = SDL_GetTicks();
  i.bgm.SetGain(gain);
  i.bgmScriptPaused =
      version == "REALLIVE-PRESENTATION-7" && restoredProgress.bgmScriptPaused;
  i.bgm.SetPaused(isStoryPaused() || i.bgmScriptPaused);
  i.selectMouseCursor(restoredMouseCursor);
  if (version == "REALLIVE-PRESENTATION-7")
    i.persistReadHistory();
}

bool RealLiveRuntime::onEvent(EventRef e) {
  if (!impl)
    return Runtime::onEvent(e);
  // Movie playback owns input; no gameplay/UI event can accidentally end it.
  if (impl->movie && impl->movie->isOpen() && !impl->movie->finished()) {
    if (impl->videoSkip.accept(e))
      impl->movie->stop();
    return true;
  }
  impl->videoSkip.reset();

  // Engine sends raw fingers before synthetic SDL_TOUCH_MOUSEID pointer
  // events. Ignore mouse releases from drags, multi-touch and cancellations.
  if (!impl->input.acceptTouch(e)) {
    impl->titleInput.reset();
    if (impl->textbox)
      impl->textbox->controls.reset();
    impl->button1 = impl->button2 = 0;
    return true;
  }

  // Menu/save/backlog own input, and must never inherit a held skip key.
  for (auto *scene : GetActiveScenes())
    if (scene && scene->capturesInput()) {
      impl->input.resetSkip();
      impl->skipHeld = false;
      return Runtime::onEvent(e);
    }

  const bool dialogue = impl->pause && impl->textbox &&
                        impl->textbox->isVisible() &&
                        !impl->textbox->concealed &&
                        !impl->choice->isActive() && impl->focusedEdit < 0;
  if (impl->input.processSkip(e, dialogue)) {
    impl->skipHeld = impl->input.skipHeld();
    return true;
  }
  impl->skipHeld = impl->input.skipHeld();
  return Runtime::onEvent(Kotonoha::MapControllerEvent(e, dialogue));
}
int RealLiveRuntime::translationRoute() const { return impl->vm.scenario(); }
std::vector<int> RealLiveRuntime::translationRoutes() const {
  return impl->archive.ScenarioNumbers();
}
std::vector<std::string> RealLiveRuntime::translationTexts(int route) const {
  std::vector<std::string> result;
  std::set<std::string> seen;
  auto &scene = impl->archive.Scenario(route);
  RLMachine staticMemory;
  auto add = [&](const std::string &raw) {
    if (raw.empty())
      return;
    std::string decoded;
    // Preserve PRINT expressions and native control/name bytes in source keys;
    // evaluate them only during execution, where the correct memory is known.
    try {
      decoded = raw.find(char(0)) != std::string::npos ||
                        raw.compare(0, 9, "###PRINT(") == 0
                    ? libreallive::ParsableToPrintableString(raw)
                    : decodeText(raw, scene.encoding());
    } catch (const std::exception &) {
      // Dynamic name references use CP932 codes outside the western alphabet.
      // Their printable expression form retains the exact bytes for
      // translators.
      decoded = libreallive::ParsableToPrintableString(raw);
    }
    if (!decoded.empty() && seen.insert(decoded).second)
      result.push_back(std::move(decoded));
  };
  for (const auto &element : scene) {
    if (auto *text =
            dynamic_cast<const libreallive::TextoutElement *>(element.get()))
      add(text->GetText());
    auto *command =
        dynamic_cast<const libreallive::CommandElement *>(element.get());
    if (!command)
      continue;
    if (auto *select =
            dynamic_cast<const libreallive::SelectElement *>(command))
      for (const auto &option : select->raw_params())
        add(option.text);
    int parameter = -1;
    if (command->modtype() == 1 && command->module() == 10 &&
        (command->opcode() == 0 || command->opcode() == 2))
      parameter = 1;
    if (command->modtype() == 1 && command->module() == 10 &&
        command->opcode() == 100 && command->overload() == 0)
      parameter = 0;
    if (command->modtype() == 1 && command->module() == 4 &&
        command->opcode() == 0)
      parameter = 0;
    if (command->modtype() == 1 && command->module() == 4 &&
        (command->opcode() == 1301 || command->opcode() == 1311 ||
         command->opcode() == 1710))
      parameter = 1;
    if (parameter >= 0 &&
        static_cast<std::size_t>(parameter) < command->GetParamCount()) {
      auto piece = Parameter(*command, parameter);
      if (!piece.IsMemoryReference() &&
          piece.GetExpressionValueType() == libreallive::ValueTypeString)
        add(piece.GetStringValue(staticMemory));
    }
  }
  return result;
}
Translation::SourceTexts RealLiveRuntime::translationSources() const {
  Translation::SourceTexts result;
  for (int route : translationRoutes())
    result[route] = translationTexts(route);
  return result;
}
std::vector<std::string> RealLiveRuntime::activeTranslationKeys() const {
  std::vector<std::string> keys;
  if (!impl->speakerName.empty())
    keys.push_back(impl->speakerName);
  if (!impl->dialogue.empty())
    keys.push_back(impl->dialogue);
  return keys;
}
std::string RealLiveRuntime::debugNode() const {
  return std::to_string(impl->vm.scenario()) + ":" +
         std::to_string(impl->vm.index());
}
std::string RealLiveRuntime::nodeClockKey() const {
  return std::to_string(impl->vm.scenario());
}
std::vector<DebugNode> RealLiveRuntime::debugNodes() const {
  std::vector<DebugNode> nodes;
  for (auto n : impl->archive.ScenarioNumbers())
    nodes.push_back({std::to_string(n), "SEEN " + std::to_string(n), {}});
  return nodes;
}
std::vector<DebugAudioAsset> RealLiveRuntime::debugSceneAudio() {
  std::vector<DebugAudioAsset> result;
  for (const auto &element : impl->archive.Scenario(impl->vm.scenario())) {
    const auto *command =
        dynamic_cast<const libreallive::CommandElement *>(element.get());
    if (!command || command->modtype() != 1 || command->module() != 23 ||
        command->opcode() != 0 || command->GetParamCount() == 0)
      continue;
    try {
      const int voice = IntegerParameter(impl->vm.machine(), *command, 0);
      if (voice <= 0)
        continue;
      auto id = "voice/" + std::to_string(voice) + ".wav";
      if (std::none_of(result.begin(), result.end(),
                       [&](const auto &item) { return item.id == id; }))
        result.push_back({std::move(id), {}});
    } catch (const std::exception &) {
      // Dynamic voice expressions still appear when actually played through
      // AssetProvider::noteAudio; static scene enumeration remains safe.
    }
  }
  return result;
}
std::optional<Kotonoha::AudioClip>
RealLiveRuntime::debugOriginalAudio(const std::string &id) {
  constexpr std::string_view prefix = "voice/";
  constexpr std::string_view suffix = ".wav";
  if (id.size() <= prefix.size() + suffix.size() ||
      id.compare(0, prefix.size(), prefix) != 0 ||
      id.compare(id.size() - suffix.size(), suffix.size(), suffix) != 0)
    return std::nullopt;
  int voice = 0;
  const auto first = id.data() + prefix.size();
  const auto last = id.data() + id.size() - suffix.size();
  auto parsed = std::from_chars(first, last, voice);
  if (parsed.ec != std::errc{} || parsed.ptr != last || voice <= 0)
    return std::nullopt;
  auto bytes = impl->voices.read(voice);
  if (!bytes)
    return std::nullopt;
  auto encoded = bytes->size() >= 4 && (*bytes)[0] == 'O' &&
                         (*bytes)[1] == 'g' && (*bytes)[2] == 'g' &&
                         (*bytes)[3] == 'S'
                     ? std::move(*bytes)
                     : nwaToWav(*bytes);
  Kotonoha::AudioClip clip;
  if (!Kotonoha::MediaDecoder::tryDecodeAudio(encoded, clip))
    return std::nullopt;
  impl->assets->noteAudio(id, clip);
  return clip;
}
void RealLiveRuntime::debugJumpToNode(const std::string &s) {
  auto colon = s.find(':');
  if (colon == s.npos)
    impl->vm.jump(std::stoi(s));
  else
    impl->vm.seek(std::stoi(s.substr(0, colon)),
                  std::stoull(s.substr(colon + 1)));
  impl->deadline = 0;
  impl->editFields.clear();
  impl->focusedEdit = -1;
  if (impl->engine)
    SDL_StopTextInput(&impl->engine->GetWindow());
  impl->error.clear();
  impl->pause = false;
  impl->dialogue.clear();
  if (impl->choice)
    impl->choice->hide();
  if (impl->movie)
    impl->movie->stop();
  impl->canvasTexture.reset();
  impl->canvasPixels.reset();
  impl->transitionOld.reset();
  impl->transitionTexture.reset();
  impl->zoom.reset();
  impl->zoomTexture.reset();
  if (impl->textbox)
    impl->textbox->hide();
}
void RealLiveRuntime::debugRestartScene() {
  debugJumpToNode(std::to_string(impl->vm.scenario()));
}
bool RealLiveRuntime::debugStep() {
  auto &i = *impl;
  try {
    return i.vm.step();
  } catch (const std::exception &e) {
    i.error = "REALLIVE " + debugNode() + ": " + e.what();
    SDL_Log("%s", i.error.c_str());
    return false;
  }
}
SDL_FPoint RealLiveRuntime::sceneSize(const Scene &s) const {
  return s.designSpace()
             ? SDL_FPoint{1280, 720}
             : SDL_FPoint{(float)impl->canvasW, (float)impl->canvasH};
}
} // namespace RealLive
