#include "SummerRuntime.hpp"
#include "SummerBacklog.hpp"
#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/ImageButtonBar.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/TextInputForm.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/components/VNSaveMenu.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SummerRadish/ImoutoScript.hpp>
#include <SummerRadish/Map.hpp>
#include <SummerRadish/Native.hpp>
#include <SummerRadish/Script.hpp>
#include <SummerRadish/Transition.hpp>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>

namespace SummerRadish {
using namespace Kotonoha;
namespace {
class SummerText final : public VNTextBox {
  Kotonoha::Providers::SummerAssets &assets;
  std::unique_ptr<ClickMap> controlMap;
  // SRV1 uses GRAPHIC/FRAME/MESSFRAME, SRV2/LostM use
  // GRAPHIC/MESFRAME/MESFRAME.
  std::string frameTexb, frameCmap, selectTexb, nameTexb;
  bool lostM = false;
  bool srv2 = false;
  bool imouto = false;
  bool meo = false;
  int nameId = -1;
  bool useCustomPlayerName = false;
  SDL_Color actorColor{240, 240, 245, 255};
  void renderFrame(RendererRef r) override {
    if (meo) {
      // MEO renders dialogue subtitles over MPEG-1 video without a TEXB frame.
      SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&r, 10, 12, 22, 190);
      const SDL_FRect bar{20, 418, 760, 162};
      SDL_RenderFillRect(&r, &bar);
      SDL_SetRenderDrawColor(&r, 255, 255, 255, 65);
      SDL_RenderRect(&r, &bar);
      return;
    }
    // The normal frame is already composited by the stage. Overlay the
    // original atlas states without rebuilding/upscaling the whole canvas.
    if (assets.contains(frameTexb)) {
      auto &atlas = assets.texture(r, frameTexb);
      if (lostM || srv2) {
        static constexpr SDL_FRect lostMBases[7] = {
            {0, 248, 97, 13},  {0, 334, 92, 13}, {0, 200, 92, 13},
            {0, 308, 97, 13},  {0, 321, 97, 13}, {0, 235, 92, 13},
            {394, 248, 16, 20}};
        static constexpr SDL_FRect srv2Bases[7] = {
            {0, 288, 81, 20},  {0, 348, 106, 20}, {0, 211, 95, 20},
            {0, 308, 87, 20},  {0, 328, 79, 20},  {0, 232, 46, 54},
            {379, 288, 31, 10}};
        const auto *bases = lostM ? lostMBases : srv2Bases;
        for (int i = 0; i < 7 && i < int(controls.buttons.size()); ++i) {
          const int state = controls.state(i);
          const bool active = (i == 0 && skipActive && skipActive()) ||
                              (i == 5 && autoActive && autoActive());
          if (!state && !active)
            continue;
          const int col = active ? (state ? 3 : 2) : state;
          SDL_FRect source{bases[i].x + float(col) * bases[i].w, bases[i].y,
                           bases[i].w, bases[i].h};
          RenderUpscaledTexture(&r, &atlas, &source, &controls.buttons[i].area);
        }
        return;
      }
      for (int i = 0; i < 6; ++i) {
        const int state = controls.state(i);
        const bool active = (i == 0 && skipActive && skipActive()) ||
                            (i == 2 && autoActive && autoActive());
        if (!state && !active)
          continue;
        SDL_FRect source{};
        if (i == 0 || i == 2)
          source = {float((active ? 192 : 0) + (state ? 96 : 0)),
                    float(i == 0 ? 250 : 268), 96, 18};
        else if (i == 1 || i == 3)
          source = {480, float(i == 1 ? 250 : 268), 96, 18};
        else if (i == 4)
          source = {594, 250, 18, 18};
        else
          source = {float(612 + state * 46), 224, 46, 62};
        RenderUpscaledTexture(&r, &atlas, &source, &controls.buttons[i].area);
      }
    }
  }
  void renderSpeaker(RendererRef r) override {
    if (lostM) {
      if (nameId < 0)
        return;
      if (assets.contains(frameTexb)) {
        auto &atlas = assets.texture(r, frameTexb);
        SDL_FRect baseSrc{440, 200, 218, 35};
        SDL_FRect baseDst{25, 391, 218, 35};
        RenderUpscaledTexture(&r, &atlas, &baseSrc, &baseDst);
      }
      if (nameId == 0 && useCustomPlayerName) {
        engine.GetVNUI().text(r, speakerName, 40, 396, 188,
                              engine.GetSettings().vnTextColorOverride
                                  ? engine.GetSettings().vnSpeakerColor
                                  : SDL_Color{240, 240, 240, 255});
      } else if (assets.contains(nameTexb)) {
        auto &nameAtlas = assets.texture(r, nameTexb);
        float nw = 0, nh = 0;
        SDL_GetTextureSize(&nameAtlas, &nw, &nh);
        const float srcY = float(nameId * 34);
        if (srcY + 34.0f <= nh) {
          SDL_FRect src{0, srcY, std::min(188.0f, nw), 34.0f};
          SDL_FRect dst{40, 391, src.w, 34.0f};
          RenderUpscaledTexture(&r, &nameAtlas, &src, &dst);
        }
      }
      return;
    }
    const float speakerX = imouto ? 62.0f : (meo ? 38.0f : 24.0f);
    const float speakerY = imouto ? 454.0f : (meo ? 426.0f : 400.0f);
    const float speakerW = imouto ? 676.0f : (meo ? 724.0f : 650.0f);
    engine.GetVNUI().text(r, speakerName, speakerX, speakerY, speakerW,
                          engine.GetSettings().vnTextColorOverride
                              ? engine.GetSettings().vnSpeakerColor
                              : SDL_Color{240, 240, 245, 255});
  }
  void renderDialogue(RendererRef r) override {
    auto measure = [&](const std::string &value) {
      return engine.GetVNUI().textWidth(value);
    };
    const float boxX = lostM ? 80.0f : (imouto ? 62.0f : (meo ? 38.0f : 20.0f));
    const float boxY =
        lostM ? 434.0f : (imouto ? 486.0f : (meo ? 460.0f : 444.0f));
    const float boxW =
        lostM ? 644.0f : (imouto ? 676.0f : (meo ? 724.0f : 710.0f));
    const SDL_Color color = engine.GetSettings().vnTextColorOverride
                                ? engine.GetSettings().vnTextColor
                                : actorColor;
    const auto lines = VNTextBoxUtility::RevealLines(
        fullDialogue, visibleDialogue.size(), measure, boxW);
    for (std::size_t i = 0; i < lines.size(); ++i)
      engine.GetVNUI().text(r, lines[i], boxX, boxY + i * 28, boxW, color);
  }
  void renderIndicator(RendererRef r) override {
    if (assets.contains(frameTexb)) {
      auto &atlas = assets.texture(r, frameTexb);
      if (lostM) {
        const bool isAuto = autoActive && autoActive();
        const float frameX = isAuto
                                 ? float(70 + ((SDL_GetTicks() / 200) % 4) * 7)
                                 : float(((SDL_GetTicks() / 120) % 7) * 7);
        SDL_FRect source{frameX, 347, 7, 87};
        SDL_FRect target{740, 473, 7, 87};
        RenderUpscaledTexture(&r, &atlas, &source, &target);
        return;
      }
      SDL_FRect source{float(256 + ((SDL_GetTicks() / 200) % 4) * 16), 210, 16,
                       40};
      SDL_FRect target{719, 526, 16, 40};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
    }
  }

public:
  ImageButtonBar controls;
  bool concealed = false;
  std::function<bool()> autoActive, skipActive;
  void setLostMMetadata(int id, bool customName, SDL_Color color) {
    nameId = id;
    useCustomPlayerName = customName;
    actorColor = color;
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
  SummerText(Engine &e, Kotonoha::Providers::SummerAssets &a)
      : VNTextBox(e, "", "", 40), assets(a), lostM(a.isLostM()),
        srv2(!a.isLostM() && a.contains("GRAPHIC/MESFRAME/MESFRAME.TEXB")),
        imouto(a.isImoutoDeIkou()),
        meo(!a.isImoutoDeIkou() && a.contains("SCRIPT/A-00.XML")) {
    // SRV2/LostM use GRAPHIC/MESFRAME/*; SRV1 uses GRAPHIC/FRAME/*.
    const bool useMesframe = lostM || srv2;
    const std::string prefix =
        useMesframe ? "GRAPHIC/MESFRAME/" : "GRAPHIC/FRAME/";
    frameTexb = prefix + (useMesframe ? "MESFRAME.TEXB" : "MESSFRAME.TEXB");
    frameCmap = prefix + (useMesframe ? "MESFRAME.CMAP" : "MESSFRAME.CMAP");
    selectTexb = prefix + "SELECT.TEXB";
    nameTexb = prefix + "NAME.TEXB";
    hide();
    if (auto cmap = assets.tryReadAsset(frameCmap))
      controlMap = std::make_unique<ClickMap>(std::move(*cmap));
    if (lostM) {
      controls.hitTest = [this](SDL_FPoint p) {
        const int id =
            controlMap ? controlMap->value(int(p.x) - 64, int(p.y) - 426) : -1;
        return id >= 1 && id <= 7 ? id - 1 : -1;
      };
      controls.buttons = {{{443, 563, 97, 13}, {}}, {{612, 580, 92, 13}, {}},
                          {{502, 580, 92, 13}, {}}, {{557, 563, 97, 13}, {}},
                          {{669, 563, 97, 13}, {}}, {{392, 580, 92, 13}, {}},
                          {{733, 432, 16, 20}, {}}};
    } else if (srv2) {
      controls.hitTest = [this](SDL_FPoint p) {
        const int id =
            controlMap ? controlMap->value(int(p.x), int(p.y) - 390) : -1;
        return id >= 1 && id <= 7 ? id - 1 : -1;
      };
      controls.buttons = {{{396, 576, 81, 20}, {}}, {{465, 576, 106, 20}, {}},
                          {{559, 576, 95, 20}, {}}, {{642, 576, 87, 20}, {}},
                          {{717, 576, 79, 20}, {}}, {{750, 522, 46, 54}, {}},
                          {{765, 512, 31, 10}, {}}};
    } else {
      controls.hitTest = [this](SDL_FPoint p) {
        const int id =
            controlMap ? controlMap->value(int(p.x), int(p.y) - 391) : -1;
        return id >= 1 && id <= 6 ? id - 1 : -1;
      };
      controls.buttons = {{{417, 576, 96, 18}, {}}, {{507, 576, 96, 18}, {}},
                          {{597, 576, 96, 18}, {}}, {{687, 576, 96, 18}, {}},
                          {{778, 577, 18, 18}, {}}, {{750, 515, 46, 62}, {}}};
    }
  }
  bool designSpace() const override { return false; }
  bool handleEvent(EventRef e) override {
    return isVisible() && !concealed && controls.handle(e);
  }
};
class SummerChoice final : public ChoiceQuestionScene {
  Kotonoha::Providers::SummerAssets &assets;
  std::string selectTexb;
  std::vector<bool> readStates;
  bool pointerFocus = false, keyboardFocus = false;
  void layout() override {
    if (nativeMode >= 0) {
      ChoiceQuestionScene::layout();
      return;
    }
    float width = 16;
    for (const auto &option : options)
      width = std::max(
          width, engine.GetUI().measureSized(option.label, 10000, 28).x + 16);
    width = std::min(736.0f, width);
    const float left = (800 - width) / 2 - 32;
    float y = 300 - (50 * options.size()) / 2.0f + 4 - scroll;
    for (auto &option : options) {
      option.hitBox = {left, y, width + 64, 42};
      y += 50;
    }
  }
  void renderOption(RendererRef r, std::size_t index, bool focused) override {
    focused = focused && (pointerFocus || keyboardFocus);
    const auto &rect = options[index].hitBox;
    const bool read = index < readStates.size() && readStates[index];
    const float capL = assets.isLostM() ? 20.0f : 32.0f;
    const float midW = assets.isLostM() ? 18.0f : 16.0f;
    const float capR = assets.isLostM() ? 20.0f : 32.0f;
    const float stride = capL + midW + capR;
    const float secondOffset = assets.isLostM() ? 116.0f : 160.0f;
    const float start =
        (read ? secondOffset : 0.0f) + (focused ? stride : 0.0f);
    if (assets.contains(selectTexb)) {
      auto &atlas = assets.texture(r, selectTexb);
      SDL_FRect source{start, 0, capL, 42}, target{rect.x, rect.y, capL, 42};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
      source = {start + capL, 0, midW, 42};
      target = {rect.x + capL, rect.y, rect.w - capL - capR, 42};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
      source = {start + capL + midW, 0, capR, 42};
      target = {rect.x + rect.w - capR, rect.y, capR, 42};
      RenderUpscaledTexture(&r, &atlas, &source, &target);
    } else {
      SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&r, focused ? 72 : 20, focused ? 46 : 24,
                             focused ? 116 : 42, focused ? 240 : 215);
      SDL_RenderFillRect(&r, &rect);
      SDL_SetRenderDrawColor(&r, focused ? 255 : 180, focused ? 225 : 190,
                             focused ? 150 : 220, focused ? 235 : 160);
      SDL_RenderRect(&r, &rect);
    }
    const float maxTextW = std::max(64.0f, rect.w - capL - capR);
    float fontSize = 28.0f;
    SDL_FPoint size =
        engine.GetUI().measureSized(options[index].label, 10000.0f, fontSize);
    while (size.x > maxTextW && fontSize > 16.0f) {
      fontSize -= 2.0f;
      size =
          engine.GetUI().measureSized(options[index].label, 10000.0f, fontSize);
    }
    const SDL_Color color = read ? (focused ? SDL_Color{255, 240, 128, 255}
                                            : SDL_Color{204, 192, 96, 255})
                                 : (focused ? SDL_Color{240, 240, 240, 255}
                                            : SDL_Color{192, 192, 192, 255});
    engine.GetUI().textSized(
        r, options[index].label, rect.x + (rect.w - size.x) / 2,
        rect.y + (42 - size.y) / 2, maxTextW, fontSize, color);
  }
  SDL_FRect listArea() const override { return {0, 0, 800, 600}; }
  void renderPanel(RendererRef r) override {
    if (!questionText.empty()) {
      engine.GetUI().panel(r, {60, 70, 680, 65});
      engine.GetUI().text(r, questionText, 80, 85, 640);
    }
  }
  std::unique_ptr<ClickMap> hitmap;
  int nativeMode = -1, focus = -1, uniformPhase = 0, uniformIndex = 0;
  bool isSrv2 = false;
  bool pointerDownInChoice = false;
  Uint64 uniformPhaseStart = 0;
  float oldX = -100, oldY = -100;

public:
  std::function<void(const std::string &)> nativeSound;
  std::function<bool()> nativeSoundPlaying;
  explicit SummerChoice(Engine &e, Kotonoha::Providers::SummerAssets &a)
      : ChoiceQuestionScene(e, "", {}, nullptr), assets(a) {
    isSrv2 = assets.contains("GRAPHIC/MESFRAME/MESFRAME.TEXB");
    selectTexb =
        isSrv2 ? "GRAPHIC/MESFRAME/SELECT.TEXB" : "GRAPHIC/FRAME/SELECT.TEXB";
  }
  void setReadStates(std::vector<bool> states) {
    readStates = std::move(states);
  }
  bool designSpace() const override { return false; }
  int mode() const { return active ? nativeMode : -1; }
  int focused() const { return focus; }
  void hide() override {
    ChoiceQuestionScene::hide();
    nativeMode = -1;
    hitmap.reset();
  }
  void setQuestion(const std::string &q, const std::vector<ChoiceOption> &o,
                   std::function<void(int, const std::string &)> cb) override {
    nativeMode = -1;
    readStates.clear();
    pointerFocus = keyboardFocus = false;
    hitmap.reset();
    ChoiceQuestionScene::setQuestion(q, o, std::move(cb));
  }
  void setNative(int mode, Kotonoha::Providers::SummerAssets &assets,
                 std::function<void(int, const std::string &)> cb) {
    // mode == 100 represents UNISELECT so SRV2 MAPSELECT(0..3) doesn't collide.
    const bool uniform = (mode == 100);
    nativeMode = mode;
    focus = uniform ? 0 : -1;
    uniformPhase = 0;
    pointerDownInChoice = false;
    uniformPhaseStart = SDL_GetTicks();
    oldX = oldY = -100;
    const auto id = uniform ? "GRAPHIC/UNIFORM/UNICHOICE.CMAP"
                            : mapLayout(mode, isSrv2).hitmap;
    hitmap = std::make_unique<ClickMap>(assets.read(id));
    std::vector<ChoiceOption> list;
    if (uniform)
      for (int i = 0; i < 3; ++i)
        list.push_back({std::to_string(i), i});
    else
      for (const auto &location : mapLayout(mode, isSrv2).locations)
        list.push_back(
            {mapCharacterVariable(location.result, isSrv2), location.result});
    ChoiceQuestionScene::setQuestion("", list, std::move(cb));
    if (uniform && nativeSound)
      nativeSound(isSrv2 ? "SE/SELUNI/000" : "systemmsg_0000");
  }
  std::string nativeBackground() const {
    if (nativeMode == 100) {
      if (uniformPhase) {
        if (isSrv2) {
          static const char *srv2Uniforms[3] = {
              "GRAPHIC/UNIFORM/DECCRU.CGD",
              "GRAPHIC/UNIFORM/DECLEM.CGD",
              "GRAPHIC/UNIFORM/DECSUK.CGD",
          };
          if (uniformIndex >= 0 && uniformIndex < 3 &&
              assets.contains(srv2Uniforms[uniformIndex]))
            return srv2Uniforms[uniformIndex];
        }
        const std::string kakunin = std::string("GRAPHIC/UNIFORM/KAKUNIN_") +
                                    char('A' + uniformIndex) + ".CGD";
        if (assets.contains(kakunin))
          return kakunin;
        return "GRAPHIC/UNIFORM/UNICHOICE.CGD";
      }
      return "GRAPHIC/UNIFORM/UNICHOICE.CGD";
    }
    return nativeMode >= 0 ? mapLayout(nativeMode, isSrv2).background : "";
  }
  void compose(Image &canvas,
               const std::function<const Image &(const std::string &)> &image,
               const Machine &vm) const {
    if (!active || nativeMode < 0)
      return;
    if (nativeMode != 100) {
      const auto &layout = mapLayout(nativeMode, isSrv2);
      if (focus >= 0 && focus < int(layout.locations.size())) {
        const auto &item = layout.locations[focus];
        compositeImageRegion(canvas, image(layout.texture), item.position.left,
                             item.position.top, item.source.left,
                             item.source.top,
                             item.source.right - item.source.left,
                             item.source.bottom - item.source.top);
        if (isSrv2 && assets.contains("GRAPHIC/MAP/MAPNAME.TEXB")) {
          const auto &mapName = image("GRAPHIC/MAP/MAPNAME.TEXB");
          if (focus * 75 + 75 <= mapName.height && mapName.width >= 210)
            compositeImageRegion(canvas, mapName, 28, 494, 0, focus * 75, 210,
                                 75);
        }
      }
      for (auto &item : layout.locations) {
        auto v =
            vm.variables.find("@" + mapCharacterVariable(item.result, isSrv2));
        int actor = v == vm.variables.end() ? 0 : int(number(v->second));
        int face = mapFaceIndex(
            nativeMode, size_t(&item - layout.locations.data()), actor, isSrv2);
        const int maxFaces = isSrv2 ? 6 : 8;
        const int faceW = isSrv2 ? 90 : 98;
        const int faceH = isSrv2 ? 90 : 62;
        if (face >= 0 && face < maxFaces &&
            assets.contains("GRAPHIC/MAP/FACE.TEXB")) {
          const auto &faceTex = image("GRAPHIC/MAP/FACE.TEXB");
          if (face * faceH + faceH <= faceTex.height && faceTex.width >= faceW)
            compositeImageRegion(canvas, faceTex, item.face.x, item.face.y, 0,
                                 face * faceH, faceW, faceH);
        }
      }
    } else if (uniformPhase == 0) {
      if (focus >= 0 && focus < 3) {
        static const int x[3] = {220, 20, 420};
        const auto &tex = image("GRAPHIC/UNIFORM/UNICHOICE.TEXB");
        const int dstY = (isSrv2 || tex.height >= 600) ? 0 : 52;
        const int srcH = std::min(tex.height, 600 - dstY);
        const int colW = (tex.width >= 3 * 362) ? 361 : (tex.width / 3);
        const int stride = (tex.width >= 3 * 362) ? 362 : colW;
        compositeImageRegion(canvas, tex, x[focus], dstY, focus * stride, 0,
                             colW, srcH);
      }
    } else if (uniformPhase == 2) {
      const std::string kakuninTex = "GRAPHIC/UNIFORM/KAKUNIN.TEXB";
      if (assets.contains(kakuninTex)) {
        const auto &texture = image(kakuninTex);
        compositeImageRegion(canvas, texture, 208, 362, 0, 0, 386, 90);
        if (focus >= 0)
          compositeImageRegion(canvas, texture, focus == 0 ? 288 : 414, 413,
                               focus * 100, 90, 100, 28);
      }
    }
  }
  bool onEvent(EventRef input) override {
    const auto e = MapControllerEvent(input);
    if (nativeMode < 0) {
      if (e.type == SDL_EVENT_MOUSE_MOTION && active) {
        layout();
        pointerFocus = false;
        keyboardFocus = false;
        SDL_FPoint point{e.motion.x, e.motion.y};
        for (std::size_t i = 0; i < options.size(); ++i)
          if (SDL_PointInRectFloat(&point, &options[i].hitBox)) {
            selected = i;
            pointerFocus = true;
          }
      }
      if (e.type == SDL_EVENT_KEY_DOWN)
        keyboardFocus = true;
      return ChoiceQuestionScene::onEvent(e);
    }
    if (BackAction(engine, e)) {
      if (nativeMode == 100 && uniformPhase) {
        uniformPhase = 0;
        focus = uniformIndex;
      } else
        openMenu();
      return true;
    }
    if (!active)
      return false;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        e.button.button == SDL_BUTTON_LEFT)
      pointerDownInChoice = true;
    bool confirm = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                   (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE);
    bool mouse = e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                 e.button.button == SDL_BUTTON_LEFT && pointerDownInChoice;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
        e.button.button == SDL_BUTTON_LEFT)
      pointerDownInChoice = false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_ESCAPE) {
        if (nativeMode == 100 && uniformPhase) {
          uniformPhase = 0;
          focus = uniformIndex;
          return true;
        }
        openMenu();
        return true;
      }
      int direction = e.key.key == SDLK_UP      ? 0
                      : e.key.key == SDLK_DOWN  ? 1
                      : e.key.key == SDLK_LEFT  ? 2
                      : e.key.key == SDLK_RIGHT ? 3
                                                : -1;
      if (direction >= 0) {
        if (nativeMode != 100) {
          const int prev = focus;
          focus = mapNavigate(nativeMode, focus, direction, isSrv2);
          if (focus != prev && focus >= 0 && isSrv2 && nativeSound)
            nativeSound("SE/SELMAP/016");
        } else if (uniformPhase == 2)
          focus = focus == 0 ? 1 : 0;
        else if (uniformPhase == 0) {
          static const int up[4] = {2, 1, 2, 0}, down[4] = {1, 2, 0, 1};
          focus = (direction == 0 || direction == 2 ? up : down)[focus + 1];
        }
      }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION &&
        (std::abs(e.motion.x - oldX) > 3 || std::abs(e.motion.y - oldY) > 3)) {
      if (nativeMode == 100 && uniformPhase == 2)
        focus = (e.motion.y >= 413 && e.motion.y < 441)
                    ? (e.motion.x >= 288 && e.motion.x < 388   ? 0
                       : e.motion.x >= 414 && e.motion.x < 514 ? 1
                                                               : -1)
                    : -1;
      else if (!uniformPhase) {
        const int prev = focus;
        focus = hitmap->locationIndex(int(e.motion.x), int(e.motion.y),
                                      options.size());
        if (focus != prev && focus >= 0 && nativeMode != 100 && isSrv2 &&
            nativeSound)
          nativeSound("SE/SELMAP/016");
      }
      oldX = e.motion.x;
      oldY = e.motion.y;
    }
    if (mouse) {
      if (nativeMode == 100 && uniformPhase == 1)
        confirm = true;
      else if (nativeMode == 100 && uniformPhase == 2)
        confirm = focus >= 0 && e.button.y >= 413 && e.button.y < 441 &&
                  e.button.x >= (focus == 0 ? 288 : 414) &&
                  e.button.x < (focus == 0 ? 388 : 514);
      else
        confirm = focus >= 0 &&
                  hitmap->locationIndex(int(e.button.x), int(e.button.y),
                                        options.size()) == focus;
    }
    if (!confirm)
      return false;
    if (nativeMode != 100) {
      if (focus >= 0) {
        if (isSrv2 && nativeSound)
          nativeSound("SE/SELMAP/018");
        select(size_t(focus));
      }
      return true;
    }
    if (uniformPhase == 0 && focus >= 0) {
      uniformIndex = focus;
      uniformPhase = 1;
      uniformPhaseStart = SDL_GetTicks();
      if (nativeSound) {
        if (isSrv2) {
          // Verified from SummerRadish2.sdll: 0=deccru->004, 1=declem->002,
          // 2=decsuk->006.
          static const char *srv2UniformVoices[3] = {
              "SE/SELUNI/004", "SE/SELUNI/002", "SE/SELUNI/006"};
          nativeSound(srv2UniformVoices[std::clamp(uniformIndex, 0, 2)]);
        } else {
          // Verified from SummerRadish.sdll: 0->0004, 1->0008, 2->0012.
          static const char *srv1UniformVoices[3] = {
              "systemmsg_0004", "systemmsg_0008", "systemmsg_0012"};
          nativeSound(srv1UniformVoices[std::clamp(uniformIndex, 0, 2)]);
        }
      }
      return true;
    }
    if (uniformPhase == 1) {
      const Uint64 now = SDL_GetTicks();
      const Uint64 elapsed =
          now >= uniformPhaseStart ? now - uniformPhaseStart : 0;
      // Let the outfit-name voice line play completely (matching the native
      // crossfade wipe + voice delivery) before opening the confirm dialog.
      if (elapsed < 450 ||
          (nativeSoundPlaying && nativeSoundPlaying() && elapsed < 3000))
        return true;
      uniformPhase = 2;
      uniformPhaseStart = now;
      focus = -1;
      if (nativeSound)
        nativeSound(isSrv2 ? "SE/SELUNI/008" : "systemmsg_0016");
      return true;
    }
    if (uniformPhase == 2 && focus == 0) {
      if (nativeSound)
        nativeSound(isSrv2 ? "SE/SELUNI/014" : "systemmsg_0020");
      select(size_t(uniformIndex));
      return true;
    }
    if (uniformPhase == 2 && focus == 1) {
      uniformPhase = 0;
      uniformPhaseStart = SDL_GetTicks();
      focus = uniformIndex;
      if (nativeSound)
        nativeSound(isSrv2 ? "SE/SELUNI/010" : "systemmsg_0024");
      return true;
    }
    return false;
  }
  void render(RendererRef r, float scale) override {
    if (nativeMode < 0)
      ChoiceQuestionScene::render(r, scale);
  }
};
class Stage final : public Scene {
public:
  std::function<void(RendererRef, float)> draw;
  std::function<bool(EventRef)> event;
  explicit Stage(Engine &e) : Scene(e) {}
  void render(RendererRef r, float s) override {
    if (draw)
      draw(r, s);
  }
  bool handleEvent(EventRef e) override { return event ? event(e) : false; }
};
struct Sprite {
  std::string id;
  int x = 0;
  int32_t requestedPosition = -1;
  unsigned positionSlot = 0;
  uint64_t creationOrder = 0;
};
std::string stem(std::string name) {
  return name.substr(0, name.find_last_of('.'));
}
} // namespace
struct SummerRuntime::Impl {
  SummerRuntime &runtime;
  std::shared_ptr<Kotonoha::Providers::SummerAssets> assets;
  Engine *engine = nullptr;
  Machine vm;
  std::unique_ptr<Stage> stage;
  std::unique_ptr<SummerText> text;
  std::unique_ptr<SummerChoice> choice;
  std::unique_ptr<VNSaveMenu> saves;
  PcmPlayer bgm, se, voice;
  std::unique_ptr<MediaPlayer> movie;
  VideoSkipGesture videoSkip;
  std::string background = "GRAPHIC/黒.BGD", music, pending, byteOverlay,
              lastVoice;
  bool skipMode = false;
  std::map<int, Sprite> sprites;
  uint64_t nextSpriteOrder = 0;
  std::map<std::string, Image> decodedImages;
  std::map<std::string, std::vector<CharacterAnimation>> characterAnimations;
  TexturePtr canvasTexture;
  Uint64 canvasContentSerial = 0;
  Image presented, transitionFrom, lastCanvas;
  int transitionType = 0, transitionOption = 0, transitionDuration = 0;
  uint64_t transitionStart = 0;
  int bgOffsetX = 0, bgOffsetY = 0;
  int scrollStartX = 0, scrollStartY = 0, scrollTargetX = 0, scrollTargetY = 0;
  int scrollDuration = 0;
  uint64_t scrollStart = 0;
  int currentMessageActor = 0;
  std::vector<int> extraVoiceActors_;
  uint64_t bgmFadeStart = 0;
  int bgmFadeDuration = 0;
  std::string nextMusic;
  bool waitSe = false, waitSkippable = true;
  std::string canvasKey;
  std::map<std::string, std::pair<int, int>> origins;
  std::vector<std::string> speakers;
  std::vector<SDL_Color> speakerColors;
  // SRV1: {0, -200, 200}; SRV2/LostM: parsed from GRAPHIC.INI Position line.
  std::vector<int> positionOffsets = {0, -200, 200};
  int quakeAxis = 0; // 1 = X, 2 = Y
  int quakeAmp = 0, quakeCycles = 0, quakeStepMs = 16;
  uint64_t quakeStart = 0;
  int quakeOffsetX = 0, quakeOffsetY = 0;
  std::array<std::string, 4> defaultNames{};
  TextInputForm *nameForm = nullptr;
  static std::string nameKey(const char *prefix, size_t index) {
    return std::string("$") + prefix + "0" + char('0' + index);
  }
  std::array<std::string, 4> playerNames() const {
    auto names = defaultNames;
    for (size_t i = 0; i < 4; ++i) {
      auto found = vm.variables.find(nameKey("PlayerName", i));
      if (found != vm.variables.end())
        names[i] = string(found->second);
    }
    return names;
  }
  void setDefaultNames() {
    auto names = defaultNames;
    for (size_t i = 0; i < 4; ++i) {
      auto found = vm.variables.find(nameKey("ChangeName", i));
      if (found != vm.variables.end() && !string(found->second).empty())
        names[i] = string(found->second);
      vm.variables[nameKey("PlayerName", i)] = names[i];
    }
    vm.variables["$ChangePlayerName"] =
        int64_t(playerNameChangeMask(names, defaultNames));
  }
  void enterNames() {
    setDefaultNames();
    auto names = playerNames();
    std::vector<TextInputForm::Field> fields;
    const std::array<std::string, 4> labels{"姓", "名", "姓（かな）",
                                            "名（かな）"};
    for (size_t i = 0; i < 4; ++i)
      fields.push_back({labels[i], names[i], defaultNames[i]});
    nameForm = &engine->sharedScene<TextInputForm>();
    nameForm->configure(
        "名前入力", std::move(fields),
        [this](const auto &values) {
          std::array<std::string, 4> names;
          try {
            for (size_t i = 0; i < 4; ++i)
              names[i] = normalizePlayerName(values[i]);
          } catch (const std::invalid_argument &error) {
            return std::string(error.what());
          }
          for (size_t i = 0; i < 4; ++i) {
            vm.variables[nameKey("PlayerName", i)] = names[i];
            vm.variables[nameKey("ChangeName", i)] = names[i];
          }
          vm.variables["$ChangePlayerName"] =
              int64_t(playerNameChangeMask(names, defaultNames));
          nameForm->close();
          runtime.RemoveScene(nameForm);
          pending.clear();
          saveSystem();
          vm.resume();
          return std::string();
        },
        [this] {
          runtime.RemoveScene(nameForm);
          pending.clear();
          vm.resume();
        });
    // Keep the form above the current main scene in every state: the script
    // can request the name before GAMESTART switches from Menu to Gameplay.
    runtime.AddGlobalScene(nameForm);
    vm.waiting = true;
    pending = "NAMEENTRY";
  }
  std::string choiceTarget = "@RESULT";
  uint64_t deadline = 0, lastUpdate = 0, lastSystemWrite = 0;
  bool title = false, autoMode = false, loaded = false, skipHeld = false,
       currentRead = false, openingPlayed = false;
  struct MeoFadeState {
    int alpha = 0;
    uint8_t r = 0, g = 0, b = 0;
  };
  bool meo = false, meoMenu = false, meoSceneMenu = false;
  bool meoLogoActive = false;
  Uint64 meoLogoStart = 0;
  Uint64 meoTitleFadeStart = 0;
  bool meoOverlayOpen = false;
  int meoOverlayFocus = 0; // 0=SPEED, 1=SAVE, 2=BACKLOG, 3=SYSTEM, 4=STOP
  int meoSpeedIndex = 0;   // 0=1x, 1=1.5x, 2=2x, 3=3x, 4=4x
  Bytes meoSystemMap;
  Bytes meoTitleMap;
  std::map<std::string, Bytes> meoScripts;
  std::vector<std::string> meoScenes;
  size_t meoSceneFocus = 0;
  std::map<std::string, std::string> meoText;
  std::string meoRawSpeaker;
  std::string meoVoiceTrack;
  std::vector<std::string> activeOriginalKeys;
  std::string currentRawSpeaker, currentRawDialogue, currentVoiceId;
  std::vector<Node> meoFrames;
  std::string meoCurrentScript;
  std::string meoNextScript;
  std::vector<std::pair<std::string, std::string>> meoChoices;
  std::map<std::string, int64_t> meoGlobals;
  std::map<std::string, int64_t> meoLocals;
  std::map<std::string, MeoFadeState> meoFades;
  uint64_t meoLoveViewUntil = 0;
  size_t meoFrame = 0;
  uint64_t meoFrameStart = 0;
  uint64_t meoElapsedOffset = 0;
  int meoFrameLength = 0;
  int meoSelectStart = 0, meoSelectEnd = 0;
  bool meoChoiceShown = false;
  // Imouto de Ikou! (runscrmod.dll + scrth.dll) runtime state
  bool imouto = false, imoutoMenu = false;
  ImoutoVM imoutoVm;
  std::array<uint8_t, 0x2300> imoutoSystemBin{};
  std::string imoutoProtagGiven = "久志";
  std::string imoutoProtagFamily = "吉住";
  int imoutoCharId = 0, imoutoTint = 0;
  std::string imoutoSpeaker, imoutoPendingText, imoutoPendingVoice;
  struct ImoutoLayer {
    std::string path;
    int x = 0, y = 0;
  };
  std::map<int, ImoutoLayer> imoutoBgLayers;
  struct ImoutoSprite {
    std::string id;
    int charId = 0, x = 0, y = 0, tint = 0;
  };
  std::vector<ImoutoSprite> imoutoSprites;
  struct ImoutoManpuSlot {
    bool active = false;
    std::string path;
    ManpuAnimation anim;
    int seqId = 0;
    int x = 0, y = 0;
    int frameId = -1;
  };
  std::array<ImoutoManpuSlot, 8> imoutoManpu{};
  int titleFocus = 0;
  std::string pendingScript;
  // Per-edition title layout loaded from EASYTITLE.INI.
  struct TitleChip {
    std::string name;
    std::string flag;
    int atlasX = 0, atlasY = 0, atlasW = 0, atlasH = 0; // Source rect in atlas
    int drawX = 0, drawY = 0, drawW = 0, drawH = 0;     // Screen destination
    bool hasDrawArea = false;
  };
  int titleBaseX = 0, titleBaseY = 0; // Screen base position from [TITLE000]
  int titleChipSpacing = 40;          // Vertical spacing between chips
  bool titleAbsoluteAlign = false;    // ALIGN-X/Y = ABSOLUTE (Lost M)
  std::string titleChipTexture;       // TEXNAME / TEMPLATENAME or fallback
  std::vector<TitleChip> allTitleChips;
  std::vector<TitleChip> titleChips;
  void refreshTitleChips() {
    titleChips.clear();
    for (const auto &chip : allTitleChips) {
      if (chip.name.empty())
        continue;
      if (!chip.flag.empty()) {
        auto it = vm.variables.find("$" + chip.flag);
        if (it == vm.variables.end() || number(it->second) == 0)
          continue;
      }
      titleChips.push_back(chip);
    }
    if (titleFocus >= int(titleChips.size()))
      titleFocus = 0;
  }
  std::set<std::string> warned;
  explicit Impl(SummerRuntime &r,
                std::shared_ptr<Kotonoha::Providers::SummerAssets> a)
      : runtime(r), assets(std::move(a)) {
    const auto scriptIds = assets->list("SCRIPT/");
    imouto = assets->isImoutoDeIkou();
    meo = !imouto && (std::find(scriptIds.begin(), scriptIds.end(),
                                "SCRIPT/A-00.XML") != scriptIds.end() ||
                      std::find(scriptIds.begin(), scriptIds.end(),
                                "SCRIPT/Z-ED-GOOD.XML") != scriptIds.end());
    if (imouto) {
      imoutoVm.onCall = [this](const std::string &cmd,
                               const std::vector<ImoutoSlot> &args) {
        return execImoutoCommand(cmd, args);
      };
    } else if (meo) {
      for (auto &id : scriptIds) {
        if (canonical(id.substr(id.find_last_of('.') + 1)) == "XML")
          meoScripts.emplace(id, assets->read(id));
      }
      for (const auto &[id, bytes] : meoScripts)
        meoScenes.push_back(stem(id.substr(id.find_last_of('/') + 1)));
      if (assets->contains("GRAPHIC/SYSTEM.MAP"))
        meoSystemMap = assets->read("GRAPHIC/SYSTEM.MAP");
      if (assets->contains("GRAPHIC/TITLE.MAP"))
        meoTitleMap = assets->read("GRAPHIC/TITLE.MAP");
    } else {
      for (auto &id : assets->list("SCRIPT/")) {
        auto bytes = assets->read(id);
        vm.add(stem(id.substr(7)), std::string(bytes.begin(), bytes.end()));
      }
    }
    if (!meo && !imouto) {
      std::istringstream stream(
          assets->loadTextUtf8("INI/CONFIG.INI", "UTF-16LE"));
      std::string line;
      speakers.resize(64);
      speakerColors.assign(64, SDL_Color{240, 240, 245, 255});
      std::vector<int> cgFlags(64, 0);
      while (std::getline(stream, line)) {
        if (line.rfind("PlayerName0", 0) == 0 && line.size() > 12 &&
            line[11] >= '0' && line[11] <= '3') {
          const auto eq = line.find('=');
          if (eq != line.npos) {
            auto val = line.substr(eq + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t\r") + 1);
            auto &name = defaultNames[size_t(line[11] - '0')];
            if (!val.empty() && val[0] == '@') {
              // SRV2/LostM: @-prefixed literal UTF-8 string (after UTF-16LE
              // decode).
              name = val.substr(1);
            } else if (val.size() % 2 == 0 &&
                       val.find_first_not_of("0123456789abcdefABCDEF") ==
                           val.npos) {
              // SRV1: hex-encoded byte string.
              for (size_t at = 0; at < val.size(); at += 2)
                name += char(std::stoi(val.substr(at, 2), nullptr, 16));
            }
          }
        }
        if (line.size() > 11 && line.substr(0, 3) == "CHR" &&
            line.substr(6, 4) == "Name") {
          int i = std::stoi(line.substr(3, 3));
          auto eq = line.find('=');
          if (i >= int(speakers.size()) || eq == line.npos)
            continue;
          std::string val = line.substr(eq + 1);
          val.erase(0, val.find_first_not_of(" \t"));
          val.erase(val.find_last_not_of(" \t\r") + 1);
          if (!val.empty() && val[0] == '@') {
            // SRV2/LostM: @-prefixed literal UTF-8 string.
            speakers[i] = val.substr(1);
          } else if (val.size() % 2 == 0 &&
                     val.find_first_not_of("0123456789ABCDEFabcdef") ==
                         val.npos) {
            // SRV1: hex-encoded byte string.
            std::string name;
            for (size_t p = 0; p < val.size(); p += 2)
              name += char(std::stoi(val.substr(p, 2), nullptr, 16));
            speakers[i] = name;
          }
        } else if (line.size() > 15 && line.substr(0, 3) == "CHR" &&
                   line.substr(6, 9) == "FontColor") {
          int i = std::stoi(line.substr(3, 3));
          auto eq = line.find('=');
          if (i >= 0 && i < int(speakerColors.size()) && eq != line.npos) {
            std::string val = line.substr(eq + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t\r") + 1);
            try {
              unsigned long rgb = std::stoul(val, nullptr, 16);
              speakerColors[size_t(i)] =
                  SDL_Color{Uint8((rgb >> 16) & 0xff), Uint8((rgb >> 8) & 0xff),
                            Uint8(rgb & 0xff), 255};
            } catch (...) {
            }
          }
        } else if (line.size() > 12 && line.substr(0, 3) == "CHR" &&
                   line.substr(6, 6) == "CgFlag") {
          int i = std::stoi(line.substr(3, 3));
          auto eq = line.find('=');
          if (i >= 0 && i < int(cgFlags.size()) && eq != line.npos) {
            std::string val = line.substr(eq + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t\r") + 1);
            try {
              cgFlags[size_t(i)] = std::stoi(val);
            } catch (...) {
            }
          }
        }
      }
      for (size_t i = 0; i < speakers.size(); ++i) {
        if (!speakers[i].empty() && (cgFlags[i] & 0x8000) != 0) {
          vm.variables["$HAIR" + speakers[i]] =
              int64_t((cgFlags[i] & 1) ? 0x10000 : 0);
        }
      }
      // Parse character position offsets from GRAPHIC.INI (SRV2 has 7 slots).
      if (assets->contains("INI/GRAPHIC.INI")) {
        std::istringstream gfx(
            assets->loadTextUtf8("INI/GRAPHIC.INI", "UTF-16LE"));
        std::string gline;
        while (std::getline(gfx, gline)) {
          if (gline.rfind("Position ", 0) == 0 ||
              gline.rfind("Position=", 0) == 0 ||
              gline.rfind("Position\t", 0) == 0) {
            auto eq = gline.find('=');
            if (eq == gline.npos)
              continue;
            std::string csv = gline.substr(eq + 1);
            csv.erase(0, csv.find_first_not_of(" \t"));
            csv.erase(csv.find_last_not_of(" \t\r") + 1);
            std::vector<int> parsed;
            std::istringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
              token.erase(0, token.find_first_not_of(" \t"));
              token.erase(token.find_last_not_of(" \t") + 1);
              if (!token.empty())
                parsed.push_back(std::stoi(token));
            }
            if (!parsed.empty())
              positionOffsets = std::move(parsed);
          }
        }
      }
      vm.function = [this](const auto &name, const auto &args) {
        function(name, args);
      };
    }
  }
  std::filesystem::path saveDirectory() const {
    if (const char *path = SDL_getenv("SUMMER_SAVE_DIR"))
      return path;
    return Kotonoha::saveDirectory();
  }
  void loadSystem() {
    Kotonoha::Filesystem::InputFile input(saveDirectory() / "system.dat",
                                          std::ios::binary);
    if (!input)
      return;
    std::string version;
    size_t count;
    if (!(input >> version >> count) || version != "SUMMER-SYSTEM-1" ||
        count > 100000)
      throw std::runtime_error("Invalid Summer system data");
    std::map<std::string, Value> variables;
    while (count--) {
      std::string key, value;
      int type;
      if (!(input >> std::quoted(key) >> type >> std::quoted(value)) ||
          key.empty() || key[0] != '$' || (type != 0 && type != 1))
        throw std::runtime_error("Invalid Summer persistent variable");
      if (type) {
        variables[key] = value;
      } else {
        int64_t number = 0;
        const auto result =
            std::from_chars(value.data(), value.data() + value.size(), number);
        if (result.ec != std::errc{} ||
            result.ptr != value.data() + value.size()) {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "[SummerRadish] Ignoring invalid saved number for %s",
                      key.c_str());
          continue;
        }
        variables[key] = number;
        if (meo && key.size() > 1)
          meoGlobals[key.substr(1)] = number;
      }
    }
    input >> std::ws;
    if (!input.eof())
      throw std::runtime_error("Trailing Summer system data");
    for (auto &[key, value] : variables)
      vm.variables[key] = value;
  }
  void saveSystem() {
    if (!loaded)
      return;
    if (meo) {
      for (const auto &[k, v] : meoGlobals)
        vm.variables["$" + k] = v;
    }
    size_t count = 0;
    for (auto &[key, value] : vm.variables)
      if (!key.empty() && key[0] == '$')
        ++count;
    std::ostringstream data;
    data << "SUMMER-SYSTEM-1 " << count << '\n';
    for (auto &[key, value] : vm.variables)
      if (!key.empty() && key[0] == '$')
        data << std::quoted(key) << ' ' << value.index() << ' '
             << std::quoted(string(value)) << '\n';
    const auto path = saveDirectory() / "system.dat";
    std::filesystem::create_directories(path.parent_path());
    Kotonoha::Filesystem::InputFile existing(path, std::ios::binary);
    std::string previous((std::istreambuf_iterator<char>(existing)), {});
    if (previous == data.str())
      return;
    const auto temporary = path.string() + ".tmp";
    Kotonoha::Filesystem::OutputFile output(temporary,
                                            std::ios::binary | std::ios::trunc);
    output << data.str();
    output.close();
    if (!output)
      throw std::runtime_error("Cannot write Summer system data");
    std::filesystem::rename(temporary, path);
  }
  void finishMessage() {
    if (imouto) {
      voice.Stop();
      activeOriginalKeys.clear();
      currentVoiceId.clear();
      currentRawSpeaker.clear();
      currentRawDialogue.clear();
      pending.clear();
      if (!skipMode && !skipHeld)
        autoMode = false;
      imoutoVm.resume();
      return;
    }
    if (vm.resource.children.size() > 1)
      vm.markRead(vm.resource.children[1]);
    voice.Stop();
    extraVoiceActors_.clear();
    currentMessageActor = 0;
    activeOriginalKeys.clear();
    currentVoiceId.clear();
    currentRawSpeaker.clear();
    currentRawDialogue.clear();
    pending.clear();
    autoMode = skipMode = false;
    vm.resume();
  }
  void openSaves(bool load) {
    if (!saves)
      saves = std::make_unique<VNSaveMenu>(*engine);
    saves->open(saveDirectory().string(), load);
    runtime.RemoveScene(saves.get());
    runtime.AddScene(saves.get());
  }
  void audio(PcmPlayer &player, const std::string &id,
             const std::string &channel, bool loop = false,
             std::size_t startFrame = 0) {
    player.Stop();
    if (!assets->contains(id)) {
      warn("Missing audio: " + id);
      return;
    }
    auto decoded = assets->loadAudio(id);
    SDL_AudioSpec target{SDL_AUDIO_S16, decoded.spec.channels,
                         decoded.spec.freq};
    Uint8 *converted = nullptr;
    int size = 0;
    if (!SDL_ConvertAudioSamples(&decoded.spec, decoded.data.data(),
                                 int(decoded.data.size()), &target, &converted,
                                 &size))
      throw std::runtime_error(SDL_GetError());
    Kotonoha::SDLMemoryPtr<Uint8> convertedOwner(converted);
    PcmPlayer::Clip clip;
    clip.channels = target.channels;
    clip.rate = target.freq;
    clip.loop = loop;
    clip.startFrame = startFrame;
    clip.samples.resize(size / 2);
    std::memcpy(clip.samples.data(), converted, size);

    if (loop && meo && canonical(id).size() >= 4 &&
        canonical(id).compare(canonical(id).size() - 4, 4, ".BGM") == 0) {
      const auto bytes = assets->read(id);
      if (bytes.size() >= 16) {
        clip.loopStart = std::size_t(bytes[0]) | std::size_t(bytes[1]) << 8 |
                         std::size_t(bytes[2]) << 16 |
                         std::size_t(bytes[3]) << 24;
      }
    }

    if (loop && channel == "BGM") {
      const auto introId = std::string("SOUND/BGM/INTORO/") +
                           id.substr(id.find_last_of('/') + 1);
      if (assets->contains(introId)) {
        auto intro = assets->loadAudio(introId);
        Uint8 *pcm = nullptr;
        int bytes = 0;
        if (!SDL_ConvertAudioSamples(&intro.spec, intro.data.data(),
                                     int(intro.data.size()), &target, &pcm,
                                     &bytes))
          throw std::runtime_error(SDL_GetError());
        Kotonoha::SDLMemoryPtr<Uint8> introOwner(pcm);
        std::vector<int16_t> combined(bytes / 2);
        std::memcpy(combined.data(), pcm, bytes);

        clip.loopStart = combined.size() / clip.channels;
        combined.insert(combined.end(), clip.samples.begin(),
                        clip.samples.end());
        clip.samples = std::move(combined);
      }
    }
    if (!player.Play(engine->GetAudio().channel(channel), std::move(clip)))
      throw std::runtime_error(SDL_GetError());
  }
  void warn(const std::string &s) {
    if (warned.insert(s).second)
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Summer Radish: %s", s.c_str());
  }
  int route() const {
    int id = 0;
    for (auto &[name, script] : vm.programs()) {
      if (name == vm.position.script)
        return id;
      ++id;
    }
    return 0;
  }
  std::string translated(const std::string &s) const {
    return engine->GetTranslation().text(route(), s);
  }
  // Reads EASYTITLE.INI (UTF-16 LE) and extracts the per-edition title chip
  // layout: which texture to use and where each chip is drawn. Strips a
  // leading '@' from the path (the original engine's INI pack marker).
  void loadEasyTitle(const std::string &iniPath) {
    titleChipTexture.clear();
    allTitleChips.clear();
    titleChips.clear();
    titleBaseX = 0;
    titleBaseY = 0;
    titleChipSpacing = 40;
    titleAbsoluteAlign = false;
    auto inipath = iniPath;
    if (!inipath.empty() && inipath[0] == '@')
      inipath.erase(0, 1);
    std::string data;
    try {
      const auto bytes = assets->read(inipath);
      data.assign(bytes.begin(), bytes.end());
    } catch (const std::exception &) {
      return;
    }
    auto decode = [](const std::string &s) {
      if (s.size() >= 2 && s[1] == '\0') {
        Bytes bytes(s.begin(), s.end());
        try {
          return utf16(bytes, 0, s.size() / 2);
        } catch (const std::exception &) {
          // Some INI files have mixed or invalid UTF-16 sequences. Strip null
          // bytes and treat as single-byte text.
          std::string result;
          result.reserve(s.size() / 2);
          for (size_t i = 0; i < s.size(); i += 2)
            result += s[i];
          return result;
        }
      }
      return s;
    };
    std::string text = decode(data);
    std::istringstream stream(text);
    std::string line;
    std::string section;
    int chipIndex = -1;
    while (std::getline(stream, line)) {
      size_t start = line.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
        continue;
      size_t end = line.find_last_not_of(" \t\r\n");
      line = line.substr(start, end - start + 1);
      if (line.empty() || line[0] == '#' || line[0] == ';')
        continue;
      if (line.front() == '[' && line.back() == ']') {
        section = line.substr(1, line.size() - 2);
        if (section.rfind("CHIP", 0) == 0) {
          chipIndex = std::stoi(section.substr(4));
          if (chipIndex >= 0)
            allTitleChips.resize(
                std::max(allTitleChips.size(), size_t(chipIndex + 1)));
        }
        continue;
      }
      auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;
      auto key = line.substr(0, eq);
      auto val = line.substr(eq + 1);
      key.erase(0, key.find_first_not_of(" \t"));
      if (!key.empty())
        key.erase(key.find_last_not_of(" \t") + 1);
      val.erase(0, val.find_first_not_of(" \t"));
      if (!val.empty())
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
      auto stripAt = [](std::string s) {
        if (!s.empty() && s[0] == '@')
          s.erase(0, 1);
        return s;
      };
      if (section == "TOTAL") {
        if (key == "TEMPLATENAME" || key == "TEXNAME")
          titleChipTexture = stripAt(val);
      } else if (section == "TITLE000") {
        if (key == "BASE" && !val.empty()) {
          background = stripAt(val);
        } else if (key == "ALIGN-X" || key == "ALIGN-Y") {
          if (canonical(val) == "ABSOLUTE")
            titleAbsoluteAlign = true;
        } else if (key == "DRAWAREA") {
          for (char &ch : val)
            if (ch == ',')
              ch = ' ';
          std::istringstream v(val);
          int a, b, c, d;
          if (v >> a >> b >> c >> d) {
            titleBaseX = a;
            titleBaseY = b;
          }
        }
      } else if (section.rfind("CHIP", 0) == 0 && chipIndex >= 0 &&
                 chipIndex < int(allTitleChips.size())) {
        auto &chip = allTitleChips[size_t(chipIndex)];
        if (key == "NAME")
          chip.name = val;
        else if (key == "FLAG")
          chip.flag = val;
        else if (key == "SIZE") {
          titleChipSpacing = std::stoi(val);
        } else if (key == "AREA") {
          for (char &ch : val)
            if (ch == ',')
              ch = ' ';
          std::istringstream v(val);
          int a, b, c, d;
          if (v >> a >> b >> c >> d) {
            chip.atlasX = a;
            chip.atlasY = b;
            chip.atlasW = c;
            chip.atlasH = d;
          }
        } else if (key == "DRAWAREA") {
          for (char &ch : val)
            if (ch == ',')
              ch = ' ';
          std::istringstream v(val);
          int a, b, c, d;
          if (v >> a >> b >> c >> d) {
            chip.drawX = a;
            chip.drawY = b;
            chip.drawW = c;
            chip.drawH = d;
            chip.hasDrawArea = true;
          }
        }
      }
    }
    if (titleChipTexture.empty())
      titleChipTexture = assets->contains("GRAPHIC/TITLE/TITLECHIP.TEXB")
                             ? "GRAPHIC/TITLE/TITLECHIP.TEXB"
                             : "GRAPHIC/TITLE/TITLE.TEXB";
    if (allTitleChips.empty()) {
      // Hardcoded fallback: four chips at the classic positions.
      allTitleChips.resize(4);
      static const char *names[4] = {"START", "LOAD", "CONFIG", "QUIT"};
      for (int i = 0; i < 4; ++i) {
        allTitleChips[i].name = names[i];
        allTitleChips[i].atlasX = 0;
        allTitleChips[i].atlasY = i * 32;
        allTitleChips[i].atlasW = 230;
        allTitleChips[i].atlasH = 32;
      }
      titleBaseX = 285;
      titleBaseY = 342;
      titleChipSpacing = 40;
    }
    refreshTitleChips();
  }
  void applyMeoFrame(const Node &frame) {
    std::string speaker = meoRawSpeaker;
    std::string dialogue, originalDialogue;
    std::vector<std::string> originalLines;
    bool hasDialogue = false;
    if (!frame.children.empty()) {
      for (const auto &child : frame.children) {
        if (child.tag == "Text") {
          const auto mess = meoText.find(child.attr("MESS"));
          if (hasDialogue) {
            dialogue += '\n';
            originalDialogue += '\n';
          }
          const auto original =
              mess != meoText.end() ? mess->second : child.attr("MESS");
          originalDialogue += original;
          dialogue += translated(original);
          originalLines.push_back(original);
          hasDialogue = true;
        } else if (child.tag == "Log") {
          const auto nameNo = child.attr("NAME");
          if (nameNo != "-1") {
            auto it = meoText.find(nameNo);
            if (it != meoText.end())
              speaker = it->second;
          }
        } else if (child.tag == "EraseText") {
          text->hide();
        } else if (child.tag == "Bgm") {
          auto name = canonical(child.text);
          if (name.rfind("SOUND/", 0) != 0)
            name = "SOUND/" + name;
          if (music != name) {
            music = name;
            audio(bgm, music, "BGM", true);
          }
        } else if (child.tag == "BgmStop") {
          bgm.Stop();
          music.clear();
        } else if (child.tag == "Fade") {
          const std::string name = child.attr("NAME");
          if (child.attr("ERASE") == "TRUE") {
            meoFades.erase(name);
          } else {
            auto &st = meoFades[name];
            if (name.find("_WH") != std::string::npos) {
              st.r = st.g = st.b = 255;
            }
            const std::string diff = child.attr("DIFF");
            if (!diff.empty()) {
              int a = 0, r = 0, g = 0, b = 0;
              char c1 = 0, c2 = 0, c3 = 0;
              std::istringstream ss(diff);
              if (ss >> a >> c1 >> r >> c2 >> g >> c3 >> b) {
                st.alpha = std::clamp(a, 0, 255);
                st.r = uint8_t(std::clamp(r, 0, 255));
                st.g = uint8_t(std::clamp(g, 0, 255));
                st.b = uint8_t(std::clamp(b, 0, 255));
              }
            }
          }
        } else if (child.tag == "Objects") {
          for (const auto &object : child.children) {
            if (object.attr("CREATE") != "Movie")
              continue;
            auto file = object.attr("FILE");
            if (file.empty())
              continue;
            auto slash = file.find('/');
            if (slash != std::string::npos)
              file = file.substr(slash + 1);
            const std::string id = "GRAPHIC/" + file;
            if (assets->contains(id)) {
              movie->open(*assets, id, false, "BGM");
              movie->setPaused(false);
            }
          }
        }
      }
    }
    if (hasDialogue) {
      meoRawSpeaker = speaker;
      currentRawSpeaker = speaker;
      currentRawDialogue = originalDialogue;
      currentVoiceId.clear();
      activeOriginalKeys.clear();
      if (!speaker.empty())
        activeOriginalKeys.push_back(speaker);
      for (const auto &line : originalLines)
        if (!line.empty())
          activeOriginalKeys.push_back(line);
      text->setDialogue(translated(speaker), dialogue);
      text->show();
    }
  }
  int64_t evalMeoExpr(const Node &n) {
    if (n.tag == "INT") {
      try {
        return std::stoll(n.text);
      } catch (...) {
        return 0;
      }
    }
    if (n.tag == "GLOBAL") {
      auto it = meoGlobals.find(n.text);
      return it != meoGlobals.end() ? it->second : 0;
    }
    if (n.tag == "LOCAL") {
      auto it = meoLocals.find(n.text);
      return it != meoLocals.end() ? it->second : 0;
    }
    if (n.tag == "ADD" && n.children.size() >= 2)
      return evalMeoExpr(n.children[0]) + evalMeoExpr(n.children[1]);
    if (n.tag == "SUB" && n.children.size() >= 2)
      return evalMeoExpr(n.children[0]) - evalMeoExpr(n.children[1]);
    if (n.tag == "OPE" && n.children.size() >= 2)
      return evalMeoExpr(n.children[0]) == evalMeoExpr(n.children[1]) ? 1 : 0;
    if (n.tag == "OPGE" && n.children.size() >= 2)
      return evalMeoExpr(n.children[0]) >= evalMeoExpr(n.children[1]) ? 1 : 0;
    if (n.tag == "SET" && n.children.size() >= 2) {
      const int64_t val = evalMeoExpr(n.children[1]);
      if (n.children[0].tag == "GLOBAL") {
        meoGlobals[n.children[0].text] = val;
        vm.variables["$" + n.children[0].text] = val;
      } else if (n.children[0].tag == "LOCAL") {
        meoLocals[n.children[0].text] = val;
      }
      return val;
    }
    if (n.tag == "CALC") {
      int64_t last = 0;
      for (const auto &c : n.children)
        last = evalMeoExpr(c);
      return last;
    }
    return 0;
  }
  void execMeoCode(const Node &node) {
    if (node.tag == "Code" || node.tag == "BLOCK") {
      for (const auto &c : node.children)
        execMeoCode(c);
      return;
    }
    if (node.tag == "CALC") {
      evalMeoExpr(node);
      return;
    }
    if (node.tag == "IFBLOCK") {
      for (size_t i = 0; i + 1 < node.children.size(); i += 2) {
        if (evalMeoExpr(node.children[i]) != 0) {
          execMeoCode(node.children[i + 1]);
          break;
        }
      }
      return;
    }
    if (node.tag == "FUNC") {
      const std::string fn = node.attr("NAME");
      std::vector<std::string> refs;
      for (const auto &c : node.children)
        if (c.tag == "STRRES")
          refs.push_back(c.text);
      if (fn == "SetSelectScript" && refs.size() >= 2) {
        const auto target = meoText.find(refs[0]);
        const auto label = meoText.find(refs[1]);
        if (target != meoText.end() && label != meoText.end())
          meoChoices.emplace_back(label->second, target->second);
      } else if (fn == "SetTimeoutScript" && !refs.empty()) {
        const auto found = meoText.find(refs[0]);
        if (found != meoText.end())
          meoNextScript = found->second;
      } else if (fn == "ViewMisaoLove") {
        meoLoveViewUntil = SDL_GetTicks() + 3000;
      }
    }
  }
  void beginMeo(const std::string &scriptName) {
    SDL_Log("MEO: begin script %s", scriptName.c_str());
    if (canonical(stem(scriptName)) == "A-00")
      meoLocals.clear();
    meoMenu = false;
    if (movie)
      movie->stop();
    voice.Stop();
    text->hide();
    meoText.clear();
    meoRawSpeaker.clear();
    meoVoiceTrack.clear();
    activeOriginalKeys.clear();
    currentRawSpeaker.clear();
    currentRawDialogue.clear();
    currentVoiceId.clear();
    meoFrames.clear();
    meoNextScript.clear();
    meoChoices.clear();
    meoFades.clear();
    meoFrame = 0;
    meoFrameLength = 0;
    meoElapsedOffset = 0;
    meoSelectStart = meoSelectEnd = 0;
    meoChoiceShown = false;
    choice->hide();

    std::string wanted = canonical(scriptName);
    if (wanted.find('.') == std::string::npos)
      wanted += ".XML";
    Bytes source;
    bool found = false;
    const std::string directId = "SCRIPT/" + wanted;
    if (auto bytes = assets->tryReadAsset(directId)) {
      source = std::move(*bytes);
      found = true;
    } else {
      try {
        (void)assets->read(directId);
      } catch (const std::exception &) {
      }
    }
    if (!found) {
      const std::string directLowerId =
          "SCRIPT/" + scriptName +
          (scriptName.find('.') == std::string::npos ? ".XML" : "");
      try {
        source = assets->read(directLowerId);
        found = true;
      } catch (const std::exception &) {
      }
    }
    if (!found) {
      for (const auto &[id, bytes] : meoScripts) {
        auto filename = std::filesystem::path(id).filename().string();
        auto stemId = stem(filename);
        const auto slash = id.find_last_of('/');
        const auto fallbackName =
            slash == std::string::npos ? id : id.substr(slash + 1);
        const auto fallbackStem = stem(fallbackName);
        const std::string requestedStem = stem(wanted);
        if (canonical(stemId) == requestedStem ||
            canonical(fallbackStem) == requestedStem) {
          source = bytes;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      text->setDialogue(
          "MISS EACH OTHER",
          "MEO assets detected, but the initial script could not be indexed.");
      text->show();
      runtime.SetState(GameState::Gameplay);
      return;
    }
    meoCurrentScript = wanted;

    const std::string xml(source.begin(), source.end());
    const auto voiceStart = xml.find("<VoiceFile>");
    if (voiceStart != std::string::npos) {
      const auto valueStart = voiceStart + sizeof("<VoiceFile>") - 1;
      const auto valueEnd = xml.find("</VoiceFile>", valueStart);
      if (valueEnd != std::string::npos) {
        meoVoiceTrack =
            canonical(xml.substr(valueStart, valueEnd - valueStart));
        if (meoVoiceTrack.rfind("SOUND/", 0) != 0)
          meoVoiceTrack = "SOUND/" + meoVoiceTrack;
        audio(voice, meoVoiceTrack, "Voice");
      }
    }
    auto attrValue = [](const std::string &tag, const std::string &name) {
      const std::string needle = name + "=\"";
      auto start = tag.find(needle);
      if (start == std::string::npos)
        return std::string();
      start += needle.size();
      auto end = tag.find('"', start);
      if (end == std::string::npos)
        return std::string();
      return tag.substr(start, end - start);
    };

    size_t textPos = 0;
    while ((textPos = xml.find("<TextRes ", textPos)) != std::string::npos) {
      const auto tagEnd = xml.find('>', textPos);
      const auto valueStart =
          tagEnd == std::string::npos ? std::string::npos : tagEnd + 1;
      const auto valueEnd = valueStart == std::string::npos
                                ? std::string::npos
                                : xml.find("</TextRes>", valueStart);
      if (tagEnd == std::string::npos || valueEnd == std::string::npos)
        break;
      const std::string tag = xml.substr(textPos, tagEnd - textPos);
      meoText[attrValue(tag, "NO")] =
          xml.substr(valueStart, valueEnd - valueStart);
      textPos = valueEnd + 10;
    }

    const std::string lengthTag = "<FrameLength>";
    auto lengthPos = xml.find(lengthTag);
    if (lengthPos != std::string::npos) {
      lengthPos += lengthTag.size();
      const auto end = xml.find("</FrameLength>", lengthPos);
      if (end != std::string::npos)
        meoFrameLength = std::stoi(xml.substr(lengthPos, end - lengthPos));
    }

    auto integerTag = [&](const char *tag) {
      const std::string open = std::string("<") + tag + ">";
      const std::string close = std::string("</") + tag + ">";
      auto start = xml.find(open);
      if (start == std::string::npos)
        return 0;
      start += open.size();
      auto end = xml.find(close, start);
      return end == std::string::npos
                 ? 0
                 : std::stoi(xml.substr(start, end - start));
    };
    meoSelectStart = integerTag("SelectStartTime");
    meoSelectEnd = integerTag("SelectEndTime");
    const auto codeStart = xml.find("<Code>");
    const auto codeEnd = xml.find("</Code>", codeStart);
    if (codeStart != std::string::npos && codeEnd != std::string::npos) {
      try {
        execMeoCode(parseXml(xml.substr(codeStart, codeEnd + 7 - codeStart)));
      } catch (const std::exception &e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MEO Code parse warning: %s",
                    e.what());
      }
    }
    saveSystem();

    size_t framePos = 0;
    while ((framePos = xml.find("<Frame ", framePos)) != std::string::npos) {
      const auto tagEnd = xml.find('>', framePos);
      const auto frameEnd = tagEnd == std::string::npos
                                ? std::string::npos
                                : xml.find("</Frame>", tagEnd);
      if (tagEnd == std::string::npos || frameEnd == std::string::npos)
        break;
      const std::string tag = xml.substr(framePos, tagEnd - framePos);
      const std::string body = xml.substr(tagEnd + 1, frameEnd - tagEnd - 1);

      Node frame;
      frame.tag = "Frame";
      frame.attrs["TIME"] = attrValue(tag, "TIME");

      size_t childPos = 0;
      while ((childPos = body.find('<', childPos)) != std::string::npos) {
        const auto end = body.find('>', childPos);
        if (end == std::string::npos)
          break;
        const std::string tag = body.substr(childPos, end - childPos + 1);
        Node child;
        if (tag.compare(0, 5, "<Log ") == 0) {
          child.tag = "Log";
          child.attrs["NAME"] = attrValue(tag, "NAME");
          child.attrs["MESS"] = attrValue(tag, "MESS");
        } else if (tag.compare(0, 6, "<Text ") == 0) {
          child.tag = "Text";
          child.attrs["MESS"] = attrValue(tag, "MESS");
        } else if (tag.compare(0, 10, "<EraseText") == 0) {
          child.tag = "EraseText";
        } else if (tag.compare(0, 9, "<BgmStop") == 0) {
          child.tag = "BgmStop";
        } else if (tag.compare(0, 4, "<Bgm") == 0) {
          const auto close = body.find("</Bgm>", end + 1);
          if (close != std::string::npos) {
            child.tag = "Bgm";
            child.text = body.substr(end + 1, close - end - 1);
          }
        } else if (tag.compare(0, 8, "<FDIN_BL") == 0 ||
                   tag.compare(0, 8, "<FDIN_WH") == 0 ||
                   tag.compare(0, 8, "<FDOT_BL") == 0 ||
                   tag.compare(0, 8, "<FDOT_WH") == 0) {
          child.tag = "Fade";
          child.attrs["NAME"] = tag.substr(1, 7);
          child.attrs["DIFF"] = attrValue(tag, "DIFF");
          child.attrs["ERASE"] = attrValue(tag, "ERASE");
        } else if (tag.find("CREATE=\"Movie\"") != std::string::npos) {
          child.tag = "Objects";
          Node object;
          object.tag = "Object";
          object.attrs["CREATE"] = "Movie";
          object.attrs["FILE"] = attrValue(tag, "FILE");
          child.children.push_back(std::move(object));
        }
        if (!child.tag.empty())
          frame.children.push_back(std::move(child));
        childPos = end + 1;
      }
      meoFrames.push_back(std::move(frame));
      framePos = frameEnd + 8;
    }

    meoFrameStart = SDL_GetTicks();
    runtime.SetState(GameState::Gameplay);
    text->show();
    if (!meoFrames.empty()) {
      meoFrame = 0;
      applyMeoFrame(meoFrames[0]);
      SDL_Log("MEO: timeline %s (%zu frames, length %d, next %s)",
              wanted.c_str(), meoFrames.size(), meoFrameLength,
              meoNextScript.c_str());
    } else {
      text->setDialogue("", "MEO timeline loaded, but no Frame nodes found.");
    }
  }
  void openMeoLogo() {
    if (movie)
      movie->stop();
    bgm.Stop();
    voice.Stop();
    music.clear();
    if (!assets->contains("GRAPHIC/ROGO.TEX")) {
      openMeoMenu(false);
      return;
    }
    meoLogoActive = true;
    meoLogoStart = SDL_GetTicks();
    meoTitleFadeStart = 0;
    meoMenu = true;
    meoSceneMenu = false;
    titleFocus = 0;
    text->hide();
    runtime.SetState(GameState::Menu);
  }
  void openMeoMenu(bool fadeIn = false) {
    if (movie)
      movie->stop();
    bgm.Stop();
    voice.Stop();
    music.clear();
    meoLogoActive = false;
    meoTitleFadeStart = fadeIn ? SDL_GetTicks() : 0;
    meoMenu = true;
    meoSceneMenu = false;
    titleFocus = 0;
    text->hide();
    if (assets->contains("SOUND/MEO02.BGM")) {
      music = "SOUND/MEO02.BGM";
      audio(bgm, music, "BGM", true);
    }
    runtime.SetState(GameState::Menu);
  }
  void updateMeo(Uint64 now) {
    if (meoMenu || meoFrames.empty())
      return;
    const double seconds =
        double(meoElapsedOffset +
               (now >= meoFrameStart ? now - meoFrameStart : 0)) /
        1000.0;
    // FILMEngine advances the SND track in groups of 24 script frames.
    const int frameTime = int(seconds * 24.0);
    if (!meoChoices.empty() && meoSelectEnd > meoSelectStart &&
        frameTime >= meoSelectStart && frameTime < meoSelectEnd &&
        !meoChoiceShown) {
      std::vector<ChoiceOption> options;
      for (size_t i = 0; i < meoChoices.size(); ++i)
        options.push_back({translated(meoChoices[i].first), int(i)});
      meoChoiceShown = true;
      choice->setQuestion("", options, [this](int index, const std::string &) {
        if (index < 0 || size_t(index) >= meoChoices.size())
          return;
        const std::string target = meoChoices[index].second;
        choice->hide();
        beginMeo(target);
      });
    }
    if (meoChoiceShown && frameTime >= meoSelectEnd)
      choice->hide();
    while (meoFrame + 1 < meoFrames.size()) {
      const int nextTime = std::stoi(meoFrames[meoFrame + 1].attr("TIME"));
      if (frameTime < nextTime)
        break;
      ++meoFrame;
      applyMeoFrame(meoFrames[meoFrame]);
    }
    if (meoFrameLength > 0 && frameTime >= meoFrameLength &&
        !meoNextScript.empty()) {
      const std::string next = meoNextScript;
      beginMeo(next);
    }
  }
  static std::string imoutoAlphaMaskPath(int mode) {
    int num = 0;
    if (mode >= 6 && mode <= 10)
      num = mode - 5; // 0001..0005.ALP
    else if (mode >= 11 && mode <= 34)
      num = mode - 4; // 0007..0030.ALP (0006.ALP is skipped in native)
    if (num <= 0)
      return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "BGDATA/%04d.ALP", num);
    return buf;
  }
  Image composeImoutoScene() {
    Image canvas;
    canvas.width = 800;
    canvas.height = 600;
    canvas.rgba.assign(800u * 600u * 4u, 0);
    for (size_t i = 3; i < canvas.rgba.size(); i += 4)
      canvas.rgba[i] = 255;
    for (const auto &[slot, layer] : imoutoBgLayers) {
      if (layer.path.empty() || !assets->contains(layer.path))
        continue;
      const auto &img = image(layer.path);
      const int drawX = img.x + layer.x - bgOffsetX;
      const int drawY = img.y + layer.y - bgOffsetY;
      compositeImage(canvas, img, drawX, drawY);
    }
    for (const auto &sp : imoutoSprites) {
      if (sp.id.empty() || !assets->contains(sp.id))
        continue;
      Image spr = image(sp.id);
      if (sp.tint != 0)
        applyLightingTint(spr, sp.tint);
      compositeImage(canvas, spr, spr.x + sp.x, spr.y + sp.y);
    }
    for (const auto &mp : imoutoManpu) {
      if (!mp.active || mp.frameId < 0)
        continue;
      auto it = mp.anim.frames.find(mp.frameId);
      if (it != mp.anim.frames.end())
        compositeImage(canvas, it->second, it->second.x + mp.x,
                       it->second.y - mp.y);
    }
    return canvas;
  }
  void openImoutoMenu() {
    if (movie)
      movie->stop();
    bgm.Stop();
    se.Stop();
    voice.Stop();
    imoutoMenu = true;
    title = false;
    titleFocus = 0;
    pending.clear();
    imoutoVm.waiting = false;
    imoutoBgLayers.clear();
    imoutoSprites.clear();
    for (auto &mp : imoutoManpu)
      mp.active = false;
    if (text)
      text->hide();
    if (choice)
      choice->hide();
    background =
        assets->contains("BGDATA/MAINTITLE.BGD")
            ? "BGDATA/MAINTITLE.BGD"
            : (assets->contains("BGDATA/TITLE.BGD") ? "BGDATA/TITLE.BGD"
                                                    : "BGDATA/NOBG.BGD");
    imoutoBgLayers[0] = {background, 0, 0};
    presented = composeImoutoScene();
    canvasKey.clear();
    runtime.SetState(GameState::Menu);
  }
  void beginImouto(const std::string &scriptStem = "init") {
    if (movie)
      movie->stop();
    bgm.Stop();
    se.Stop();
    voice.Stop();
    imoutoMenu = false;
    title = false;
    pending.clear();
    imoutoBgLayers.clear();
    imoutoSprites.clear();
    for (auto &mp : imoutoManpu)
      mp.active = false;
    imoutoTint = 0;
    bgOffsetX = bgOffsetY = 0;
    scrollDuration = 0;
    transitionDuration = 0;
    if (text)
      text->hide();
    if (choice)
      choice->hide();
    runtime.SetState(GameState::Gameplay);
    const std::string scrId = "SCRIPT/" + canonical(scriptStem) + ".SCR";
    imoutoVm.loadScript(scriptStem, assets->read(scrId));
    imoutoVm.run();
  }
  std::string expandImoutoMacros(std::string s) const {
    auto replaceAll = [&](const std::string &from, const std::string &to) {
      size_t pos = 0;
      while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
      }
    };
    replaceAll("*1", imoutoProtagGiven);
    replaceAll("*2", imoutoProtagFamily);
    return s;
  }
  bool getImoutoBit(size_t byteBase, int bit) const {
    if (bit < 0)
      return false;
    const size_t idx = byteBase + (size_t(bit) >> 3);
    if (idx >= imoutoSystemBin.size())
      return false;
    return (imoutoSystemBin[idx] & uint8_t(1u << (bit & 7))) != 0;
  }
  void setImoutoBit(size_t byteBase, int bit, bool value) {
    if (bit < 0)
      return;
    const size_t idx = byteBase + (size_t(bit) >> 3);
    if (idx >= imoutoSystemBin.size())
      return;
    const uint8_t mask = uint8_t(1u << (bit & 7));
    if (value)
      imoutoSystemBin[idx] |= mask;
    else
      imoutoSystemBin[idx] &= uint8_t(~mask);
  }
  int32_t execImoutoCommand(const std::string &cmd,
                            const std::vector<ImoutoSlot> &args) {
    auto argInt = [&](size_t i, int32_t def = 0) -> int32_t {
      return i < args.size() && args[i].type == ImoutoSlot::Type::Int
                 ? args[i].intVal
                 : def;
    };
    auto argStr = [&](size_t i) -> std::string {
      return i < args.size() && args[i].type == ImoutoSlot::Type::String
                 ? args[i].strVal
                 : std::string();
    };
    const std::string key = canonical(cmd);

    if (key == "CHR_SET") {
      imoutoCharId = argInt(0, 0);
      imoutoSpeaker = argStr(1);
      return 0;
    }
    if (key == "SETMES") {
      imoutoPendingText = expandImoutoMacros(argStr(0));
      const std::string vDef = argStr(1);
      const std::string vAlt = argStr(2);
      const std::string chosen = !vAlt.empty() ? vAlt : vDef;
      imoutoPendingVoice = chosen.empty()
                               ? std::string()
                               : ("VDATA/" + canonical(chosen) + ".OGG");
      return 0;
    }
    if (key == "PUTMES") {
      presented = composeImoutoScene();
      currentRawSpeaker = imoutoSpeaker;
      currentRawDialogue = imoutoPendingText;
      currentVoiceId = imoutoPendingVoice;
      currentRead = imoutoVm.lastSetBitWasRead;
      if (!imoutoPendingVoice.empty() && assets->contains(imoutoPendingVoice))
        audio(voice, imoutoPendingVoice, "Voice", false);
      else
        voice.Stop();
      if (text) {
        text->setDialogue(imoutoSpeaker, imoutoPendingText);
        text->show();
        if (engine && !imoutoPendingVoice.empty())
          GetVNBacklogRuntime(*engine).setLastVoice(imoutoPendingVoice);
      }
      imoutoPendingText.clear();
      imoutoPendingVoice.clear();
      pending = "MESSAGE";
      imoutoVm.waiting = true;
      deadline = 0;
      canvasKey.clear();
      return 0;
    }
    if (key == "SETSEL") {
      std::vector<std::string> labels;
      for (size_t i = 0; i + 1 < args.size(); i += 2) {
        if (args[i].type == ImoutoSlot::Type::String && !args[i].strVal.empty())
          labels.push_back(expandImoutoMacros(args[i].strVal));
      }
      if (labels.empty()) {
        imoutoVm.setSystemReg(32, 0);
        return 0;
      }
      presented = composeImoutoScene();
      if (text)
        text->hide();
      pending = "CHOICE";
      imoutoVm.waiting = true;
      std::vector<ChoiceOption> opts;
      for (size_t i = 0; i < labels.size(); ++i)
        opts.push_back({labels[i], int(i)});
      choice->setQuestion("", opts, [this](int index, const std::string &) {
        choice->hide();
        imoutoVm.setSystemReg(32, index);
        pending.clear();
        imoutoVm.resume();
      });
      canvasKey.clear();
      return 0;
    }
    if (key == "CHANGE") {
      const std::string nextStem = argStr(0);
      if (!nextStem.empty()) {
        const std::string scrId = "SCRIPT/" + canonical(nextStem) + ".SCR";
        if (assets->contains(scrId)) {
          imoutoVm.loadScript(nextStem, assets->read(scrId));
        } else {
          warn("Missing Imouto script: " + scrId);
        }
      }
      return 0;
    }
    if (key == "WAIT") {
      const int ms = argInt(0, 0);
      if (ms > 0) {
        pending = "WAIT";
        waitSkippable = true;
        waitSe = false;
        deadline = SDL_GetTicks() + uint64_t(ms);
        imoutoVm.waiting = true;
      }
      return 0;
    }
    if (key == "MOVIE") {
      const std::string file = argStr(0);
      if (movie && !file.empty()) {
        try {
          movie->open(assets->movieHandle(file), file, false, "Movie");
          pending = "MOVIE";
          videoSkip.reset();
          imoutoVm.waiting = true;
          if (text)
            text->hide();
        } catch (const std::exception &e) {
          warn(std::string("Imouto Movie failed: ") + e.what());
        }
      }
      return 0;
    }
    if (key == "ENDING") {
      if (argInt(0, 0) == 1)
        setImoutoBit(0x00, 0, true);
      openImoutoMenu();
      imoutoVm.waiting = true;
      return 0;
    }
    if (key == "LDBG") {
      const std::string name = argStr(0);
      const int tint = argInt(1, 0);
      const int x = argInt(2, 0);
      const int y = argInt(3, 0);
      const int slot = argInt(4, 0);
      std::string path = "BGDATA/" + canonical(name) + ".BGD";
      if (!assets->contains(path))
        path = "BGDATA/NOBG.BGD";
      if (slot == 0) {
        imoutoBgLayers.clear();
        imoutoSprites.clear();
        bgOffsetX = bgOffsetY = 0;
        background = path;
      }
      imoutoTint = tint;
      imoutoBgLayers[slot] = {path, x, y};
      return 0;
    }
    if (key == "LDEV") {
      const std::string name = argStr(0);
      if (args.size() > 1 && args[1].type == ImoutoSlot::Type::Int)
        setImoutoBit(0x80, args[1].intVal + 0x400, true);
      const int x = argInt(2, 0);
      const int y = argInt(3, 0);
      const int slot = argInt(4, 0);
      std::string path = "EVDATA/" + canonical(name) + ".CGD";
      if (!assets->contains(path))
        path = "EVDATA/BLACK.CGD";
      if (slot == 0) {
        imoutoBgLayers.clear();
        imoutoSprites.clear();
        bgOffsetX = bgOffsetY = 0;
        background = path;
      }
      imoutoBgLayers[slot] = {path, x, y};
      return 0;
    }
    if (key == "LDCHR" || key == "RLDCHR" || key == "LLDCHR" || key == "LCH" ||
        key == "CCH" || key == "RCH") {
      const std::string name = argStr(0);
      const int charId = argInt(1, 0);
      const std::string path = "CHRDATA/" + canonical(name) + ".CHR";
      auto eraseAtX = [&](int targetX) {
        imoutoSprites.erase(std::remove_if(imoutoSprites.begin(),
                                           imoutoSprites.end(),
                                           [targetX](const ImoutoSprite &s) {
                                             return s.x == targetX;
                                           }),
                            imoutoSprites.end());
      };
      int x = 0, y = 0;
      if (key == "LDCHR") {
        x = argInt(2, 0);
        y = argInt(3, 0);
        const int keep = argInt(4, 0);
        if (keep != 1)
          eraseAtX(x);
      } else if (key == "RLDCHR") {
        for (auto &s : imoutoSprites)
          if (s.x == 0 && s.charId > 0)
            s.x = -200;
        eraseAtX(200);
        x = 200;
      } else if (key == "LLDCHR") {
        for (auto &s : imoutoSprites)
          if (s.x == 0 && s.charId > 0)
            s.x = 200;
        eraseAtX(-200);
        x = -200;
      } else if (key == "LCH") {
        eraseAtX(-200);
        x = -200;
      } else if (key == "CCH") {
        eraseAtX(0);
        x = 0;
      } else if (key == "RCH") {
        eraseAtX(200);
        x = 200;
      }
      if (assets->contains(path))
        imoutoSprites.push_back({path, charId, x, y, imoutoTint});
      return 0;
    }
    if (key == "CCC") {
      imoutoSprites.clear();
      return 0;
    }
    if (key == "BGDXY") {
      // v1.01 behavior: reposition layered BG/EV slot >= 1 to (x, y)
      const int slot = argInt(0, 0);
      const int x = argInt(1, 0);
      const int y = argInt(2, 0);
      if (slot >= 1) {
        auto it = imoutoBgLayers.find(slot);
        if (it != imoutoBgLayers.end()) {
          it->second.x = x;
          it->second.y = y;
        }
      }
      presented = composeImoutoScene();
      canvasKey.clear();
      return 0;
    }
    if (key == "EVSCR" || key == "SCROLL") {
      const int dx = argInt(0, 0);
      const int dy = argInt(1, 0);
      const int dur = std::max(0, argInt(3, 0));
      scrollStartX = bgOffsetX;
      scrollStartY = bgOffsetY;
      scrollTargetX = bgOffsetX + dx;
      scrollTargetY = bgOffsetY + dy;
      scrollDuration = dur;
      if (dur > 0) {
        scrollStart = SDL_GetTicks();
        pending = "SCROLLCG";
        deadline = scrollStart + uint64_t(dur);
        imoutoVm.waiting = true;
      } else {
        bgOffsetX = scrollTargetX;
        bgOffsetY = scrollTargetY;
        presented = composeImoutoScene();
        canvasKey.clear();
      }
      return 0;
    }
    if (key == "FADE") {
      const int mode = argInt(0, 4);
      const int ms = std::max(0, argInt(1, 400));
      transitionFrom =
          presented.rgba.empty() ? composeImoutoScene() : presented;
      if (mode == 0) {
        imoutoBgLayers.clear();
        imoutoSprites.clear();
        if (assets->contains("BGDATA/BLACK.BGD"))
          imoutoBgLayers[0] = {"BGDATA/BLACK.BGD", 0, 0};
      } else if (mode == 5) {
        imoutoSprites.clear();
      }
      presented = composeImoutoScene();
      if (mode == 2) {
        // Fade out to white
        presented.rgba.assign(800u * 600u * 4u, 255);
      }
      if (ms > 0 && !skipMode && !skipHeld) {
        transitionType = (mode >= 6 && mode <= 34) ? mode : 4;
        transitionOption = 0;
        transitionDuration = ms;
        transitionStart = SDL_GetTicks();
        pending = "WIPE";
        deadline = transitionStart + uint64_t(ms);
        imoutoVm.waiting = true;
      } else {
        transitionDuration = 0;
      }
      canvasKey.clear();
      return 0;
    }
    if (key == "MANPU") {
      if (!args.empty() && args[0].type == ImoutoSlot::Type::String) {
        const std::string name = argStr(0);
        const int slot = std::clamp(argInt(2, 0), 0, 7);
        const int seqId = argInt(3, 0);
        const int x = argInt(4, 0);
        const int y = argInt(5, 0);
        const std::string path = "SYSDAT/" + canonical(name) + ".MANP";
        if (assets->contains(path)) {
          auto &mp = imoutoManpu[size_t(slot)];
          mp.path = path;
          mp.anim = decodeManpu(assets->read(path));
          mp.seqId = seqId;
          mp.x = x;
          mp.y = y;
          mp.active = true;
          mp.frameId =
              mp.anim.frames.empty() ? -1 : mp.anim.frames.begin()->first;
          auto sit = mp.anim.sequences.find(seqId);
          if (sit != mp.anim.sequences.end()) {
            for (uint32_t w : sit->second) {
              if ((w >> 24) == 0x10) {
                mp.frameId = int(w & 0xFFFFFFu);
                break;
              }
            }
          }
        }
      } else {
        const int op = argInt(0, 0);
        const int slot = std::clamp(argInt(1, 0), 0, 7);
        auto &mp = imoutoManpu[size_t(slot)];
        if (op == 0 || op == 5) {
          mp.active = false;
        } else if (op == 3) {
          mp.seqId = argInt(2, 0);
          if (args.size() > 3)
            mp.x = argInt(3, mp.x);
          if (args.size() > 4)
            mp.y = argInt(4, mp.y);
          mp.active = true;
        }
      }
      canvasKey.clear();
      return 0;
    }
    if (key == "PLAYBGM") {
      if (!args.empty() && args[0].type != ImoutoSlot::Type::Int)
        return 0; // Native ignores string arg0 typo (e.g. Playbgm("M04"))
      const int mode = argInt(0, 0);
      if (mode == 0 || mode == 3) {
        bgm.Stop();
        music.clear();
      } else if (mode == 1 || mode == 2) {
        const std::string name = argStr(1);
        const std::string path = "BGMDATA/" + canonical(name) + ".OGG";
        if (!name.empty() && assets->contains(path)) {
          audio(bgm, path, "BGM", true);
          music = path;
        }
      }
      return 0;
    }
    if (key == "PLAYSE") {
      const int mode = argInt(1, 0);
      if (mode == 0 || mode == 3) {
        se.Stop();
      } else if (mode == 1 || mode == 2) {
        const std::string name = argStr(2);
        const std::string path = "SEDATA/" + canonical(name) + ".OGG";
        if (!name.empty() && assets->contains(path))
          audio(se, path, "Se", mode == 2);
      }
      return 0;
    }
    if (key == "SETFLAG") {
      setImoutoBit(0x80, argInt(0, -1), true);
      return 0;
    }
    if (key == "RESETFLAG") {
      setImoutoBit(0x80, argInt(0, -1), false);
      return 0;
    }
    if (key == "GETFLAG") {
      imoutoVm.setSystemReg(33, getImoutoBit(0x80, argInt(0, -1)) ? 1 : 0);
      return 0;
    }
    if (key == "SETSYSBIT") {
      setImoutoBit(0x00, argInt(0, -1), argInt(1, 0) != 0);
      return 0;
    }
    if (key == "GETSYSBIT") {
      imoutoVm.setSystemReg(33, getImoutoBit(0x00, argInt(0, -1)) ? 1 : 0);
      return 0;
    }
    if (key == "SETRCFLAG") {
      setImoutoBit(0x80, argInt(0, -1) + 0x400, true);
      return 0;
    }
    return 0;
  }
  void begin(const std::string &script = "START") {
    if (movie)
      movie->stop();
    bgm.Stop();
    se.Stop();
    voice.Stop();
    music.clear();
    sprites.clear();
    background = "GRAPHIC/黒.BGD";
    pending.clear();
    autoMode = skipMode = skipHeld = false;
    text->concealed = false;
    text->controls.reset();
    title = false;
    deadline = 0;
    runtime.SetState(GameState::Menu);
    text->hide();
    choice->hide();
    byteOverlay.clear();
    transitionDuration = 0;
    presented = image(background);
    canvasKey.clear();
    GetVNBacklogRuntime(*engine).clear();
    saveSystem();
    pendingScript = script;
    openingPlayed = true;
    vm.start(script);
    // Keep the original boot warnings/logos; globals absent on first boot are
    // zero.
    vm.run();
  }
  void bind(Engine &e) {
    engine = &e;
    if (!stage) {
      auto &history = e.configureSharedScene<VNBacklog, SummerBacklog>(*assets);
      history.setVoiceReplay([this, history = &history](const std::string &id) {
        audio(history->replay, id, "Voice");
      });
      stage = std::make_unique<Stage>(e);
      text = std::make_unique<SummerText>(e, *assets);
      text->autoActive = [this] { return autoMode; };
      text->skipActive = [this] { return skipMode || skipHeld; };
      if (assets->isLostM() ||
          assets->contains("GRAPHIC/MESFRAME/MESFRAME.TEXB")) {
        text->controls.buttons[0].action = [this] {
          skipMode = !skipMode;
          if (skipMode)
            autoMode = false;
        };
        text->controls.buttons[1].action = [this] {
          GetVNBacklogRuntime(*engine).open();
        };
        text->controls.buttons[2].action = [this] {
          GetInGameMenuRuntime(*engine).open();
        };
        text->controls.buttons[3].action = [this] {
          const auto qs = saveDirectory() / "quicksave.vn";
          if (std::filesystem::exists(qs))
            runtime.loadGame(qs.string());
        };
        text->controls.buttons[4].action = [this] {
          runtime.saveGame((saveDirectory() / "quicksave.vn").string());
        };
        text->controls.buttons[5].action = [this] {
          autoMode = !autoMode;
          if (autoMode)
            skipMode = false;
          deadline = 0;
        };
        text->controls.buttons[6].action = [this] { text->concealed = true; };
      } else {
        text->controls.buttons[0].action = [this] {
          skipMode = !skipMode;
          if (skipMode)
            autoMode = false;
        };
        text->controls.buttons[1].action = [this] {
          GetVNBacklogRuntime(*engine).open();
        };
        text->controls.buttons[2].action = [this] {
          autoMode = !autoMode;
          if (autoMode)
            skipMode = false;
          deadline = 0;
        };
        text->controls.buttons[3].action = [this] {
          GetInGameMenuRuntime(*engine).open();
        };
        text->controls.buttons[4].action = [this] { text->concealed = true; };
        text->controls.buttons[5].action = [this] {
          runtime.saveGame((saveDirectory() / "quicksave.vn").string());
        };
      }
      choice = std::make_unique<SummerChoice>(e, *assets);
      movie = std::make_unique<MediaPlayer>(e.GetAudio());
      choice->nativeSound = [this](const std::string &id) {
        audio(se, "SOUND/" + id + ".ogg", "Se");
      };
      choice->nativeSoundPlaying = [this] { return se.Playing(); };
      stage->draw = [this](RendererRef r, float scale) { draw(r, scale); };
      stage->event = [this](EventRef e) { return event(e); };
    }
    runtime.AddScene(stage.get());
    runtime.AddScene(text.get());
    runtime.AddScene(choice.get());
    if (meo || imouto) {
      runtime.AddScene(stage.get(), GameState::Gameplay);
      runtime.AddScene(text.get(), GameState::Gameplay);
      runtime.AddScene(choice.get(), GameState::Gameplay);
    }
    auto &backlog = GetVNBacklogRuntime(e);
    runtime.AddScene(&backlog);
    if (meo || imouto)
      runtime.AddScene(&backlog, GameState::Gameplay);
    auto &menu = GetInGameMenuRuntime(e);
    menu.close();
    if (meo) {
      menu.SetPresentation(
          [this](RendererRef renderer) {
            SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 185);
            SDL_FRect dim{0, 0, 1280, 720};
            SDL_RenderFillRect(&renderer, &dim);
            auto &atlas = assets->texture(renderer, "GRAPHIC/SYSTEM.TEX");
            SDL_FRect source{0, 0, 640, 60};
            SDL_FRect target{80, 60, 1120, 105};
            RenderUpscaledTexture(&renderer, &atlas, &source, &target);
          },
          [this](RendererRef renderer, int index, bool focused) {
            static constexpr float rows[] = {328, 240, 284, 196, 196, 328};
            static constexpr const char *labels[] = {
                "CONTINUE", "BACK LOG", "SYSTEM", "SAVE", "LOAD", "TITLE"};
            auto &atlas = assets->texture(renderer, "GRAPHIC/SYSTEM.TEX");
            SDL_FRect source{0, rows[index], 640, 42};
            SDL_FRect target{120, float(180 + index * 74), 1040, 62};
            RenderUpscaledTexture(&renderer, &atlas, &source, &target);
            if (focused) {
              SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
              SDL_SetRenderDrawColor(&renderer, 255, 220, 130, 70);
              SDL_RenderFillRect(&renderer, &target);
            }
            engine->GetUI().text(renderer, labels[index], 900, target.y + 15,
                                 230, {255, 255, 255, 255});
          },
          [](int index) {
            return SDL_FRect{120, float(180 + index * 74), 1040, 62};
          });
    } else {
      menu.SetPresentation({}, {}, {});
    }
    menu.SetOnSave([this] { openSaves(false); });
    menu.SetOnLoad([this] { openSaves(true); });
    menu.SetOnReturnToMenu([this] {
      GetInGameMenuRuntime(*engine).close();
      if (meo)
        openMeoMenu();
      else if (imouto)
        openImoutoMenu();
      else
        begin();
    });
    if (!meo)
      runtime.AddScene(&menu);
    if (imouto)
      runtime.AddScene(&menu, GameState::Gameplay);
    if (!loaded) {
      loadSystem();
      loaded = true;
      if (meo)
        openMeoLogo();
      else if (imouto)
        openImoutoMenu();
      else
        begin();
    }
  }
  void options(const std::string &question, std::vector<std::string> labels,
               const std::string &kind, const std::vector<int> &ids = {}) {
    pending = kind;
    vm.waiting = true;
    text->hide();
    std::vector<ChoiceOption> options;
    for (size_t i = 0; i < labels.size(); ++i)
      options.push_back(
          {translated(labels[i]), ids.empty() ? int(i) : ids.at(i)});
    choice->setQuestion(question, options,
                        [this, kind](int index, const std::string &) {
                          choice->hide();
                          pending.clear();
                          if (kind == "SELECT") {
                            vm.markRead(vm.resource.children.at(1 + index * 2));
                            vm.variables[choiceTarget] = int64_t(index);
                            saveSystem();
                          } else if (kind == "UNIFORM") {
                            vm.variables["@制服番号"] = int64_t(index + 2);
                            vm.variables["@EV制服"] = int64_t(index + 24);
                          } else if (kind == "MAP")
                            vm.variables["@移動場所"] = int64_t(index);
                          vm.resume();
                        });
  }
  void message() {
    auto &r = vm.resource.children;
    if (r.size() < 5)
      throw std::runtime_error("Invalid MESSAGE resource");
    int msgNameId = int(number(vm.evaluate(r[3])));
    int actor = int(number(vm.evaluate(r[4])));
    currentMessageActor = actor;
    std::string speaker =
        actor > 0 && actor < int(speakers.size()) ? speakers[actor] : "";
    if (assets->isLostM() && msgNameId < 0)
      speaker.clear();

    currentRead = vm.hasRead(r[1]);
    const auto names = playerNames();
    if (actor == 1 && !names[1].empty())
      speaker = names[1];
    const int64_t changeMask = number(vm.variables["$ChangePlayerName"]);
    const SDL_Color actorCol = (actor >= 0 && actor < int(speakerColors.size()))
                                   ? speakerColors[size_t(actor)]
                                   : SDL_Color{240, 240, 245, 255};
    text->setLostMMetadata(msgNameId, (changeMask & 1) != 0, actorCol);
    currentRawSpeaker = speaker;
    currentRawDialogue =
        substitutePlayerNames(string(vm.evaluate(r[2])), names);
    activeOriginalKeys.clear();
    if (!speaker.empty())
      activeOriginalKeys.push_back(speaker);
    if (!currentRawDialogue.empty())
      activeOriginalKeys.push_back(currentRawDialogue);
    currentVoiceId.clear();
    text->setDialogue(translated(speaker), translated(currentRawDialogue));
    text->show();
    text->concealed = false;
    if (!currentRead)
      skipMode = false;
    voice.Stop();
    if (r.size() > 5) {
      std::string stemStr;
      if ((changeMask & 2) != 0 && r.size() > 6)
        stemStr = string(vm.evaluate(r[6]));
      if (stemStr.empty())
        stemStr = string(vm.evaluate(r[5]));
      auto id = "VOICE/" + stemStr + ".ogg";
      if (!assets->contains(id) &&
          assets->contains("VOICE/" + stemStr + ".wav"))
        id = "VOICE/" + stemStr + ".wav";
      if (assets->contains(id)) {
        currentVoiceId = id;
        audio(voice, id, "Voice");
        GetVNBacklogRuntime(*engine).setLastVoice(id);
      }
    }
    vm.waiting = true;
    pending = "MESSAGE";
    deadline = 0;
  }
  void function(const std::string &name, const std::vector<Value> &args) {
    auto arg = [&](size_t i) -> Value {
      return i < args.size() ? args[i] : Value(int64_t(0));
    };
    if (name == "MESSAGE") {
      message();
      return;
    }
    if (name == "SELECT") {
      const auto &node =
          vm.programs().at(vm.position.script).code.at(vm.position.pc - 1).node;
      choiceTarget =
          node.children.empty()
              ? "@RESULT"
              : (node.children[0].attr("type") == "GLOBAL" ? "$" : "@") +
                    node.children[0].text;
      std::vector<std::string> labels;
      std::vector<int> ids;
      std::vector<bool> read;
      for (size_t i = 2; i < vm.resource.children.size() && ids.size() < 32;
           i += 2) {
        const auto index = int((i - 2) / 2);
        if (std::size_t(index + 1) < args.size() && !number(args[index + 1]))
          continue;
        labels.push_back(string(vm.evaluate(vm.resource.children[i])));
        ids.push_back(index);
        read.push_back(vm.hasRead(vm.resource.children[i - 1]));
      }
      if (labels.empty()) {
        vm.variables[choiceTarget] = int64_t(-1);
        return;
      }
      options("", std::move(labels), "SELECT", ids);
      choice->setReadStates(std::move(read));
      return;
    }
    if (name == "LOADBACK" || name == "LOADEVENT") {
      auto id = "GRAPHIC/" + string(arg(0));
      id += graphicSuffix(uint32_t(number(arg(1)))) +
            (name == "LOADBACK" ? ".BGD" : ".CGD");
      bgOffsetX = name == "LOADEVENT" ? int(number(arg(2))) : 0;
      bgOffsetY = name == "LOADEVENT" ? int(number(arg(3))) : 0;
      scrollDuration = 0;
      if (assets->contains(id)) {
        background = id;
        sprites.clear();
      } else
        warn("Missing picture: " + id);
      return;
    }
    if (name == "SCROLLCG") {
      scrollStartX = bgOffsetX;
      scrollStartY = bgOffsetY;
      scrollTargetX = int(number(arg(1)));
      scrollTargetY = int(number(arg(2)));
      scrollDuration = int(number(arg(0)));
      if (scrollDuration > 0) {
        scrollStart = SDL_GetTicks();
        pending = "SCROLLCG";
        vm.waiting = true;
        deadline = scrollStart + uint64_t(scrollDuration);
        text->hide();
      } else {
        bgOffsetX = scrollTargetX;
        bgOffsetY = scrollTargetY;
        presented = composeScene();
      }
      return;
    }
    if (name == "ADDVOICEANIMECHAR") {
      extraVoiceActors_.push_back(int(number(arg(0))));
      return;
    }
    if (name == "LOADCHAR" || name == "LOADADDCHAR") {
      auto id =
          "GRAPHIC/" +
          (name == "LOADADDCHAR" ? std::string("ADDCHAR/") : std::string()) +
          string(arg(0));
      id += graphicSuffix(uint32_t(number(arg(1)))) + ".CHR";
      if (assets->contains(id)) {
        if (!origins.count(id)) {
          auto img = assets->picture(id);
          origins[id] = {img.x, img.y};
        }
        auto &sprite =
            sprites[int(number(arg(2))) + (name == "LOADADDCHAR" ? 100 : 0)];
        if (sprite.id.empty())
          sprite.creationOrder = ++nextSpriteOrder;
        sprite.id = id;
        sprite.requestedPosition =
            args.size() > 3 ? int32_t(number(arg(3))) : -1;
      } else
        warn("Missing character: " + id);
      return;
    }
    if (name == "ERASECHAR") {
      sprites.erase(int(number(arg(0))));
      sprites.erase(int(number(arg(0))) + 100);
      return;
    }
    if (name == "PLAYBGM") {
      auto id = "SOUND/BGM/" + string(arg(0)) + ".ogg";
      if (music != id) {
        audio(bgm, id, "BGM", args.size() < 2 || number(arg(1)) != 0);
        music = id;
      }
      bgmFadeDuration = 0;
      return;
    }
    if (name == "STOPBGM") {
      bgm.Stop();
      music.clear();
      bgmFadeDuration = 0;
      return;
    }
    if (name == "FADEOUTBGM") {
      int speed = int(number(arg(1)));
      if (speed < 0 || speed >= 3)
        speed = 0;
      static const int duration[3] = {1000, 500, 2000};
      bgmFadeDuration = duration[speed];
      bgmFadeStart = SDL_GetTicks();
      nextMusic = args.empty() ? "" : "SOUND/BGM/" + string(arg(0)) + ".ogg";
      return;
    }
    if (name == "PLAYSE") {
      audio(se, "SOUND/" + string(arg(0)) + ".ogg", "Se");
      return;
    }
    if (name == "STOPSE") {
      se.Stop();
      return;
    }
    if (name == "WAITTIME" || name == "WAITSE") {
      waitSe = name == "WAITSE";
      waitSkippable = (number(arg(waitSe ? 1 : 1)) & 1) == 0;
      pending = "WAIT";
      vm.waiting = true;
      int64_t delay =
          waitSe ? (args.empty() || number(arg(0)) < 0 ? INT64_C(86400000)
                                                       : number(arg(0)))
                 : number(arg(0));
      deadline = SDL_GetTicks() + std::clamp<int64_t>(delay, 0, 86400000);
      return;
    }
    if (name == "EASYTITLE") {
      title = true;
      titleFocus = 0;
      pending = "TITLE";
      vm.waiting = true;
      deadline = 0;
      background = "GRAPHIC/TITLE/TITLE.CGD";
      sprites.clear();
      text->hide();
      loadEasyTitle(string(arg(0)));
      return;
    }
    if (name == "UNISELECT" || name == "MAPSELECT") {
      bool uniform = name == "UNISELECT";
      pending = uniform ? "UNIFORM" : "MAP";
      vm.waiting = true;
      text->hide();
      int mode = uniform ? 100 : int(number(arg(0)));
      vm.variables["@KOTONOHA_MAP_MODE"] = int64_t(mode);
      choice->setNative(mode, *assets,
                        [this, uniform](int value, const std::string &) {
                          if (!lastCanvas.rgba.empty())
                            presented = lastCanvas;
                          choice->hide();
                          pending.clear();
                          if (uniform) {
                            vm.variables["@制服番号"] = int64_t(value + 2);
                            vm.variables["@EV制服"] = int64_t(value + 24);
                          } else
                            vm.variables["@移動場所"] = int64_t(value);
                          vm.resume();
                        });
      return;
    }
    if (name == "MESSWINDOW") {
      if (number(arg(0)))
        text->show();
      else
        text->hide();
      return;
    }
    if (name == "WIPE") {
      text->hide();
      transitionType = int(number(arg(0)));
      transitionOption = int(number(arg(1)));
      transitionDuration = wipeDuration(transitionType);
      transitionStart = SDL_GetTicks();
      transitionFrom =
          presented.rgba.empty() ? image("GRAPHIC/黒.BGD") : presented;
      presented = composeScene();
      if (transitionDuration > 0) {
        pending = "WIPE";
        vm.waiting = true;
        deadline = transitionStart + transitionDuration;
      }
      return;
    }
    if (name == "EASYCGMODE" || name == "EASYSOUNDMODE" ||
        name == "EASYREPLAYMODE") {
      vm.variables["@RESULT"] = std::string("BACK");
      return;
    }
    if (name == "GAMESTART") {
      // Global overlays (such as an open name form) may already be registered
      // for Gameplay; never register the same scene twice or it would receive
      // every event twice.
      for (auto *scene : runtime.GetScenes(GameState::Menu)) {
        const auto &gameplay = runtime.GetScenes(GameState::Gameplay);
        if (std::find(gameplay.begin(), gameplay.end(), scene) ==
            gameplay.end())
          runtime.AddScene(scene, GameState::Gameplay);
      }
      runtime.SetState(GameState::Gameplay);
      return;
    }
    if (name == "GAMEEND")
      return;
    if (name == "SETQUITCODE") {
      if (number(arg(0)) != 0)
        vm.ended = true;
      return;
    }
    if (name == "SETDEFAULTNAME") {
      setDefaultNames();
      return;
    }
    if (name == "NAMEENTRY") {
      enterNames();
      return;
    }
    if (name == "TOPLADY") {
      vm.variables["@FLAG020"] = int64_t(topLady(
          {number(vm.variables["@FLAG001"]), number(vm.variables["@FLAG002"]),
           number(vm.variables["@FLAG003"]),
           number(vm.variables["@FLAG004"])}));
      return;
    }
    if (name == "BYTEVIEW") {
      byteOverlay = byteViewPicture(int(number(vm.variables["@移動場所"])));
      if (!byteOverlay.empty()) {
        pending = "BYTEVIEW";
        vm.waiting = true;
        deadline = SDL_GetTicks() + byteViewDuration(500);
        text->hide();
      }
      return;
    }
    if (name == "PLAYMOVIE") {
      movie->open(assets->movieHandle(string(arg(0))), string(arg(0)), false,
                  "BGM");
      text->hide();
      vm.waiting = true;
      pending = "MOVIE";
      videoSkip.reset();
      return;
    }
    if (name == "QUAKEX" || name == "QUAKEY") {
      const int amp = int(number(arg(0)));
      const int cycles = int(number(arg(1)));
      const int stepScale = int(number(arg(2)));
      const int stepMs = std::max(16, (500 * std::max(1, stepScale)) / 500);
      if (amp == 0 || cycles <= 0)
        return;
      quakeAxis = (name == "QUAKEX") ? 1 : 2;
      quakeAmp = amp;
      quakeCycles = cycles;
      quakeStepMs = stepMs;
      quakeStart = SDL_GetTicks();
      quakeOffsetX = (quakeAxis == 1) ? -amp : 0;
      quakeOffsetY = (quakeAxis == 2) ? -amp : 0;
      deadline = quakeStart + uint64_t(cycles) * 4u * uint64_t(stepMs);
      pending = "QUAKE";
      vm.waiting = true;
      return;
    }
    throw std::runtime_error("Unimplemented native function: " + name);
  }
  void picture(RendererRef r, const std::string &id, SDL_FRect target,
               const SDL_FRect *source = nullptr) {
    auto &texture = assets->texture(r, id);
    RenderUpscaledTexture(&r, &texture, source, &target);
  }
  const Image &image(const std::string &id) {
    auto it = decodedImages.find(id);
    if (it == decodedImages.end())
      it = decodedImages.emplace(id, assets->picture(id)).first;
    return it->second;
  }
  const std::vector<CharacterAnimation> &animations(const std::string &id) {
    auto it = characterAnimations.find(id);
    if (it == characterAnimations.end())
      it = characterAnimations
               .emplace(id, decodeCharacterAnimations(assets->read(id)))
               .first;
    return it->second;
  }
  Image composeScene() {
    const auto &bgImg = image(background);
    Image canvas;
    if (bgImg.width == 800 && bgImg.height == 600 && bgOffsetX == 0 &&
        bgOffsetY == 0) {
      canvas = bgImg;
    } else {
      canvas.width = 800;
      canvas.height = 600;
      canvas.rgba.assign(800 * 600 * 4, 0);
      for (size_t i = 3; i < canvas.rgba.size(); i += 4)
        canvas.rgba[i] = 255;
      compositeImage(canvas, bgImg, bgOffsetX, bgOffsetY);
    }
    std::vector<std::pair<int, Sprite *>> ordered;
    for (auto &[slot, sprite] : sprites)
      if (slot < 100)
        ordered.emplace_back(slot, &sprite);
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
      return a.second->creationOrder < b.second->creationOrder;
    });
    std::vector<CharacterPlacement> positions;
    for (const auto &[slot, sprite] : ordered)
      positions.push_back({sprite->requestedPosition, sprite->positionSlot});
    assignCharacterPositions(
        positions, std::max<unsigned>(1u, unsigned(positionOffsets.size())));
    const bool voicePlaying = voice.Playing();
    const int mouthFrame = voicePlaying ? int((SDL_GetTicks() / 120) % 3) : 0;
    auto updateMouth = [&](std::vector<CharacterAnimation> &anims) {
      for (auto &anim : anims)
        if (anim.selector == 0)
          anim.currentFrame = 0;
      if (voicePlaying) {
        if (currentMessageActor > 0)
          selectCharacterAnimationFrame(anims, currentMessageActor, 0,
                                        mouthFrame);
        for (int extraActor : extraVoiceActors_)
          selectCharacterAnimationFrame(anims, extraActor, 0, mouthFrame);
      }
    };
    for (size_t i = 0; i < ordered.size(); ++i) {
      auto &[slot, pointer] = ordered[i];
      auto &sprite = *pointer;
      sprite.positionSlot = positions[i].previous;
      // SRV1 uses 3 slots {0,-200,200}; SRV2 uses 7 from GRAPHIC.INI.
      sprite.x = sprite.positionSlot < positionOffsets.size()
                     ? positionOffsets[sprite.positionSlot]
                     : 0;
      auto origin = origins.at(sprite.id);
      compositeImage(canvas, image(sprite.id), origin.first + sprite.x,
                     origin.second);
      auto anims = animations(sprite.id);
      updateMouth(anims);
      compositeCharacterAnimations(canvas, anims, sprite.x);
      // Native additional characters are attached to their base actor and
      // rendered immediately after it using the same assigned x offset.
      const auto added = sprites.find(slot + 100);
      if (added != sprites.end()) {
        const auto addOrigin = origins.at(added->second.id);
        compositeImage(canvas, image(added->second.id),
                       addOrigin.first + sprite.x, addOrigin.second);
        auto addAnims = animations(added->second.id);
        updateMouth(addAnims);
        compositeCharacterAnimations(canvas, addAnims, sprite.x);
      }
    }
    return canvas;
  }
  void draw(RendererRef r, float) {
    if (meo) {
      if (meoLogoActive) {
        SDL_SetRenderDrawColor(&r, 0, 0, 0, 255);
        SDL_RenderClear(&r);
        const SDL_FRect src{0, 0, 640, 480};
        const SDL_FRect dst{0, 0, 800, 600};
        picture(r, "GRAPHIC/ROGO.TEX", dst, &src);
        // MEOModule.gem @ 0x100028a0: 1000ms fade-in, 2000ms hold, 1000ms
        // fade-out.
        const Uint64 now = SDL_GetTicks();
        const Uint64 elapsed = now >= meoLogoStart ? now - meoLogoStart : 0;
        Uint8 blackAlpha = 0;
        if (elapsed < 1000)
          blackAlpha = Uint8(255 - (elapsed * 255) / 1000);
        else if (elapsed >= 3000)
          blackAlpha =
              Uint8(std::min<Uint64>(255, ((elapsed - 3000) * 255) / 1000));
        if (blackAlpha > 0) {
          SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(&r, 0, 0, 0, blackAlpha);
          SDL_RenderFillRect(&r, &dst);
        }
        return;
      }
      if (meoMenu) {
        SDL_SetRenderDrawColor(&r, 0, 0, 0, 255);
        SDL_RenderClear(&r);
        const SDL_FRect source{0, 0, 640, 480};
        const SDL_FRect destination{0, 0, 800, 600};
        picture(r, "GRAPHIC/TITLE.TEX", destination, &source);
        // Exact TITLE.MAP button rectangles in 640x480 scaled 1.25x to 800x600.
        static constexpr SDL_FRect kTitleBtn800[4] = {
            {410.0f, 120.00f, 240.0f, 57.50f},
            {410.0f, 177.50f, 240.0f, 56.25f},
            {410.0f, 233.75f, 240.0f, 56.25f},
            {410.0f, 303.75f, 240.0f, 56.25f}};
        if (titleFocus >= 0 && titleFocus < 4) {
          const SDL_FRect selected{194, float(480 + titleFocus * 48), 192, 45};
          picture(r, "GRAPHIC/TITLE.TEX", kTitleBtn800[titleFocus], &selected);
        }
        if (meoTitleFadeStart > 0) {
          const Uint64 now = SDL_GetTicks();
          const Uint64 elapsed =
              now >= meoTitleFadeStart ? now - meoTitleFadeStart : 0;
          if (elapsed < 1000) {
            SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(&r, 0, 0, 0,
                                   Uint8(255 - (elapsed * 255) / 1000));
            SDL_RenderFillRect(&r, &destination);
          } else {
            meoTitleFadeStart = 0;
          }
        }
        if (meoSceneMenu) {
          SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(&r, 16, 18, 28, 238);
          const SDL_FRect panel{100, 70, 600, 470};
          SDL_RenderFillRect(&r, &panel);
          SDL_SetRenderDrawColor(&r, 255, 255, 255, 90);
          SDL_RenderRect(&r, &panel);
          const size_t page = meoSceneFocus / 12;
          engine->GetUI().textSized(
              r, "SCENE  UP/DOWN: SELECT  ENTER: PLAY  ESC: BACK", 130, 88, 540,
              18, {255, 255, 255, 255});
          for (size_t row = 0; row < 12; ++row) {
            const size_t index = page * 12 + row;
            if (index >= meoScenes.size())
              break;
            const SDL_Color col = index == meoSceneFocus
                                      ? SDL_Color{255, 212, 110, 255}
                                      : SDL_Color{200, 205, 220, 255};
            engine->GetUI().textSized(r, meoScenes[index], 150,
                                      float(126 + row * 32), 480, 20, col);
          }
        }
        return;
      }
      if (movie && movie->isOpen())
        movie->render(r);
      else {
        SDL_SetRenderDrawColor(&r, 16, 18, 28, 255);
        SDL_RenderClear(&r);
      }
      for (const auto &[name, fade] : meoFades) {
        if (fade.alpha > 0) {
          SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(&r, fade.r, fade.g, fade.b,
                                 Uint8(std::clamp(fade.alpha, 0, 255)));
          const SDL_FRect full{0, 0, 800, 600};
          SDL_RenderFillRect(&r, &full);
        }
      }
      if (meoLoveViewUntil > SDL_GetTicks()) {
        const int64_t love =
            meoLocals.count("L_MISAO") ? meoLocals.at("L_MISAO") : 0;
        const int64_t maxLove =
            meoLocals.count("L_MISAO_MAX")
                ? std::max<int64_t>(1, meoLocals.at("L_MISAO_MAX"))
                : 17;
        SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(&r, 20, 12, 28, 200);
        const SDL_FRect box{560, 24, 216, 36};
        SDL_RenderFillRect(&r, &box);
        SDL_SetRenderDrawColor(&r, 255, 120, 175, 230);
        const float ratio =
            std::clamp(float(love) / float(maxLove), 0.0f, 1.0f);
        const SDL_FRect bar{568, 44, 200.0f * ratio, 10};
        SDL_RenderFillRect(&r, &bar);
        SDL_SetRenderDrawColor(&r, 255, 225, 240, 255);
        const std::string label = "MISAO LOVE " + std::to_string(love) + "/" +
                                  std::to_string(maxLove);
        SDL_RenderDebugText(&r, 570, 29, label.c_str());
      }
      if (meoOverlayOpen && assets->contains("GRAPHIC/SYSTEM.TEX")) {
        // MEO native top overlay bar (640x480 logical space scaled 1.25x to
        // 800x600): bar background + love gauge + 5 SYSTEM.MAP icon buttons +
        // active tooltip banner.
        constexpr float sx = 800.0f / 640.0f;
        constexpr float sy = 600.0f / 480.0f;
        const SDL_FRect barSrc{0, 0, 640, 61};
        const SDL_FRect barDst{0, 0, 640 * sx, 61 * sy};
        picture(r, "GRAPHIC/SYSTEM.TEX", barDst, &barSrc);
        // Love meter fill inside the top bar heart gauge
        const int64_t love =
            meoLocals.count("L_MISAO") ? meoLocals.at("L_MISAO") : 0;
        const int64_t maxLove =
            meoLocals.count("L_MISAO_MAX")
                ? std::max<int64_t>(1, meoLocals.at("L_MISAO_MAX"))
                : 17;
        const float ratio =
            std::clamp(float(love) / float(maxLove), 0.0f, 1.0f);
        if (ratio > 0.0f) {
          const SDL_FRect gaugeSrc{640, 30, 280.0f * ratio, 16};
          const SDL_FRect gaugeDst{96 * sx, 28 * sy, 280.0f * ratio * sx,
                                   16 * sy};
          picture(r, "GRAPHIC/SYSTEM.TEX", gaugeDst, &gaugeSrc);
        }
        // 5 native buttons from SYSTEM.MAP: SPEED, SAVE, BACKLOG, SYSTEM, STOP
        static constexpr SDL_FRect kBtnDst640[5] = {{452, 29, 28, 26},
                                                    {484, 29, 28, 26},
                                                    {516, 29, 27, 26},
                                                    {547, 29, 27, 26},
                                                    {589, 29, 27, 26}};
        static constexpr float kIconW[5] = {28, 28, 27, 27, 27};
        for (int b = 0; b < 5; ++b) {
          const bool focused = (b == meoOverlayFocus);
          const SDL_FRect iconSrc{float(b * 60 + (focused ? 30 : 0)), 124,
                                  kIconW[b], 26};
          const SDL_FRect iconDst{kBtnDst640[b].x * sx, kBtnDst640[b].y * sy,
                                  kBtnDst640[b].w * sx, kBtnDst640[b].h * sy};
          picture(r, "GRAPHIC/SYSTEM.TEX", iconDst, &iconSrc);
        }
        // Active tooltip banner directly below the top bar
        if (meoOverlayFocus >= 0 && meoOverlayFocus < 5) {
          const SDL_FRect tipSrc{0, float(152 + meoOverlayFocus * 44), 640, 41};
          const SDL_FRect tipDst{0, 61 * sy, 640 * sx, 41 * sy};
          picture(r, "GRAPHIC/SYSTEM.TEX", tipDst, &tipSrc);
        }
        // Speed strip indicator when speed > 1x
        if (meoSpeedIndex > 0 && meoSpeedIndex <= 4) {
          const SDL_FRect spdSrc{640, float(85 + (meoSpeedIndex - 1) * 28), 265,
                                 26};
          const SDL_FRect spdDst{16 * sx, 108 * sy, 200 * sx, 20 * sy};
          picture(r, "GRAPHIC/SYSTEM.TEX", spdDst, &spdSrc);
        }
      }
      return;
    }
    if (imouto) {
      std::ostringstream state;
      state << "IMOUTO:" << imoutoMenu << ':' << titleFocus << ':'
            << imoutoVm.currentScriptName << ':' << imoutoVm.pc << ':'
            << bgOffsetX << ':' << bgOffsetY << ':' << transitionDuration << ':'
            << (transitionDuration ? (SDL_GetTicks() - transitionStart) / 8 : 0)
            << ':' << text->isVisible() << ':' << text->concealed;
      for (const auto &[slot, layer] : imoutoBgLayers)
        state << '|' << slot << ':' << layer.path << ':' << layer.x << ':'
              << layer.y;
      for (const auto &sp : imoutoSprites)
        state << '|' << sp.id << ':' << sp.x << ':' << sp.y << ':' << sp.tint;
      for (const auto &mp : imoutoManpu)
        if (mp.active)
          state << "|M:" << mp.path << ':' << mp.frameId << ':' << mp.x << ':'
                << mp.y;
      if (!canvasTexture || canvasKey != state.str()) {
        if (decodedImages.size() > 96)
          decodedImages.clear();
        Image canvas =
            (presented.rgba.empty() || scrollDuration > 0 || imoutoMenu)
                ? composeImoutoScene()
                : presented;
        if (transitionDuration > 0 && !imoutoMenu) {
          const Image *maskPtr = nullptr;
          const auto maskPath = imoutoAlphaMaskPath(transitionType);
          if (!maskPath.empty() && assets->contains(maskPath))
            maskPtr = &image(maskPath);
          const int wipeKind =
              (transitionType >= 6 && transitionType <= 34) ? 3 : 4;
          canvas = wipeFrame(transitionFrom, presented, wipeKind,
                             int(SDL_GetTicks() - transitionStart),
                             transitionDuration, maskPtr, 0);
        }
        if (!imoutoMenu && text->isVisible() && !text->concealed &&
            assets->contains("SYSDAT/WIN.TEXB")) {
          const auto &win = image("SYSDAT/WIN.TEXB");
          // SYSDAT/WIN.TEXB is 732x600; y=0..155 is the message frame + top
          // tabs, y>=156 contains hover/indicator sprite sheets.
          compositeImageRegion(canvas, win, 34, 432, 0, 0,
                               std::min(732, win.width),
                               std::min(156, win.height));
        }
        if (!canvasTexture)
          canvasTexture.reset(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STREAMING,
                                                800, 600));
        if (!canvasTexture || !SDL_UpdateTexture(canvasTexture.get(), nullptr,
                                                 canvas.rgba.data(), 800 * 4))
          throw std::runtime_error(SDL_GetError());
        ++canvasContentSerial;
        lastCanvas = std::move(canvas);
        canvasKey = state.str();
      }
      const SDL_FRect target{0, 0, 800, 600};
      RenderUpscaledTexture(&r, canvasTexture.get(), nullptr, &target,
                            canvasContentSerial);
      if (imoutoMenu) {
        if (assets->contains("SYSDAT/MENU.TEXB")) {
          // SYSDAT/MENU.TEXB (744x351): 9 rows of 39px; left half (x=0..371)
          // is focused, right half (x=372..743) is unfocused.
          // Rows: 0=START, 1=LOAD, 2=CONFIG, 4=EXIT.
          static constexpr int kMenuRows[4] = {0, 1, 2, 4};
          for (int b = 0; b < 4; ++b) {
            const bool focused = (b == titleFocus);
            const SDL_FRect src{focused ? 0.0f : 372.0f,
                                float(kMenuRows[b] * 39), 372.0f, 39.0f};
            const SDL_FRect dst{414.0f, float(350 + b * 48), 372.0f, 39.0f};
            picture(r, "SYSDAT/MENU.TEXB", dst, &src);
          }
        } else {
          static constexpr const char *kLabels[4] = {"START", "LOAD", "CONFIG",
                                                     "EXIT"};
          SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
          for (int b = 0; b < 4; ++b) {
            const SDL_FRect btn{540, float(340 + b * 52), 200, 40};
            const bool focused = (b == titleFocus);
            SDL_SetRenderDrawColor(&r, focused ? 240 : 28, focused ? 130 : 32,
                                   focused ? 170 : 54, focused ? 235 : 190);
            SDL_RenderFillRect(&r, &btn);
            SDL_SetRenderDrawColor(&r, 255, 255, 255, 255);
            SDL_RenderRect(&r, &btn);
            engine->GetUI().textSized(r, kLabels[b], btn.x + 20, btn.y + 8, 160,
                                      22, {255, 255, 255, 255});
          }
        }
      }
      if (pending == "MOVIE" && movie && movie->isOpen()) {
        movie->update(r);
        movie->render(r, target);
      }
      return;
    }
    std::string base =
        choice->mode() >= 0 ? choice->nativeBackground() : background;
    std::ostringstream state;
    state << base << byteOverlay << ':' << bgOffsetX << ':' << bgOffsetY << ':'
          << transitionDuration << ':'
          << (transitionDuration ? (SDL_GetTicks() - transitionStart) / 8 : 0)
          << ':' << (voice.Playing() ? (SDL_GetTicks() / 120) % 3 + 1 : 0)
          << ':' << title << ':' << titleFocus << ':' << text->isVisible()
          << ':' << text->concealed << ':' << choice->mode() << ':'
          << choice->focused();
    for (auto &[slot, sprite] : sprites)
      state << slot << sprite.id << sprite.x;
    if (choice->mode() >= 0)
      for (const auto &[key, value] : vm.variables)
        if (key.compare(0, 6, "@CHAR_") == 0)
          state << key << string(value);
    if (!canvasTexture || canvasKey != state.str()) {
      if (decodedImages.size() > 96)
        decodedImages.clear();
      Image canvas = (choice->mode() >= 0 || title)
                         ? image(base)
                         : ((presented.rgba.empty() || voice.Playing() ||
                             scrollDuration > 0)
                                ? composeScene()
                                : presented);
      if (transitionDuration > 0 && choice->mode() < 0 && !title) {
        const Image *maskPtr = nullptr;
        const auto maskPath = wipeMaskAsset(transitionType);
        if (!maskPath.empty() && assets->contains(maskPath))
          maskPtr = &image(maskPath);
        canvas = wipeFrame(transitionFrom, presented, transitionType,
                           int(SDL_GetTicks() - transitionStart),
                           transitionDuration, maskPtr, transitionOption);
      }
      if (!byteOverlay.empty())
        compositeImage(canvas, image(byteOverlay), 216, 161);
      if (text->isVisible() && !text->concealed) {
        const std::string framePath =
            assets->contains("GRAPHIC/MESFRAME/MESFRAME.TEXB")
                ? "GRAPHIC/MESFRAME/MESFRAME.TEXB"
                : "GRAPHIC/FRAME/MESSFRAME.TEXB";
        if (assets->contains(framePath)) {
          if (assets->isLostM())
            compositeImageRegion(canvas, image(framePath), 64, 426, 0, 0, 707,
                                 171);
          else
            compositeImageRegion(canvas, image(framePath), 0, 391, 0, 0, 800,
                                 209);
        }
      }
      choice->compose(
          canvas, [this](const auto &id) -> const Image & { return image(id); },
          vm);
      if (title) {
        if (assets->contains(titleChipTexture))
          for (size_t i = 0; i < titleChips.size(); ++i) {
            const auto &chip = titleChips[i];
            const bool focused = i == size_t(titleFocus);
            const int dstX = (titleAbsoluteAlign && chip.hasDrawArea)
                                 ? chip.drawX
                                 : titleBaseX;
            const int dstY = (titleAbsoluteAlign && chip.hasDrawArea)
                                 ? chip.drawY
                                 : (titleBaseY + int(i) * titleChipSpacing);
            compositeImageRegion(canvas, image(titleChipTexture), dstX, dstY,
                                 chip.atlasX + (focused ? chip.atlasW : 0),
                                 chip.atlasY, chip.atlasW, chip.atlasH);
          }
      }
      if (!canvasTexture)
        canvasTexture.reset(SDL_CreateTexture(
            &r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 800, 600));
      if (!canvasTexture || !SDL_UpdateTexture(canvasTexture.get(), nullptr,
                                               canvas.rgba.data(), 800 * 4))
        throw std::runtime_error(SDL_GetError());
      ++canvasContentSerial;
      lastCanvas = std::move(canvas);
      canvasKey = state.str();
    }
    SDL_FRect target{float(quakeOffsetX), float(quakeOffsetY), 800, 600};
    RenderUpscaledTexture(&r, canvasTexture.get(), nullptr, &target,
                          canvasContentSerial);
    if (pending == "MOVIE" && movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, target);
    }
    if (pending == "OPENING" && movie && movie->isOpen()) {
      movie->update(r);
      movie->render(r, target);
    }
  }
  void titleAction(int index) {
    if (index < 0 || size_t(index) >= titleChips.size())
      return;
    const std::string chipName = canonical(titleChips[size_t(index)].name);
    if (chipName == "START") {
      title = false;
      pending.clear();
      vm.variables["@RESULT"] = std::string("START");
      vm.resume();
    } else if (chipName == "LOAD") {
      openSaves(true);
    } else if (chipName == "CONFIG") {
      engine->openSettings();
    } else if (chipName == "EXIT" || chipName == "QUIT") {
      SDL_Event e{};
      e.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&e);
    } else {
      title = false;
      pending.clear();
      vm.variables["@RESULT"] = titleChips[size_t(index)].name;
      vm.resume();
    }
  }
  bool event(EventRef e) {
    if (meo && meoLogoActive) {
      if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) ||
          (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
           (e.button.button == SDL_BUTTON_LEFT ||
            e.button.button == SDL_BUTTON_RIGHT))) {
        openMeoMenu(false);
        return true;
      }
      return true;
    }
    if (meo && meoMenu) {
      auto hitMeoTitle = [this](float x, float y) -> int {
        const int mx = int(x * 640.0f / 800.0f);
        const int my = int(y * 480.0f / 600.0f);
        if (mx < 0 || mx >= 640 || my < 0 || my >= 480)
          return -1;
        if (meoTitleMap.size() >= 8 + 640u * 480u) {
          const uint8_t id = meoTitleMap[8 + size_t(my) * 640u + size_t(mx)];
          if (id >= 1 && id <= 4)
            return int(id) - 1;
        }
        static constexpr SDL_FRect kTitleBtn800[4] = {
            {410.0f, 120.00f, 240.0f, 57.50f},
            {410.0f, 177.50f, 240.0f, 56.25f},
            {410.0f, 233.75f, 240.0f, 56.25f},
            {410.0f, 303.75f, 240.0f, 56.25f}};
        const SDL_FPoint pt{x, y};
        for (int i = 0; i < 4; ++i)
          if (SDL_PointInRectFloat(&pt, &kTitleBtn800[i]))
            return i;
        return -1;
      };
      if (meoSceneMenu) {
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
          if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_BACKSPACE ||
              e.key.key == SDLK_AC_BACK)
            meoSceneMenu = false;
          else if (e.key.key == SDLK_UP && meoSceneFocus > 0)
            --meoSceneFocus;
          else if (e.key.key == SDLK_DOWN &&
                   meoSceneFocus + 1 < meoScenes.size())
            ++meoSceneFocus;
          else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) &&
                   !meoScenes.empty())
            beginMeo(meoScenes[meoSceneFocus]);
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                   e.button.button == SDL_BUTTON_LEFT) {
          const int row = int((e.button.y - 126) / 32);
          if (e.button.x >= 150 && e.button.x < 650 && row >= 0 && row < 12) {
            const size_t index = meoSceneFocus / 12 * 12 + size_t(row);
            if (index < meoScenes.size()) {
              meoSceneFocus = index;
              beginMeo(meoScenes[index]);
            }
          }
        }
        return true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_UP)
          titleFocus = (titleFocus + 3) % 4;
        else if (e.key.key == SDLK_DOWN)
          titleFocus = (titleFocus + 1) % 4;
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
          if (titleFocus == 0)
            beginMeo("A-00");
          else if (titleFocus == 1)
            openSaves(true);
          else if (titleFocus == 2) {
            meoSceneMenu = true;
            meoSceneFocus = 0;
          } else if (titleFocus == 3) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
          }
        }
      } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
        const int hit = hitMeoTitle(e.motion.x, e.motion.y);
        if (hit >= 0)
          titleFocus = hit;
      } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 e.button.button == SDL_BUTTON_LEFT) {
        const int hit = hitMeoTitle(e.button.x, e.button.y);
        if (hit >= 0) {
          titleFocus = hit;
          if (hit == 0)
            beginMeo("A-00");
          else if (hit == 1)
            openSaves(true);
          else if (hit == 2) {
            meoSceneMenu = true;
            meoSceneFocus = 0;
          } else if (hit == 3) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
          }
        }
      }
      return true;
    }
    if (meo && !meoMenu) {
      auto hitMeoOverlay = [this](float x, float y) -> int {
        const int mx = int(x * 640.0f / 800.0f);
        const int my = int(y * 480.0f / 600.0f);
        if (mx < 0 || mx >= 640 || my < 0 || my >= 480)
          return -1;
        if (meoSystemMap.size() >= 8 + 640u * 480u) {
          const uint8_t id = meoSystemMap[8 + size_t(my) * 640u + size_t(mx)];
          if (id >= 1 && id <= 5)
            return int(id) - 1;
        }
        static constexpr SDL_FRect kBtn640[5] = {{452, 29, 28, 26},
                                                 {484, 29, 28, 26},
                                                 {516, 29, 27, 26},
                                                 {547, 29, 27, 26},
                                                 {589, 29, 27, 26}};
        const SDL_FPoint pt{float(mx), float(my)};
        for (int b = 0; b < 5; ++b)
          if (SDL_PointInRectFloat(&pt, &kBtn640[b]))
            return b;
        return -1;
      };
      auto activateMeoOverlay = [this](int button) {
        if (button == 0) {
          meoSpeedIndex = (meoSpeedIndex + 1) % 5;
        } else if (button == 1) {
          meoOverlayOpen = false;
          openSaves(false);
        } else if (button == 2) {
          meoOverlayOpen = false;
          GetVNBacklogRuntime(*engine).open();
        } else if (button == 3) {
          meoOverlayOpen = false;
          engine->openSettings();
        } else if (button == 4) {
          meoOverlayOpen = false;
        }
      };
      if (meoOverlayOpen) {
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
          if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_BACKSPACE ||
              e.key.key == SDLK_AC_BACK) {
            meoOverlayOpen = false;
          } else if (e.key.key == SDLK_LEFT || e.key.key == SDLK_UP) {
            meoOverlayFocus = (meoOverlayFocus + 4) % 5;
          } else if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_DOWN) {
            meoOverlayFocus = (meoOverlayFocus + 1) % 5;
          } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
            activateMeoOverlay(meoOverlayFocus);
          } else if (e.key.key == SDLK_L) {
            meoOverlayOpen = false;
            openSaves(true);
          } else if (e.key.key == SDLK_T) {
            meoOverlayOpen = false;
            openMeoMenu();
          }
          return true;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
          const int hit = hitMeoOverlay(e.motion.x, e.motion.y);
          if (hit >= 0)
            meoOverlayFocus = hit;
          return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          if (e.button.button == SDL_BUTTON_RIGHT) {
            meoOverlayOpen = false;
            return true;
          }
          if (e.button.button == SDL_BUTTON_LEFT) {
            const int hit = hitMeoOverlay(e.button.x, e.button.y);
            if (hit >= 0) {
              meoOverlayFocus = hit;
              activateMeoOverlay(hit);
            } else if (e.button.y > 130.0f) {
              meoOverlayOpen = false;
            }
            return true;
          }
        }
        return true;
      }
      if (!engine->isStoryPaused() && !choice->isActive()) {
        if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
             e.key.key == SDLK_ESCAPE) ||
            (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             e.button.button == SDL_BUTTON_RIGHT)) {
          meoOverlayOpen = true;
          return true;
        }
      }
    }
    if (imouto && imoutoMenu) {
      auto runTitleBtn = [this](int idx) {
        if (idx == 0) {
          beginImouto("init");
        } else if (idx == 1) {
          openSaves(true);
        } else if (idx == 2) {
          engine->openSettings();
        } else if (idx == 3) {
          SDL_Event quit{};
          quit.type = SDL_EVENT_QUIT;
          SDL_PushEvent(&quit);
        }
      };
      const bool hasNativeMenu = assets->contains("SYSDAT/MENU.TEXB");
      auto imoutoBtnRect = [hasNativeMenu](int b) -> SDL_FRect {
        return hasNativeMenu
                   ? SDL_FRect{414.0f, float(350 + b * 48), 372.0f, 39.0f}
                   : SDL_FRect{540.0f, float(340 + b * 52), 200.0f, 40.0f};
      };
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_UP)
          titleFocus = (titleFocus + 3) % 4;
        else if (e.key.key == SDLK_DOWN)
          titleFocus = (titleFocus + 1) % 4;
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)
          runTitleBtn(titleFocus);
      } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
        const SDL_FPoint pt{e.motion.x, e.motion.y};
        for (int b = 0; b < 4; ++b) {
          const SDL_FRect btn = imoutoBtnRect(b);
          if (SDL_PointInRectFloat(&pt, &btn))
            titleFocus = b;
        }
      } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 e.button.button == SDL_BUTTON_LEFT) {
        const SDL_FPoint pt{e.button.x, e.button.y};
        for (int b = 0; b < 4; ++b) {
          const SDL_FRect btn = imoutoBtnRect(b);
          if (SDL_PointInRectFloat(&pt, &btn)) {
            titleFocus = b;
            runTitleBtn(b);
            break;
          }
        }
      }
      return true;
    }
    if (pending == "MESSAGE" && BackAction(*engine, e)) {
      GetInGameMenuRuntime(*engine).open();
      return true;
    }
    if (pending == "MESSAGE" && ShouldOpenBacklog(*engine, e)) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }
    // Controller Back/left shoulder maps to PAGEUP; accept it as history too.
    if (pending == "MESSAGE" && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        e.key.key == SDLK_PAGEUP) {
      GetVNBacklogRuntime(*engine).open();
      return true;
    }
    if (e.type == SDL_EVENT_KEY_UP &&
        (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL))
      skipHeld = false;
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
      skipHeld = false;
    if (engine->isStoryPaused() || choice->isActive())
      return false;
    if (title) {
      refreshTitleChips();
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        const auto chipCount =
            titleChips.empty() ? size_t(1) : titleChips.size();
        if (e.key.key == SDLK_DOWN)
          titleFocus = (titleFocus + 1) % int(chipCount);
        else if (e.key.key == SDLK_UP)
          titleFocus = (titleFocus + int(chipCount) - 1) % int(chipCount);
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)
          titleAction(titleFocus);
      }
      if (e.type == SDL_EVENT_MOUSE_MOTION)
        for (size_t i = 0; i < titleChips.size(); ++i) {
          const auto &chip = titleChips[i];
          const float rx =
              float((titleAbsoluteAlign && chip.hasDrawArea) ? chip.drawX
                                                             : titleBaseX);
          const float ry =
              float((titleAbsoluteAlign && chip.hasDrawArea)
                        ? chip.drawY
                        : (titleBaseY + int(i) * titleChipSpacing));
          SDL_FRect rect{rx, ry, float(chip.atlasW), float(chip.atlasH)};
          SDL_FPoint point{e.motion.x, e.motion.y};
          if (SDL_PointInRectFloat(&point, &rect))
            titleFocus = int(i);
        }
      for (size_t i = 0; i < titleChips.size(); ++i) {
        const auto &chip = titleChips[i];
        const float rx = float(
            (titleAbsoluteAlign && chip.hasDrawArea) ? chip.drawX : titleBaseX);
        const float ry = float((titleAbsoluteAlign && chip.hasDrawArea)
                                   ? chip.drawY
                                   : (titleBaseY + int(i) * titleChipSpacing));
        if (UI::clicked(e, {rx, ry, float(chip.atlasW), float(chip.atlasH)})) {
          titleAction(int(i));
          break;
        }
      }
      return true;
    }
    // Movie input is handled by the outer runtime before stage/UI dispatch.
    // Never let a single click or unrelated event resume either VM.
    if (pending == "OPENING" || pending == "MOVIE")
      return true;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL) {
        skipHeld = true;
        return true;
      }
      if (e.key.key == SDLK_ESCAPE) {
        GetInGameMenuRuntime(*engine).open();
        return true;
      }
      if (e.key.key == SDLK_B) {
        GetVNBacklogRuntime(*engine).open();
        return true;
      }
      if (e.key.key == SDLK_A) {
        autoMode = !autoMode;
        if (autoMode)
          skipMode = false;
        return true;
      }
      if (e.key.key == SDLK_S) {
        skipMode = !skipMode;
        if (skipMode)
          autoMode = false;
        return true;
      }
      if (e.key.key == SDLK_F5) {
        runtime.saveGame((saveDirectory() / "quicksave.vn").string());
        return true;
      }
      if (e.key.key == SDLK_F6) {
        runtime.loadGame((saveDirectory() / "quicksave.vn").string());
        return true;
      }
    }
    if (pending == "MESSAGE" && text->concealed &&
        (UI::clicked(e, {0, 0, 800, 600}) ||
         (e.type == SDL_EVENT_KEY_DOWN &&
          (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)))) {
      text->concealed = false;
      return true;
    }
    if (text->handleEvent(e))
      return true;
    bool advance = UI::clicked(e, {0, 0, 800, 600}) ||
                   (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                    (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE));
    const bool isWaiting = imouto ? imoutoVm.waiting : vm.waiting;
    if (advance && isWaiting &&
        (pending == "MESSAGE" || pending == "WAIT" || pending == "BYTEVIEW" ||
         pending == "WIPE" || pending == "SCROLLCG" || pending == "QUAKE" ||
         (imouto && pending == "MOVIE"))) {
      if (pending == "MESSAGE" && !text->isDialogueComplete())
        text->completeDialogue();
      else {
        if (pending == "MESSAGE")
          finishMessage();
        else if (pending != "WAIT" || waitSkippable) {
          if (pending == "MOVIE" && movie)
            movie->stop();
          if (waitSe)
            se.Stop();
          if (pending == "SCROLLCG") {
            bgOffsetX = scrollTargetX;
            bgOffsetY = scrollTargetY;
            scrollDuration = 0;
            presented = imouto ? composeImoutoScene() : composeScene();
          }
          if (pending == "QUAKE") {
            quakeAxis = 0;
            quakeOffsetX = quakeOffsetY = 0;
          }
          transitionDuration = 0;
          byteOverlay.clear();
          pending.clear();
          if (imouto)
            imoutoVm.resume();
          else
            vm.resume();
        }
      }
      return true;
    }
    return false;
  }
};
SummerRuntime::SummerRuntime(
    std::shared_ptr<Kotonoha::Providers::SummerAssets> a, std::string name,
    std::string appId)
    : Runtime(std::move(name), "0.1.0", std::move(appId)),
      impl(std::make_unique<Impl>(*this, std::move(a))) {}
