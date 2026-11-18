#include "PmailStage.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <Pmail/Port.hpp>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace Kotonoha::PmailPort {
namespace {
// The original 16-color text palette is not yet recovered. These values
// preserve readable contrast over the computer backgrounds until native
// palette entries can be verified.
SDL_Color computerColor(int index) {
  switch (index) {
  case 1:
    return {45, 45, 48, 255};
  case 8:
    return {34, 73, 143, 255};
  case 10:
    return {24, 38, 66, 255};
  default:
    return {35, 43, 59, 255};
  }
}
// Native chat/panel records carry a style byte. Retail traces use 1 for
// system lines, 2 for the player and 11 for other speakers.
SDL_Color computerLineColor(int mode) {
  switch (mode) {
  case 1:
    return computerColor(1);
  case 2:
    return computerColor(8);
  case 11:
    return computerColor(10);
  default:
    return computerColor(0);
  }
}
} // namespace

PmailStage::PmailStage(Kotonoha::Engine &engine, PmailGame &game,
                       Kotonoha::Providers::SnowAssets &assets)
    : Kotonoha::Scene(engine), game_(game), assets_(assets) {}

PmailStage::~PmailStage() = default;

void PmailStage::update() {
  // The stage doesn't really own game logic, it just reflects it.
}

void PmailStage::render(Kotonoha::RendererRef renderer, float scale) {
  // The original composes into an 800x600 framebuffer. Compose animation
  // layers at that resolution, then scale once; scaling every accumulated
  // OSBOOT frame separately makes software renderers progressively slower.
  if (!sceneTexture_) {
    sceneTexture_.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, 800, 600));
    if (!sceneTexture_)
      throw std::runtime_error(SDL_GetError());
    SDL_SetTextureBlendMode(sceneTexture_.get(), SDL_BLENDMODE_NONE);
  }
  auto *previousTarget = SDL_GetRenderTarget(&renderer);
  if (!SDL_SetRenderTarget(&renderer, sceneTexture_.get()))
    throw std::runtime_error(SDL_GetError());
  struct RestoreTarget {
    SDL_Renderer *renderer;
    SDL_Texture *target;
    ~RestoreTarget() { SDL_SetRenderTarget(renderer, target); }
  } restore{&renderer, previousTarget};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderClear(&renderer);
  usedAnimationTextures_.clear();
  // 1. Draw Background
  if (game_.filled()) {
    const uint32_t color = game_.fillColor();
    SDL_SetRenderDrawColor(&renderer, Uint8(color >> 16), Uint8(color >> 8),
                           Uint8(color), 255);
    SDL_RenderClear(&renderer);
  }
  updateBackground(renderer);
  if (backgroundTexture_) {
    const SDL_FRect stage{0, 0, 800, 600};
    SDL_RenderTexture(&renderer, backgroundTexture_.get(), nullptr, &stage);
  }

  renderLips(renderer, game_.background(), 0, 0);

  // 2. Draw Sprites
  renderSprites(renderer);
  renderAnimations(renderer);
  renderComputerUi(renderer);
  if (game_.staffRollActive()) {
    const auto roll = game_.staffRoll();
    if (!roll.vignettes.empty()) {
      const std::size_t vigIdx =
          (static_cast<std::size_t>(roll.elapsedMs) * roll.vignettes.size()) /
          std::max<std::uint32_t>(1, roll.durationMs);
      const std::string &vigName =
          roll.vignettes[std::min(vigIdx, roll.vignettes.size() - 1)];
      const std::string cacheKey = vigName + ".edg";
      auto it = spriteCache_.find(cacheKey);
      if (it == spriteCache_.end()) {
        SpriteTex st;
        try {
          auto surface = assets_.loadSurfaceAsset("graphic/" + cacheKey);
          if (surface) {
            st.tex.reset(
                SDL_CreateTextureFromSurface(&renderer, surface.get()));
            if (st.tex)
              SDL_GetTextureSize(st.tex.get(), &st.w, &st.h);
          }
        } catch (...) {
        }
        it = spriteCache_.emplace(cacheKey, std::move(st)).first;
      }
      if (it->second.tex) {
        const SDL_FRect dst{70.0f, 70.0f, it->second.w, it->second.h};
        SDL_RenderTexture(&renderer, it->second.tex.get(), nullptr, &dst);
      }
    }
    const int rollIdx = std::clamp(int((uint64_t(roll.elapsedMs) * 12) /
                                       std::max<uint32_t>(1, roll.durationMs)),
                                   0, 11);
    char rollFile[32];
    std::snprintf(rollFile, sizeof(rollFile), "roll_%02d.rol", rollIdx);
    auto it = spriteCache_.find(rollFile);
    if (it == spriteCache_.end()) {
      SpriteTex st;
      try {
        auto surface =
            assets_.loadSurfaceAsset("graphic/" + std::string(rollFile));
        if (surface) {
          st.tex.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
          if (st.tex) {
            SDL_GetTextureSize(st.tex.get(), &st.w, &st.h);
            SDL_SetTextureBlendMode(st.tex.get(), SDL_BLENDMODE_BLEND);
          }
        }
      } catch (...) {
      }
      it = spriteCache_.emplace(rollFile, std::move(st)).first;
    }
    if (it->second.tex) {
      const uint32_t segMs = std::max<uint32_t>(1, roll.durationMs / 12);
      const float progress = float(roll.elapsedMs % segMs) / float(segMs);
      const float y = 600.0f - progress * (600.0f + it->second.h);
      const SDL_FRect dst{556.0f, y, it->second.w, it->second.h};
      SDL_RenderTexture(&renderer, it->second.tex.get(), nullptr, &dst);
    }
  }
  if (!SDL_SetRenderTarget(&renderer, previousTarget))
    throw std::runtime_error(SDL_GetError());
  const SDL_FRect stage{0, 0, 800, 600};
  // Apply the engine-selected upscaler to the 800x600 original game raster.
  // VN text and pause/save controls are rendered separately at full output
  // resolution; the original artwork is the part that benefits from scaling.
  if (!Kotonoha::RenderUpscaledTexture(&renderer, sceneTexture_.get(), nullptr,
                                       &stage, SDL_GetTicksNS()))
    throw std::runtime_error(SDL_GetError());
}

