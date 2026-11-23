#pragma once
#include <Kotonoha/components/ImageButtonBar.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SummerRadish/Map.hpp>
#include <SummerRadish/SummerAssets.hpp>
#include <optional>

namespace SummerRadish {
// MESSAGE.INI [BackLog], with the original atlas and pixel hit map.
// Some installations (e.g. a second retail disc) ship a different asset set
// that omits the FRAME/ directory entirely; those optional assets are loaded
// only when present.
class SummerBacklog final : public Kotonoha::VNBacklog {
  Kotonoha::Providers::SummerAssets &assets;
  ClickMap map;
  bool hasCmap = false;
  Kotonoha::ImageButtonBar controls;
  std::vector<SDL_FRect> rows;
  std::size_t selected = 0;
  void move(int delta) {
    if (entries.empty())
      return;
    selected = std::size_t(
        std::clamp(int(selected) + delta, 0, int(entries.size()) - 1));
    scrollToBottom = false;
    if (selected < rows.size())
      targetScroll = std::clamp(rows[selected].y + rows[selected].h / 2 - 300,
                                0.0f, maxScroll);
  }
  SDL_FRect closeButtonArea() const override {
    return hasCmap ? SDL_FRect{} : SDL_FRect{660, 12, 124, 34};
  }
  float scrollInputHeight() const override { return 600; }
  void renderFrame(Kotonoha::RendererRef r) override {
    engine.GetUI().backdrop(r, {0, 0, 64, 160});
    if (!hasCmap)
      engine.GetUI().button(r, closeButtonArea(), "Close [ESC]");
  }
  void renderEntries(Kotonoha::RendererRef r) override {
    auto &ui = engine.GetVNUI();
    rows.clear();
    float y = 8;
    for (const auto &entry : entries) {
      const float height = ui.textSize(entry.speaker, 712).y +
                           ui.textSize(entry.text, 712).y + 16;
      rows.push_back({16, y, 712, height});
      y += height;
    }
    maxScroll = std::max(0.0f, y - 600);
    if (scrollToBottom) {
      scroll = targetScroll = maxScroll;
      scrollToBottom = false;
    }
    scroll = std::clamp(scroll, 0.0f, maxScroll);
    targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
    SDL_Rect previous{};
    const bool clipped = SDL_RenderClipEnabled(&r);
    SDL_GetRenderClipRect(&r, &previous);
    SDL_Rect clip{0, 0, 744, 600};
    SDL_SetRenderClipRect(&r, &clip);
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto &row = rows[i];
      if (row.y + row.h < scroll || row.y - scroll > 600)
        continue;
      const auto &entry = entries[i];
      float top = row.y - scroll;
      top += ui.text(r, entry.speaker, row.x, top, row.w,
                     engine.GetSettings().vnTextColorOverride
                         ? engine.GetSettings().vnSpeakerColor
                         : SDL_Color{240, 240, 245, 255});
      ui.text(r, entry.text, row.x, top, row.w,
              engine.GetSettings().vnTextColorOverride
                  ? engine.GetSettings().vnTextColor
                  : SDL_Color{240, 240, 245, 255});
    }
    SDL_SetRenderClipRect(&r, clipped ? &previous : nullptr);
    const std::string backlogTex =
        assets.containsAsset("GRAPHIC/MESFRAME/BACKLOG.TEXB")
            ? "GRAPHIC/MESFRAME/BACKLOG.TEXB"
            : "GRAPHIC/FRAME/BACKLOG.TEXB";
    if (assets.containsAsset(backlogTex)) {
      auto &atlas = assets.texture(r, backlogTex);
      if (assets.isLostM()) {
        SDL_FRect source{0, 0, 54, 559}, target{746, 37, 54, 559};
        Kotonoha::RenderUpscaledTexture(&r, &atlas, &source, &target);
        constexpr SDL_FRect sources[] = {{54, 0, 33, 60},   {54, 60, 33, 60},
                                         {54, 138, 33, 38}, {54, 179, 33, 60},
                                         {54, 257, 33, 60}, {54, 341, 33, 38}};
        for (int i = 0; i < 6; ++i)
          if (controls.state(i))
            Kotonoha::RenderUpscaledTexture(&r, &atlas, &sources[i],
                                            &controls.buttons[i].area);
      } else {
        SDL_FRect source{0, 0, 56, 600}, target{744, 0, 56, 600};
        Kotonoha::RenderUpscaledTexture(&r, &atlas, &source, &target);
        constexpr SDL_FRect sources[] = {{56, 0, 36, 32},   {56, 32, 36, 22},
                                         {56, 54, 38, 38},  {56, 92, 36, 22},
                                         {56, 114, 36, 32}, {56, 146, 32, 32}};
        for (int i = 0; i < 6; ++i)
          if (controls.state(i))
            Kotonoha::RenderUpscaledTexture(&r, &atlas, &sources[i],
                                            &controls.buttons[i].area);
      }
    }
  }

public:
  Kotonoha::PcmPlayer replay;
  SummerBacklog(Kotonoha::Engine &e, Kotonoha::Providers::SummerAssets &a)
      : VNBacklog(e), assets(a), map() {
    if (auto cmap = assets.tryReadAsset("GRAPHIC/MESFRAME/BACKLOG.CMAP"))
      map = ClickMap(std::move(*cmap)), hasCmap = true;
    else if (auto cmap1 = assets.tryReadAsset("GRAPHIC/FRAME/BACKLOG.CMAP"))
      map = ClickMap(std::move(*cmap1)), hasCmap = true;
    controls.axis = Kotonoha::ImageButtonBar::NavigationAxis::Vertical;
    const bool lostM = assets.isLostM();
    controls.hitTest = [this, lostM](SDL_FPoint p) {
      const int id = lostM ? map.value(int(p.x) - 746, int(p.y) - 37)
                           : map.value(int(p.x) - 744, int(p.y));
      return id >= 1 && id <= 6 ? id - 1 : -1;
    };
    if (lostM) {
      controls.buttons = {{{751, 59, 33, 60}, [this] { move(-3); }},
                          {{751, 149, 33, 60}, [this] { move(-1); }},
                          {{751, 281, 33, 38}, [this] { replayEntry(selected); }},
                          {{751, 359, 33, 60}, [this] { move(1); }},
                          {{751, 449, 33, 60}, [this] { move(3); }},
                          {{751, 531, 33, 38}, [this] { close(); }}};
    } else {
      controls.buttons = {{{757, 73, 36, 32}, [this] { move(-3); }},
                          {{757, 155, 36, 22}, [this] { move(-1); }},
                          {{757, 250, 38, 38}, [this] { replayEntry(selected); }},
                          {{757, 360, 36, 22}, [this] { move(1); }},
                          {{757, 431, 36, 32}, [this] { move(3); }},
                          {{759, 544, 32, 32}, [this] { close(); }}};
    }
  }
  bool designSpace() const override { return false; }
  std::size_t selectedEntry() const { return selected; }
  void open() override {
    VNBacklog::open();
    selected = entries.empty() ? 0 : entries.size() - 1;
    controls.reset();
  }
  void close() override {
    replay.Stop();
    controls.reset();
    VNBacklog::close();
  }
  void clear() override {
    replay.Stop();
    rows.clear();
    selected = 0;
    VNBacklog::clear();
  }
  bool onEvent(Kotonoha::EventRef event) override {
    if (!isVisible())
      return false;
    if (controls.handle(event))
      return true;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event.button.button == SDL_BUTTON_LEFT && event.button.x < 744) {
      const SDL_FPoint point{event.button.x, event.button.y + scroll};
      for (std::size_t i = 0; i < rows.size(); ++i)
        if (SDL_PointInRectFloat(&point, &rows[i])) {
          selected = i;
          replayEntry(i);
          return true;
        }
    }
    return VNBacklog::onEvent(event);
  }
};
} // namespace SummerRadish