SummerRuntime::~SummerRuntime() = default;
void SummerRuntime::shutdown() {
  if (impl) {
    if (impl->engine) {
      auto &menu = GetInGameMenuRuntime(*impl->engine);
      menu.close();
      menu.SetPresentation({}, {}, {});
      menu.SetOnSave({});
      menu.SetOnLoad({});
      menu.SetOnReturnToMenu({});
    }
    try {
      impl->saveSystem();
    } catch (const std::exception &e) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Summer persistent data: %s",
                   e.what());
    }
    impl->bgm.Stop();
    impl->se.Stop();
    impl->voice.Stop();
    if (impl->movie)
      impl->movie->stop();
    if (impl->nameForm) {
      impl->nameForm->close();
      RemoveScene(impl->nameForm);
      impl->nameForm = nullptr;
    }
    impl->saves.reset();
    impl->choice.reset();
    impl->text.reset();
    impl->stage.reset();
    impl->canvasTexture.reset();
    impl->assets->clearCache();
  }
}
void SummerRuntime::initMenu(Engine &e) { impl->bind(e); }
void SummerRuntime::initGameplay(Engine &e) { impl->bind(e); }
SDL_FPoint SummerRuntime::sceneSize(const Scene &s) const {
  return s.designSpace() ? SDL_FPoint{1280, 720} : SDL_FPoint{800, 600};
}
void SummerRuntime::update() {
  Runtime::update();
  if (!impl->engine || !impl->loaded)
    return;
  auto &i = *impl;
  if (i.meo) {
    const auto now = SDL_GetTicks();
    if (i.meoLogoActive) {
      i.lastUpdate = now;
      if (now >= i.meoLogoStart && now - i.meoLogoStart >= 4000)
        i.openMeoMenu(true);
      return;
    }
    const bool paused = i.engine->isStoryPaused() || i.meoOverlayOpen;
    i.bgm.SetPaused(paused);
    i.se.SetPaused(paused);
    i.voice.SetPaused(paused);
    if (i.movie)
      i.movie->setPaused(paused);
    if (paused) {
      if (i.lastUpdate && now >= i.lastUpdate)
        i.meoFrameStart += now - i.lastUpdate;
      i.lastUpdate = now;
      return;
    }
    i.lastUpdate = now;
    i.updateMeo(now);
    if (i.movie)
      i.movie->update(i.engine->GetRenderer());
    return;
  }
  const auto now = SDL_GetTicks();
  const bool paused = i.engine->isStoryPaused();
  i.bgm.SetPaused(paused);
  i.se.SetPaused(paused);
  i.voice.SetPaused(paused);
  if (i.movie)
    i.movie->setPaused(paused);
  if (paused) {
    if (i.deadline && i.lastUpdate)
      i.deadline += now - i.lastUpdate;
    if (i.transitionDuration && i.lastUpdate)
      i.transitionStart += now - i.lastUpdate;
    if (i.scrollDuration && i.lastUpdate)
      i.scrollStart += now - i.lastUpdate;
    if (i.bgmFadeDuration && i.lastUpdate)
      i.bgmFadeStart += now - i.lastUpdate;
    i.lastUpdate = now;
    return;
  }
  i.lastUpdate = now;
  if (i.pending == "SCROLLCG" && i.scrollDuration > 0) {
    const float t = std::clamp(
        float(now - i.scrollStart) / float(i.scrollDuration), 0.0f, 1.0f);
    i.bgOffsetX = int(std::lrint(float(i.scrollStartX) +
                                 float(i.scrollTargetX - i.scrollStartX) * t));
    i.bgOffsetY = int(std::lrint(float(i.scrollStartY) +
                                 float(i.scrollTargetY - i.scrollStartY) * t));
    if (now >= i.deadline) {
      i.bgOffsetX = i.scrollTargetX;
      i.bgOffsetY = i.scrollTargetY;
      i.scrollDuration = 0;
      i.presented = i.composeScene();
      i.pending.clear();
      i.vm.resume();
    }
  }
  if (i.pending == "MOVIE" && i.movie && i.movie->finished()) {
    i.movie->stop();
    i.pending.clear();
    i.vm.resume();
  }
  if (i.pending == "OPENING" && i.movie && i.movie->finished()) {
    i.movie->stop();
    i.pending.clear();
    i.title = true;
    i.titleFocus = 0;
    i.background = "GRAPHIC/TITLE/TITLE.CGD";
    i.sprites.clear();
    i.text->hide();
    i.vm.start(i.pendingScript);
    i.vm.run();
  }
  if (i.bgmFadeDuration > 0) {
    float progress =
        std::min(1.0f, float(now - i.bgmFadeStart) / i.bgmFadeDuration);
    i.bgm.SetGain(1 - progress);
    if (progress >= 1) {
      i.bgm.Stop();
      i.music.clear();
      i.bgmFadeDuration = 0;
      if (!i.nextMusic.empty()) {
        i.music = i.nextMusic;
        i.audio(i.bgm, i.music, "BGM", true);
        i.nextMusic.clear();
      }
    }
  }
  if ((i.pending == "WAIT" || i.pending == "BYTEVIEW" || i.pending == "WIPE") &&
      (now >= i.deadline ||
       (i.pending == "WAIT" && i.waitSe && !i.se.Playing()))) {
    if (i.waitSe)
      i.se.Stop();
    i.waitSe = false;
    i.transitionDuration = 0;
    i.byteOverlay.clear();
    i.pending.clear();
    if (i.imouto)
      i.imoutoVm.resume();
    else
      i.vm.resume();
  }
  if (i.imouto && i.pending == "MOVIE" && i.movie && i.movie->finished()) {
    i.movie->stop();
    i.pending.clear();
    i.imoutoVm.resume();
  }
  if (i.imouto && i.pending == "SCROLLCG" && i.scrollDuration > 0) {
    if (now >= i.deadline) {
      i.bgOffsetX = i.scrollTargetX;
      i.bgOffsetY = i.scrollTargetY;
      i.scrollDuration = 0;
      i.presented = i.composeImoutoScene();
      i.pending.clear();
      i.imoutoVm.resume();
    } else {
      const float t = std::clamp(
          float(now - i.scrollStart) / float(i.scrollDuration), 0.0f, 1.0f);
      i.bgOffsetX =
          i.scrollStartX + int((i.scrollTargetX - i.scrollStartX) * t);
      i.bgOffsetY =
          i.scrollStartY + int((i.scrollTargetY - i.scrollStartY) * t);
    }
  }
  if (i.pending == "QUAKE" && i.quakeAxis != 0) {
    if (now >= i.deadline) {
      i.quakeAxis = 0;
      i.quakeOffsetX = i.quakeOffsetY = 0;
      i.pending.clear();
      i.vm.resume();
    } else {
      const uint64_t stepIdx =
          (now >= i.quakeStart ? (now - i.quakeStart) : 0) /
          uint64_t(std::max(1, i.quakeStepMs));
      static constexpr int phaseSign[4] = {-1, 0, 1, 0};
      const int offset = i.quakeAmp * phaseSign[stepIdx & 3u];
      i.quakeOffsetX = (i.quakeAxis == 1) ? offset : 0;
      i.quakeOffsetY = (i.quakeAxis == 2) ? offset : 0;
    }
  }
  if (now - i.lastSystemWrite >= 5000) {
    i.saveSystem();
    i.lastSystemWrite = now;
  }
  if (i.pending == "MESSAGE" && !i.text->concealed &&
      (i.skipHeld || i.skipMode) && i.currentRead) {
    i.text->completeDialogue();
    i.finishMessage();
  }
  if (i.pending == "MESSAGE" && !i.text->concealed && i.autoMode &&
      i.text->isDialogueComplete() && !i.voice.Playing()) {
    if (!i.deadline)
      i.deadline = SDL_GetTicks() + 1500;
    if (SDL_GetTicks() >= i.deadline) {
      i.finishMessage();
    }
  }
  if (i.imouto && i.imoutoVm.finished && !i.imoutoMenu)
    i.openImoutoMenu();
  if (!i.imouto && i.vm.ended)
    i.begin();
}
bool SummerRuntime::onEvent(EventRef e) {
  auto &i = *impl;
  if ((i.pending == "OPENING" || i.pending == "MOVIE") && i.movie &&
      i.movie->isOpen()) {
    if (i.videoSkip.accept(e)) {
      const bool opening = i.pending == "OPENING";
      i.movie->stop();
      i.pending.clear();
      if (opening) {
        i.vm.start(i.pendingScript);
        i.vm.run();
      } else if (i.imouto) {
        i.imoutoVm.resume();
      } else {
        i.vm.resume();
      }
    }
    return true;
  }
  if (i.meo) {
    for (auto *scene : GetActiveScenes())
      if (scene && scene->capturesInput())
        return Runtime::onEvent(e);
    return Runtime::onEvent(MapControllerEvent(e, false));
  }
  // Shared modals (pause, save browser, history, name entry) and choices map
  // controller buttons in their own context. Pre-mapping would turn South
  // into held skip while a menu is open.
  for (auto *scene : GetActiveScenes())
    if (scene && scene->capturesInput()) {
      i.skipHeld = false;
      return Runtime::onEvent(e);
    }
  const bool dialogue = i.text && i.text->isVisible() && !i.text->concealed;
  // The message-box toolbar must see raw directions: while dialogue is open
  // the shared mapping turns Left/Right into text-speed keys, but here they
  // focus the on-screen buttons and Up releases focus back to the novel.
  if (dialogue &&
      (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
       e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
      i.text->handleEvent(e))
    return true;
  return Runtime::onEvent(MapControllerEvent(e, dialogue));
}
void SummerRuntime::saveGame(const std::string &path) {
  auto &i = *impl;
  if (i.meo) {
    if (i.meoMenu || i.meoCurrentScript.empty())
      throw std::runtime_error("Start MEO before saving");
    VNSave save;
    save.nodeId = 0;
    const Uint64 now = SDL_GetTicks();
    const Uint64 elapsed = i.meoElapsedOffset +
                           (now >= i.meoFrameStart ? now - i.meoFrameStart : 0);
    std::ostringstream s;
    s << "MEO-PRESENTATION-1\n"
      << i.meoCurrentScript << "\n"
      << elapsed << "\n"
      << i.meoLocals.size() << "\n";
    for (const auto &[k, v] : i.meoLocals)
      s << std::quoted(k) << ' ' << v << '\n';
    s << i.meoGlobals.size() << "\n";
    for (const auto &[k, v] : i.meoGlobals)
      s << std::quoted(k) << ' ' << v << '\n';
    save.gameData = s.str();
    i.saveSystem();
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path().empty()
            ? "."
            : std::filesystem::path(path).parent_path());
    save.write(path);
    return;
  }
  if (!i.vm.waiting || i.pending == "WAIT" || i.pending == "WIPE" ||
      i.pending == "BYTEVIEW" || i.pending == "MOVIE" ||
      i.pending == "SCROLLCG" || i.title)
    throw std::runtime_error("Save at a dialogue or choice");
  std::ostringstream s;
  s << "SUMMER-PRESENTATION-2\n"
    << std::quoted(i.vm.save()) << '\n'
    << std::quoted(i.background) << ' ' << std::quoted(i.music) << ' '
    << std::quoted(i.pending) << ' ' << std::quoted(i.choiceTarget) << '\n'
    << i.sprites.size() << '\n';
  for (auto &[slot, picture] : i.sprites)
    s << slot << ' ' << std::quoted(picture.id) << ' ' << picture.x << ' '
      << picture.requestedPosition << ' ' << picture.positionSlot << ' '
      << picture.creationOrder << '\n';
  i.saveSystem();
  VNSave save;
  save.nodeId = 0;
  save.gameData = s.str();
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path().empty()
          ? "."
          : std::filesystem::path(path).parent_path());
  save.write(path);
}
void SummerRuntime::loadGame(const std::string &path) {
  auto &i = *impl;
  auto save = VNSave::read(path);
  if (i.meo) {
    std::istringstream input(save.gameData);
    std::string version, script;
    Uint64 elapsed = 0;
    if (!std::getline(input, version) || version != "MEO-PRESENTATION-1" ||
        !std::getline(input, script) || !(input >> elapsed) ||
        elapsed > 24ull * 60 * 60 * 1000)
      throw std::runtime_error("Invalid MEO save");
    std::map<std::string, int64_t> savedLocals;
    std::map<std::string, int64_t> savedGlobals;
    size_t localCount = 0;
    bool hasSavedVars = false;
    if (input >> localCount && localCount <= 4096) {
      hasSavedVars = true;
      for (size_t idx = 0; idx < localCount; ++idx) {
        std::string k;
        int64_t v = 0;
        if (input >> std::quoted(k) >> v)
          savedLocals[k] = v;
      }
      size_t globalCount = 0;
      if (input >> globalCount && globalCount <= 4096) {
        for (size_t idx = 0; idx < globalCount; ++idx) {
          std::string k;
          int64_t v = 0;
          if (input >> std::quoted(k) >> v)
            savedGlobals[k] = v;
        }
      }
    }
    if (hasSavedVars) {
      i.meoLocals = savedLocals;
      for (const auto &[k, v] : savedGlobals) {
        i.meoGlobals[k] = v;
        i.vm.variables["$" + k] = v;
      }
    }
    const auto prevLocals = i.meoLocals;
    const auto prevGlobals = i.meoGlobals;
    i.beginMeo(script);
    if (hasSavedVars) {
      i.meoLocals = prevLocals;
      for (const auto &[k, v] : prevGlobals) {
        i.meoGlobals[k] = v;
        i.vm.variables["$" + k] = v;
      }
      i.meoNextScript.clear();
      i.meoChoices.clear();
      auto it = i.meoScripts.find("SCRIPT/" + i.meoCurrentScript);
      if (it != i.meoScripts.end()) {
        const std::string xml(it->second.begin(), it->second.end());
        const auto codeStart = xml.find("<Code>");
        const auto codeEnd = xml.find("</Code>", codeStart);
        if (codeStart != std::string::npos && codeEnd != std::string::npos) {
          try {
            i.execMeoCode(
                parseXml(xml.substr(codeStart, codeEnd + 7 - codeStart)));
          } catch (...) {
          }
        }
      }
      i.meoLocals = prevLocals;
    }
    const Uint64 now = SDL_GetTicks();
    i.meoFrameStart = now;
    i.meoElapsedOffset = elapsed;
    if (!i.meoVoiceTrack.empty())
      i.audio(i.voice, i.meoVoiceTrack, "Voice", false,
              std::size_t(elapsed * 44100 / 1000));
    SDL_Log("MEO: restored %s at %llu ms", script.c_str(),
            static_cast<unsigned long long>(elapsed));
    i.updateMeo(now);
    return;
  }
  std::istringstream s(save.gameData);
  std::string version, data, background, music, pending, target;
  size_t count;
  if (!(s >> version >> std::quoted(data) >> std::quoted(background) >>
        std::quoted(music) >> std::quoted(pending) >> std::quoted(target) >>
        count) ||
      (version != "SUMMER-PRESENTATION-1" &&
       version != "SUMMER-PRESENTATION-2") ||
      count > 64 || !i.assets->contains(background) ||
      (pending != "MESSAGE" && pending != "SELECT" && pending != "UNIFORM" &&
       pending != "MAP"))
    throw std::runtime_error("Invalid Summer presentation save");
  Machine restored = i.vm;
  restored.restore(data);
  for (auto &[key, value] : i.vm.variables)
    if (!key.empty() && key[0] == '$')
      restored.variables[key] = value;
  std::map<int, Sprite> sprites;
  uint64_t nextOrder = 0;
  while (count--) {
    int slot;
    Sprite sprite;
    if (!(s >> slot >> std::quoted(sprite.id) >> sprite.x) ||
        !i.assets->contains(sprite.id) || std::abs(int64_t(sprite.x)) > 10000)
      throw std::runtime_error("Invalid saved character");
    if (version == "SUMMER-PRESENTATION-2") {
      if (!(s >> sprite.requestedPosition >> sprite.positionSlot >>
            sprite.creationOrder) ||
          sprite.positionSlot >= i.positionOffsets.size() ||
          sprite.creationOrder == UINT64_MAX)
        throw std::runtime_error("Invalid saved character placement");
    } else {
      sprite.positionSlot = sprite.x < 0 ? 1 : sprite.x > 0 ? 2 : 0;
      sprite.requestedPosition = int32_t(sprite.positionSlot);
      sprite.creationOrder = nextOrder + 1;
    }
    nextOrder = std::max(nextOrder, sprite.creationOrder);
    sprites[slot] = sprite;
    auto img = i.assets->picture(sprite.id);
    i.origins[sprite.id] = {img.x, img.y};
  }
  s >> std::ws;
  if (!s.eof())
    throw std::runtime_error("Trailing presentation data");
  i.vm = std::move(restored);
  if (i.movie)
    i.movie->stop();
  i.background = background;
  i.music = music;
  i.sprites = std::move(sprites);
  i.nextSpriteOrder = nextOrder;
  i.title = false;
  i.autoMode = i.skipMode = i.skipHeld = false;
  i.text->controls.reset();
  i.text->concealed = false;
  i.pending = pending;
  i.choiceTarget = target;
  i.deadline = 0;
  i.transitionDuration = 0;
  i.presented = i.composeScene();
  i.canvasKey.clear();
  i.choice->hide();
  i.text->hide();
  i.voice.Stop();
  i.se.Stop();
  i.bgm.Stop();
  if (!music.empty())
    i.audio(i.bgm, music, "BGM", true);
  if (pending == "MESSAGE")
    i.message();
  else if (pending == "SELECT") {
    const auto &node = i.vm.programs()
                           .at(i.vm.position.script)
                           .code.at(i.vm.position.pc - 1)
                           .node;
    std::vector<Value> args;
    for (const auto &part : node.children)
      args.push_back(i.vm.evaluate(part));
    i.function("SELECT", args);
  } else {
    bool uniform = pending == "UNIFORM";
    int mode =
        uniform ? 100 : int(number(i.vm.variables["@KOTONOHA_MAP_MODE"]));
    i.choice->setNative(mode, *i.assets,
                        [&i, uniform](int value, const std::string &) {
                          if (!i.lastCanvas.rgba.empty())
                            i.presented = i.lastCanvas;
                          i.choice->hide();
                          i.pending.clear();
                          if (uniform) {
                            i.vm.variables["@制服番号"] = int64_t(value + 2);
                            i.vm.variables["@EV制服"] = int64_t(value + 24);
                          } else
                            i.vm.variables["@移動場所"] = int64_t(value);
                          i.vm.resume();
                        });
  }
  GetInGameMenuRuntime(*i.engine).close();
  GetVNBacklogRuntime(*i.engine).close();
}
Translation::SourceTexts SummerRuntime::translationSources() const {
  Translation::SourceTexts result;
  if (impl->meo) {
    // The MEO XML dialogue table is not part of the Summer script VM. Index
    // its TextRes entries as original keys, including inactive story scenes.
    auto &texts = result[0];
    std::set<std::string> unique;
    for (const auto &[id, bytes] : impl->meoScripts) {
      const std::string xml(bytes.begin(), bytes.end());
      std::size_t pos = 0;
      while ((pos = xml.find("<TextRes ", pos)) != std::string::npos) {
        const auto start = xml.find('>', pos);
        if (start == std::string::npos)
          break;
        const auto end = xml.find("</TextRes>", start + 1);
        if (end == std::string::npos)
          break;
        const auto value = xml.substr(start + 1, end - start - 1);
        if (!value.empty() && unique.insert(value).second)
          texts.push_back(value);
        pos = end + sizeof("</TextRes>") - 1;
      }
    }
    return result;
  }
  int route = 0;
  for (auto &[name, script] : impl->vm.programs()) {
    for (auto &instruction : script.code)
      if (instruction.node.tag == "RESOURCE")
        for (auto &node : instruction.node.children)
          if (node.attr("type") == "STR")
            result[route].push_back(node.text);
    ++route;
  }
  return result;
}
int SummerRuntime::translationRoute() const { return impl->route(); }
std::vector<std::string> SummerRuntime::activeTranslationKeys() const {
  return impl->activeOriginalKeys;
}
std::vector<DebugAudioAsset> SummerRuntime::debugSceneAudio() {
  if (impl->currentVoiceId.empty())
    return {};
  return {{impl->currentVoiceId, impl->currentRawSpeaker,
           impl->currentRawDialogue}};
}
std::string SummerRuntime::debugNode() const {
  if (impl->meo)
    return impl->meoCurrentScript + ":" + std::to_string(impl->meoFrame);
  if (impl->imouto)
    return impl->imoutoVm.currentScriptName + ":" +
           std::to_string(impl->imoutoVm.pc);
  return impl->vm.position.script + ":" + std::to_string(impl->vm.position.pc);
}
std::string SummerRuntime::nodeClockKey() const {
  if (impl->meo)
    return impl->meoCurrentScript;
  if (impl->imouto)
    return impl->imoutoVm.currentScriptName;
  // All positions within the same script share the node clock; only the
  // event clock resets between PCs of a script.
  return impl->vm.position.script;
}
std::vector<DebugNode> SummerRuntime::debugNodes() const {
  std::vector<DebugNode> nodes;
  if (impl->meo) {
    for (const auto &[id, bytes] : impl->meoScripts) {
      const auto name = std::filesystem::path(id).filename().string();
      std::vector<std::string> references{id};
      if (name == impl->meoCurrentScript) {
        for (const auto &frame : impl->meoFrames)
          for (const auto &child : frame.children)
            for (const auto &object : child.children) {
              auto file = object.attr("FILE");
              if (file.empty())
                continue;
              const auto slash = file.find('/');
              if (slash != std::string::npos)
                file = file.substr(slash + 1);
              const auto logical = "GRAPHIC/" + file;
              if (std::find(references.begin(), references.end(), logical) ==
                  references.end())
                references.push_back(logical);
            }
      }
      nodes.push_back({name, name, std::move(references)});
    }
    return nodes;
  }
  for (auto &[name, s] : impl->vm.programs()) {
    nodes.push_back({name, name, {}});
    for (auto &[label, pc] : s.labels)
      nodes.push_back({name + "::" + label, label, {}});
  }
  return nodes;
}
void SummerRuntime::debugJumpToNode(const std::string &node) {
  if (impl->movie)
    impl->movie->stop();
  if (!impl->engine)
    throw std::runtime_error("Runtime has not started");
  if (impl->meo)
    impl->beginMeo(node);
  else if (impl->imouto)
    impl->beginImouto(node);
  else
    impl->begin(node);
  SetState(GameState::Gameplay);
  for (auto *scene : GetScenes(GameState::Menu)) {
    const auto &gameplay = GetScenes(GameState::Gameplay);
    if (std::find(gameplay.begin(), gameplay.end(), scene) == gameplay.end())
      AddScene(scene, GameState::Gameplay);
  }
}
bool SummerRuntime::isMeoOverlayOpen() const {
  return impl && impl->meo && impl->meoOverlayOpen;
}
} // namespace SummerRadish