void PmailStage::updateBackground(Kotonoha::RendererRef renderer) {
  std::string bgName = game_.background();
  if (bgName.empty()) {
    renderedBackground_.clear();
    backgroundTexture_.reset();
    return;
  }

  if (renderedBackground_ != bgName) {
    backgroundTexture_.reset();

    for (const char *ext : {".CRGB", ".DIB", ".img", ".evd"}) {
      try {
        auto surface = assets_.loadSurfaceAsset("graphic/" + bgName + ext);
        if (surface) {
          backgroundTexture_.reset(
              SDL_CreateTextureFromSurface(&renderer, surface.get()));
          renderedBackground_ = bgName;
          break;
        }
      } catch (...) {
      }
    }
    renderedBackground_ = bgName;
  }
}

void PmailStage::renderSprites(Kotonoha::RendererRef renderer) {
  for (const auto &sp : game_.sprites()) {
    if (sp.name.empty())
      continue;

    auto it = spriteCache_.find(sp.name);
    if (it == spriteCache_.end()) {
      // Load sprite. We'll try common extensions.
      SpriteTex st;
      const char *exts[] = {".CHAR", ".chd", ".img", ".evd", ".CRGB"};
      for (const char *ext : exts) {
        std::string fullPath = sp.name + ext;
        try {
          auto surface = assets_.loadSurfaceAsset("graphic/" + fullPath);
          if (surface) {
            st.tex.reset(
                SDL_CreateTextureFromSurface(&renderer, surface.get()));
            if (st.tex) {
              SDL_GetTextureSize(st.tex.get(), &st.w, &st.h);
              break;
            }
          }
        } catch (...) {
        }
      }
      it = spriteCache_.emplace(sp.name, std::move(st)).first;
    }

    if (it->second.tex) {
      const auto placement = assets_.Sprite(sp.name);
      SDL_FRect dst{float(sp.x) + (placement.valid ? placement.x : 0.0f),
                    float(sp.y) + (placement.valid ? placement.y : 0.0f),
                    it->second.w, it->second.h};
      // Apply tint
      SDL_SetTextureColorMod(
          it->second.tex.get(),
          Uint8(std::clamp(sp.tint[0], 0, 0x100) * 255 / 0x100),
          Uint8(std::clamp(sp.tint[1], 0, 0x100) * 255 / 0x100),
          Uint8(std::clamp(sp.tint[2], 0, 0x100) * 255 / 0x100));
      SDL_SetTextureBlendMode(it->second.tex.get(), SDL_BLENDMODE_BLEND);
      SDL_RenderTexture(&renderer, it->second.tex.get(), nullptr, &dst);
      renderLips(renderer, sp.name, float(sp.x), float(sp.y), sp.tint);
    }
  }
}

