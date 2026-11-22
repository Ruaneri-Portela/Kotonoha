#include <Kirikiri/EmoteRenderer.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace Kirikiri {

namespace {

struct CharacterTheme {
  SDL_Color primary;
  SDL_Color accent;
  const char *displayName;
  const char *nameplate;
};

CharacterTheme themeForCharacter(const std::string &name) {
  if (name == "ショコラ")
    return {{232, 118, 148, 240},
            {92, 52, 46, 255},
            "Chocola",
            "VNX+/cn_name_chocola.png"};
  if (name == "バニラ")
    return {{132, 182, 236, 240},
            {245, 248, 255, 255},
            "Vanilla",
            "VNX+/cn_name_vanilla.png"};
  if (name == "時雨")
    return {{186, 138, 224, 240},
            {72, 48, 96, 255},
            "Shigure",
            "VNX+/cn_name_shigure.png"};
  if (name == "アズキ")
    return {{216, 112, 98, 240},
            {112, 48, 42, 255},
            "Azuki",
            "VNX+/cn_name_azuki.png"};
  if (name == "メイプル")
    return {{238, 176, 96, 240},
            {116, 72, 32, 255},
            "Maple",
            "VNX+/cn_name_maple.png"};
  if (name == "シナモン")
    return {{168, 142, 232, 240},
            {78, 62, 118, 255},
            "Cinnamon",
            "VNX+/cn_name_cinnamon.png"};
  if (name == "ココナツ")
    return {{116, 196, 188, 240},
            {44, 88, 86, 255},
            "Coconut",
            "VNX+/cn_name_coconut.png"};
  return {{210, 150, 175, 240}, {80, 50, 65, 255}, name.c_str(), ""};
}

} // namespace

