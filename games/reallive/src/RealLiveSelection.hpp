#pragma once
#include "RealLiveAssets.hpp"
#include <RealLive/Images.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/ImageButtonBar.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <iomanip>
#include <sstream>

namespace RealLive {
// RealLive selections share the configured text window, not a separate panel.
class RealLiveSelection final : public Kotonoha::ChoiceQuestionScene {
  RealLiveAssets &assets;
  Kotonoha::ImageButtonBar controls;
  Kotonoha::SurfacePtr backing;
  Kotonoha::TexturePtr backingTexture;
  std::string frameID;
  SDL_FRect window{}, content{};
  float fontSize = 25, lineHeight = 28;
  SDL_Color textColor{255, 255, 255, 255};
  bool keyboardFocus = false;
  static std::string numbered(const std::string &prefix, int id) {
    std::ostringstream out;
    out << prefix << std::setw(3) << std::setfill('0') << id;
    return out.str();
  }
  SDL_FRect listArea() const override { return content; }
  float scrollInputHeight() const override { return 480; }
  void layout() override {
    controls.buttons.clear();
    float y = content.y - scroll;
    for (std::size_t i = 0; i < options.size(); ++i) {
      const auto size =
          engine.GetVNUI().measureSized(options[i].label, content.w, fontSize);
      const float height = std::max(lineHeight, size.y);
      options[i].hitBox = {content.x, y,
                           std::min(content.w, std::max(1.0f, size.x)), height};
      controls.buttons.push_back({options[i].hitBox, [this, i] { select(i); }});
      y += height;
    }
  }
  void renderPanel(Kotonoha::RendererRef r) override {
    if (backing) {
      if (!backingTexture) {
        backingTexture.reset(SDL_CreateTextureFromSurface(&r, backing.get()));
        if (!backingTexture)
          throw std::runtime_error(SDL_GetError());
      }
      SDL_FRect target{window.x, window.y, float(backing->w),
                       float(backing->h)};
      Kotonoha::RenderUpscaledTexture(&r, backingTexture.get(), nullptr,
                                      &target);
    }
    auto &frame = assets.texture(r, frameID);
    Kotonoha::RenderUpscaledTexture(&r, &frame, nullptr, &window);
  }
  void renderOption(Kotonoha::RendererRef r, std::size_t index, bool) override {
    const auto &option = options[index];
    const bool focused =
        controls.hovered == int(index) || (keyboardFocus && selected == index);
    const auto color = engine.GetSettings().vnTextColorOverride
                           ? engine.GetSettings().vnTextColor
                           : textColor;
    if (focused) {
      SDL_SetRenderDrawBlendMode(&r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&r, color.r, color.g, color.b, 255);
      SDL_RenderFillRect(&r, &option.hitBox);
    } else {
      engine.GetVNUI().textSized(r, option.label, option.hitBox.x + 1,
                                 option.hitBox.y + 1, content.w, fontSize,
                                 {0, 0, 0, 255});
    }
    engine.GetVNUI().textSized(r, option.label, option.hitBox.x,
                               option.hitBox.y, content.w, fontSize,
                               focused ? SDL_Color{0, 0, 0, 255} : color);
  }

public:
  explicit RealLiveSelection(Kotonoha::Engine &e, RealLiveAssets &a)
      : ChoiceQuestionScene(e, "", {}, nullptr), assets(a) {}
  bool designSpace() const override { return false; }
  void configure(int number) {
    const auto &config = assets.settings();
    const auto key = numbered("WINDOW.", number) + ".";
    fontSize = std::max(1, config.integer(key + "MOJI_SIZE", 25));
    lineHeight =
        std::max(1.0f, fontSize + config.integer(key + "MOJI_REP", 3, 1) +
                           config.integer(key + "LUBY_SIZE"));
    const float width =
        std::max(1, config.integer(key + "MOJI_CNT", 22)) *
        std::max(1.0f, fontSize + config.integer(key + "MOJI_REP", -1));
    const float height =
        std::max(1, config.integer(key + "MOJI_CNT", 3, 1)) * lineHeight;
    const auto waku =
        numbered("WAKU.", config.integer(key + "WAKU_SETNO")) + ".000.";
    frameID = assets.graphic(config.text(waku + "NAME", "smw00a"));
    auto frame = assets.loadSurface(frameID);
    window = {0, 0, float(frame->w), float(frame->h)};
    const int origin = config.integer(key + "POS");
    window.x = config.integer(key + "POS", 0, 1);
    window.y = config.integer(key + "POS", 345, 2);
    if (origin & 1)
      window.x = 640 - window.w - window.x;
    if (origin & 2)
      window.y = 480 - window.h - window.y;
    window.x = std::clamp(window.x, 0.0f, std::max(0.0f, 640 - window.w));
    window.y = std::clamp(window.y, 0.0f, std::max(0.0f, 480 - window.h));
    content = {window.x + config.integer(key + "MOJI_POS", 53, 2),
               window.y + config.integer(key + "MOJI_POS", 19), width, height};
    textColor = {Uint8(config.integer("COLOR_TABLE.000", 255)),
                 Uint8(config.integer("COLOR_TABLE.000", 255, 1)),
                 Uint8(config.integer("COLOR_TABLE.000", 255, 2)), 255};
    backing.reset();
    backingTexture.reset();
    const auto back = config.text(waku + "BACK");
    if (!back.empty()) {
      backing = assets.loadSurface(assets.graphic(back));
      const auto attr =
          config.integer(key + "ATTR_MOD") ? key + "ATTR" : "WINDOW_ATTR";
      // The original BACK image supplies coverage; WINDOW_ATTR supplies color.
      for (int y = 0; y < backing->h; ++y)
        for (int x = 0; x < backing->w; ++x) {
          auto *p = static_cast<Uint8 *>(backing->pixels) + y * backing->pitch +
                    x * 4;
          for (int c = 0; c < 3; ++c)
            p[c] = Uint8(config.integer(attr, c == 2 ? 160 : 100, c));
          p[3] = Uint8(int(p[3]) * config.integer(attr, 200, 3) / 255);
        }
    }
    controls.reset();
    keyboardFocus = false;
  }
  bool onEvent(Kotonoha::EventRef event) override {
    if (!isActive())
      return false;
    layout();
    if (event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      keyboardFocus = false;
      if (controls.handle(event))
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN ||
        event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
      keyboardFocus = true;
    return ChoiceQuestionScene::onEvent(event);
  }
};
} // namespace RealLive