void PmailStage::renderAnimationFrame(Kotonoha::RendererRef renderer,
                                      const std::string &name, uint32_t frame,
                                      float x, float y, const int *tint) {
  const std::string id = "graphic/" + name + ".anim#" + std::to_string(frame);
  usedAnimationTextures_.insert(id);
  auto found = animationCache_.find(id);
  if (found == animationCache_.end()) {
    const auto surface = assets_.loadSurfaceAsset(id);
    SpriteTex texture;
    texture.tex.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
    if (!texture.tex)
      throw std::runtime_error(std::string("Cannot create ANIM texture: ") +
                               SDL_GetError());
    SDL_GetTextureSize(texture.tex.get(), &texture.w, &texture.h);
    SDL_SetTextureBlendMode(texture.tex.get(), SDL_BLENDMODE_BLEND);
    found = animationCache_.emplace(id, std::move(texture)).first;
  }
  const auto &placement = assets_.AnimationOf(name).frames.at(frame);
  const SDL_FRect dst{x + placement.x, y + placement.y, placement.w,
                      placement.h};
  SDL_SetTextureColorMod(
      found->second.tex.get(),
      tint ? Uint8(std::clamp(tint[0], 0, 256) * 255 / 256) : 255,
      tint ? Uint8(std::clamp(tint[1], 0, 256) * 255 / 256) : 255,
      tint ? Uint8(std::clamp(tint[2], 0, 256) * 255 / 256) : 255);
  SDL_RenderTexture(&renderer, found->second.tex.get(), nullptr, &dst);
}

void PmailStage::renderLips(Kotonoha::RendererRef renderer,
                            const std::string &name, float x, float y,
                            const int *tint) {
  if (name.empty() || !game_.voicePlaying || !game_.voicePlaying())
    return;
  const auto &animation = assets_.AnimationOf(name);
  const int frame = game_.voiceMouthFrame ? game_.voiceMouthFrame() : 0;
  for (const auto &region : animation.regions) {
    if (region.actor != uint32_t(game_.voiceActor()) || !region.count ||
        region.first >= animation.frames.size() ||
        region.count > animation.frames.size() - region.first)
      continue;
    // The audio pattern has three phases but native artwork may expose only
    // one or two variants for this actor. Wrap safely within each region.
    for (size_t i = 0; i < region.count; ++i)
      usedAnimationTextures_.insert("graphic/" + name + ".anim#" +
                                    std::to_string(region.first + i));
    renderAnimationFrame(
        renderer, name,
        uint32_t(MouthRegionFrame(region.first, region.count, frame)), x, y,
        tint);
  }
}