std::string EmoteRenderer::resolveVisualAsset(
    const Kotonoha::Providers::KirikiriAssets &assets, std::string storage,
    bool foreground) {
  std::replace(storage.begin(), storage.end(), '\\', '/');
  if (storage.empty() || storage.front() == '/' ||
      storage.find("://") != std::string::npos)
    return {};
  for (std::size_t start = 0; start < storage.size();) {
    const auto end = storage.find('/', start);
    if (storage.substr(start, end == std::string::npos ? std::string::npos
                                                       : end - start) == "..")
      return {};
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  const auto slash = storage.find_last_of('/');
  const auto dot = storage.find_last_of('.');
  const bool hasExtension =
      dot != std::string::npos && (slash == std::string::npos || dot > slash);
  const std::vector<std::string> folders =
      foreground
          ? std::vector<std::string>{"",       "VNX+/",  "fgimage/", "evimage/",
                                     "emote/", "image/", "bgimage/", "system/"}
          : std::vector<std::string>{"",         "VNX+/",  "bgimage/",
                                     "evimage/", "image/", "fgimage/",
                                     "system/",  "emote/"};
  for (const auto &folder : folders) {
    const auto base = folder + storage;
    for (const char *extension : {"", ".png", ".jpg", ".jpeg", ".bmp", ".webp",
                                  ".tlg", ".tlg5", ".tlg6", ".pimg", ".psb"}) {
      if (hasExtension && *extension)
        continue;
      const auto candidate = base + extension;
      if (assets.contains(candidate))
        return candidate;
    }
  }
  return {};
}

void EmoteRenderer::renderLayer(Kotonoha::RendererRef renderer,
                                Kotonoha::Engine *engine,
                                Kotonoha::Providers::KirikiriAssets &assets,
                                const ScnLayerState &layer, bool fullScreen) {
  if (layer.file.empty() || layer.showmode == 2 || layer.opacity <= 0.0)
    return;
  if (!failedAssets_.count(layer.file)) {
    const std::string resolved =
        resolveVisualAsset(assets, layer.file, !fullScreen);
    if (!resolved.empty()) {
      try {
        auto &texture = assets.texture(renderer, resolved);
        const double scale =
            100.0 /
            std::max(10.0, 100.0 + std::clamp(layer.zpos, -80.0, 400.0));
        SDL_FRect dst{};
        if (fullScreen) {
          const float w = static_cast<float>(1280.0 * std::max(1.0, scale));
          const float h = static_cast<float>(720.0 * std::max(1.0, scale));
          dst = {static_cast<float>((1280.0 - w) * 0.5 + layer.xpos),
                 static_cast<float>((720.0 - h) * 0.5 + layer.ypos), w, h};
        } else {
          const float w = static_cast<float>(texture.w * scale);
          const float h = static_cast<float>(texture.h * scale);
          dst = {static_cast<float>(640.0 - w * 0.5 + layer.xpos),
                 static_cast<float>(360.0 - h * 0.5 + layer.ypos), w, h};
        }
        const auto alpha =
            static_cast<Uint8>(std::clamp(layer.opacity, 0.0, 255.0));
        SDL_SetTextureAlphaMod(&texture, alpha);
        Kotonoha::RenderUpscaledTexture(&renderer, &texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(&texture, 255);
        return;
      } catch (const std::exception &) {
        failedAssets_.insert(layer.file);
      }
    } else {
      failedAssets_.insert(layer.file);
    }
  }

  // Fallback stage presentation when the background/CG lives inside a
  // PackinOne-encrypted XP3 archive: use VNX+/langselect_cn@base%base.png
  // tinted by time-of-day (昼/夕/夜).
  if (fullScreen) {
    const bool night = layer.file.find("夜") != std::string::npos;
    const bool evening = layer.file.find("夕") != std::string::npos;
    const std::string vnxBg =
        resolveVisualAsset(assets, "VNX+/langselect_cn@base%base.png", false);
    if (!vnxBg.empty()) {
      try {
        auto &tex = assets.texture(renderer, vnxBg);
        const SDL_FRect dst{0.0f, 0.0f, 1280.0f, 720.0f};
        if (night)
          SDL_SetTextureColorMod(&tex, 130, 140, 195);
        else if (evening)
          SDL_SetTextureColorMod(&tex, 245, 190, 165);
        else
          SDL_SetTextureColorMod(&tex, 255, 248, 245);
        Kotonoha::RenderUpscaledTexture(&renderer, &tex, nullptr, &dst);
        SDL_SetTextureColorMod(&tex, 255, 255, 255);
      } catch (...) {
      }
    } else {
      SDL_SetRenderDrawColor(&renderer, night ? 34 : (evening ? 88 : 238),
                             night ? 38 : (evening ? 58 : 224),
                             night ? 64 : (evening ? 68 : 228), 255);
      const SDL_FRect dst{0.0f, 0.0f, 1280.0f, 720.0f};
      SDL_RenderFillRect(&renderer, &dst);
    }
    if (engine) {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 45, 28, 36, 160);
      const SDL_FRect badge{24.0f, 20.0f, 420.0f, 34.0f};
      SDL_RenderFillRect(&renderer, &badge);
      engine->GetVNUI().textSized(renderer, layer.file, 36.0f, 25.0f, 396.0f,
                                  16.0f, {255, 235, 242, 255});
    }
  }
}

void EmoteRenderer::renderCharacter(Kotonoha::RendererRef renderer,
                                    Kotonoha::Engine *engine,
                                    Kotonoha::Providers::KirikiriAssets &assets,
                                    const ScnCharacterState &ch) {
  if (ch.file.empty() || ch.showmode == 2 || ch.opacity <= 0.0)
    return;
  auto getVar = [&](const char *key) -> double {
    const auto it = ch.variables.find(key);
    return it != ch.variables.end() ? it->second : 0.0;
  };
  const double scale =
      100.0 / std::max(10.0, 100.0 + std::clamp(ch.zpos, -80.0, 400.0));
  const double moveX =
      getVar("$move_LR") + getVar("$body_LR") * 0.5 + getVar("$head_LR") * 0.25;
  const double moveY =
      getVar("$move_UD") + getVar("$body_UD") * 0.5 + getVar("$head_UD") * 0.25;

  if (!failedAssets_.count(ch.file)) {
    const std::string resolved = resolveVisualAsset(assets, ch.file, true);
    if (!resolved.empty()) {
      try {
        auto &texture = assets.texture(renderer, resolved);
        const float w = static_cast<float>(texture.w * scale);
        const float h = static_cast<float>(texture.h * scale);
        const SDL_FRect dst{static_cast<float>(640.0 - w * 0.5 + ch.xpos +
                                               ch.offsetx + ch.imagex + moveX),
                            static_cast<float>(360.0 - h * 0.5 + ch.ypos +
                                               ch.offsety + ch.imagey + moveY),
                            w, h};
        const auto alpha =
            static_cast<Uint8>(std::clamp(ch.opacity, 0.0, 255.0));
        SDL_SetTextureAlphaMod(&texture, alpha);
        const double angle = ch.imagerotate + getVar("$body_slant") * 0.3 +
                             getVar("$head_slant") * 0.2;
        if (std::abs(angle) > 0.01) {
          Kotonoha::RenderUpscaledTextureRotated(&renderer, &texture, nullptr,
                                                 &dst, angle, nullptr,
                                                 SDL_FLIP_NONE);
        } else {
          Kotonoha::RenderUpscaledTexture(&renderer, &texture, nullptr, &dst);
        }
        SDL_SetTextureAlphaMod(&texture, 255);
        return;
      } catch (const std::exception &) {
        failedAssets_.insert(ch.file);
      }
    } else {
      failedAssets_.insert(ch.file);
    }
  }

  // Stylized E-mote character standee when emote.xp3 is PackinOne-encrypted:
  // reflects live E-mote position, scale, and motion keyframes.
  const auto theme = themeForCharacter(ch.name);
  const float baseW = static_cast<float>(220.0 * std::clamp(scale, 0.6, 1.5));
  const float baseH = static_cast<float>(410.0 * std::clamp(scale, 0.6, 1.5));
  const float cx = static_cast<float>(640.0 + ch.xpos + ch.offsetx * 0.25 +
                                      ch.imagex + moveX);
  const float cy = static_cast<float>(330.0 + ch.ypos * 0.25 +
                                      ch.offsety * 0.15 + ch.imagey + moveY);
  const SDL_FRect card{cx - baseW * 0.5f, cy - baseH * 0.5f, baseW, baseH};

  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, theme.accent.r, theme.accent.g,
                         theme.accent.b, 190);
  SDL_RenderFillRect(&renderer, &card);

  const SDL_FRect inner{card.x + 6.0f, card.y + 6.0f, card.w - 12.0f,
                        card.h - 12.0f};
  SDL_SetRenderDrawColor(&renderer, theme.primary.r, theme.primary.g,
                         theme.primary.b, 220);
  SDL_RenderFillRect(&renderer, &inner);

  // Cat-ear silhouette accents at top of standee
  const SDL_FRect leftEar{card.x + 18.0f, card.y - 22.0f, 36.0f, 24.0f};
  const SDL_FRect rightEar{card.x + card.w - 54.0f, card.y - 22.0f, 36.0f,
                           24.0f};
  SDL_RenderFillRect(&renderer, &leftEar);
  SDL_RenderFillRect(&renderer, &rightEar);

  // Render character's VNX+ nameplate badge inside the standee
  if (theme.nameplate[0] != '\0') {
    const std::string plateId =
        resolveVisualAsset(assets, theme.nameplate, true);
    if (!plateId.empty()) {
      try {
        auto &plateTex = assets.texture(renderer, plateId);
        const float pw = std::min(card.w - 16.0f, 200.0f);
        const float ph = pw * (41.0f / 380.0f);
        const SDL_FRect pdst{cx - pw * 0.5f, card.y + card.h - ph - 20.0f, pw,
                             ph};
        Kotonoha::RenderUpscaledTexture(&renderer, &plateTex, nullptr, &pdst);
      } catch (...) {
      }
    }
  }
  if (engine) {
    engine->GetVNUI().textSized(renderer, theme.displayName, card.x + 20.0f,
                                card.y + 28.0f, card.w - 40.0f, 22.0f,
                                {255, 250, 252, 255});
  }
}

} // namespace Kirikiri