void PmailStage::renderComputerUi(Kotonoha::RendererRef renderer) {
  if (!game_.hasComputerOverlay())
    return;
  // All positions are in native 800x600 scene units. The original game draws
  // each computer command into its back buffer immediately; this renderer
  // retains the equivalent VM streams and composites them over the background
  // each frame. Native order at 0x425c6b is panel, clock slots, then SYS 52
  // text slots. SYS 160/162 append and redraw the chat transcript before that
  // composite, so the transcript stays visible while dialogue waits for a
  // click (retail traces interleave sys 1/5 with sys 160).
  auto &ui = engine.GetVNUI();

  // 1. Chat transcript (0x407ad0 / 0x407f10). The original keeps the newest
  // 15 lines and resolves each line through its owning script object.
  const auto history = game_.chatHistory();
  float historyY = 174;
  for (const auto &line : history) {
    if (historyY >= 570)
      break;
    const float consumed = ui.textSized(renderer, line.text, 85, historyY, 624,
                                        17, computerLineColor(line.mode));
    historyY += std::clamp(consumed, 22.0f, 54.0f);
  }

  // 2. Native panels. 0x423710 (SYS 179) copies seven integers into
  // 0xf8a77a..0xf8a792 and calls 0x417a50: x, y, width, height, right
  // margin, body indent and header flag.
  const auto chat = game_.chatPanel();
  const auto mail = game_.mailPanel();
  const bool chatConfigured =
      chat.active && chat.params[2] > 0 && chat.params[3] > 0;
  auto drawPanel = [&](const PmailGame::ComputerPanel &panel, float x, float y,
                       float width, float height) {
    if (!panel.active || panel.entries.empty())
      return;
    const bool configured = panel.params[2] > 0 && panel.params[3] > 0;
    const float margin = configured ? float(std::max(panel.params[4], 0)) : 0;
    const float indent = configured ? float(std::max(panel.params[5], 0)) : 0;
    const bool drawHeader = configured && panel.params[6] != 0;
    float lineY = y;
    for (size_t i = 0; i < panel.entries.size(); ++i) {
      if (lineY >= y + height)
        break;
      const auto &line = panel.entries[i];
      // 0x417e00 lays out the first three entries with a 5px right inset
      // when the header flag is set; the body is indented and wrapped inside
      // the authored right margin.
      const bool header = drawHeader && i < 3;
      const float lineX = x + (header ? 0.0f : indent);
      const float wrap =
          std::max(16.0f, width - (header ? 0.0f : indent + margin));
      const float consumed =
          ui.textSized(renderer, line.text, lineX, lineY, wrap, 16,
                       computerLineColor(line.mode));
      lineY += std::clamp(consumed, 18.0f, 48.0f);
    }
  };
  // On original layouts the background provides the panel artwork; do not
  // invent a new mail/chat window frame or substitute unrelated sprites.
  if (chatConfigured) {
    drawPanel(chat, float(chat.params[0]), float(chat.params[1]),
              float(chat.params[2]), float(chat.params[3]));
    if (mail.active)
      drawPanel(mail, 85, 202, 288, 363);
  } else if (chat.active && mail.active) {
    drawPanel(chat, 85, 202, 288, 363);
    drawPanel(mail, 422, 202, 292, 363);
  } else if (mail.active && !mail.entries.empty()) {
    // pmail.exe:0x411390 (SYS 192..195) renders centered monologue/poem lines
    // at x = (800 - 512) / 2 = 144, vertically centered with 48px line stride.
    if (mail.selection != 0) {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 96);
      const SDL_FRect dim{0, 0, 800, 600};
      SDL_RenderFillRect(&renderer, &dim);
    }
    const float totalH = float(mail.entries.size()) * 48.0f;
    float lineY = std::max(40.0f, (600.0f - totalH) * 0.5f);
    for (const auto &line : mail.entries) {
      if (lineY >= 560.0f)
        break;
      ui.textSized(renderer, line.text, 144.0f, lineY, 512.0f, 18.0f,
                   computerLineColor(line.mode));
      lineY += 48.0f;
    }
  } else {
    drawPanel(chat, 85, 202, 620, 363);
  }

  // 3. Clock (0x4136b0). Native glyph slots sit on the bottom bar at
  // y=0x249 (585): 692, 704, 718, 735, 747 and 762. Digits and the colon are
  // 11x10; the trailing AM/PM mark is a 24x10 combined glyph.
  const auto clock = game_.computerClock();
  if (!clock.empty()) {
    static constexpr float clockX[6] = {692, 704, 718, 735, 747, 762};
    for (size_t i = 0; i < 6 && i < clock.size(); ++i) {
      const char ch = clock[i];
      if (ch == ' ')
        continue;
      std::string glyph(1, ch);
      if (i == 5 && (ch == 'A' || ch == 'P'))
        glyph += 'M';
      ui.textSized(renderer, glyph, clockX[i], 585, i == 5 ? 24.0f : 12.0f, 10,
                   computerColor(1));
    }
  }

  // 4. SYS 52 positioned text slots (0x423d50 / 0x42b230). Each slot owns an
  // authored box; the native renderer wraps to width and clips to height.
  for (const auto &slot : game_.computerText()) {
    if (slot.width <= 0 || slot.height <= 0 || slot.text.empty())
      continue;
    const int clipX = std::clamp(slot.x, 0, 800);
    const int clipY = std::clamp(slot.y, 0, 600);
    const int clipW = std::clamp(slot.width, 0, 800 - clipX);
    const int clipH = std::clamp(slot.height, 0, 600 - clipY);
    if (!clipW || !clipH)
      continue;
    const SDL_Rect clip{clipX, clipY, clipW, clipH};
    SDL_SetRenderClipRect(&renderer, &clip);
    ui.textSized(renderer, slot.text, float(slot.x), float(slot.y),
                 float(std::min(slot.width, 800)),
                 float(std::clamp(slot.size, 8, 48)),
                 computerColor(slot.color));
    SDL_SetRenderClipRect(&renderer, nullptr);
  }
}

void PmailStage::renderAnimations(Kotonoha::RendererRef renderer) {
  for (const auto &animation : game_.animations()) {
    const auto &preset = AnimationPresets[animation.preset];
    // Restore-mode frames redraw over the saved scene. OSBOOT accumulates
    // frames. Native 0x4075e8 retains the final image in the scene buffer.
    const uint32_t first = preset.restore ? animation.frame : 0;
    for (uint32_t frame = first; frame <= animation.frame; ++frame)
      renderAnimationFrame(renderer, preset.name, frame, float(animation.x),
                           float(animation.y));
  }
  for (auto it = animationCache_.begin(); it != animationCache_.end();) {
    if (!usedAnimationTextures_.count(it->first)) {
      it = animationCache_.erase(it);
    } else
      ++it;
  }
}

} // namespace Kotonoha::PmailPort
