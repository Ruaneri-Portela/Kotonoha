// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterRuntime.hpp>
#include <Ponscripter/PixelEffects.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNManager.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/VideoSkipGesture.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ponscripter {
namespace {

struct ProcessedImage {
  Kotonoha::SurfacePtr surface;
  Kotonoha::TexturePtr texture;
  int width = 0;
  int height = 0;
  bool hasNativeAlpha = false;
};

Kotonoha::SurfacePtr buildTransSurface(SDL_Surface *rawSurf, TransMode mode,
                                       int numCells, bool &outHadNativeAlpha) {
  if (!rawSurf || rawSurf->w <= 0 || rawSurf->h <= 0)
    return nullptr;

  const SDL_PixelFormatDetails *rawDetails =
      SDL_GetPixelFormatDetails(rawSurf->format);
  outHadNativeAlpha = (rawDetails && rawDetails->Amask != 0);

  Kotonoha::SurfacePtr rgba(
      SDL_ConvertSurface(rawSurf, SDL_PIXELFORMAT_RGBA32));
  if (!rgba)
    return nullptr;

  const int w = rgba->w;
  const int h = rgba->h;
  const int cells = std::max(1, numCells);

  if (mode == TransMode::Copy) {
    SDL_LockSurface(rgba.get());
    for (int y = 0; y < h; ++y) {
      auto *row = reinterpret_cast<std::uint8_t *>(rgba->pixels) +
                  static_cast<std::size_t>(y) * rgba->pitch;
      for (int x = 0; x < w; ++x)
        row[x * 4 + 3] = 255;
    }
    SDL_UnlockSurface(rgba.get());
    return rgba;
  }

  if (mode == TransMode::Alpha && !outHadNativeAlpha && w >= cells * 2) {
    const int fullCellW = w / cells;
    const int halfCellW = fullCellW / 2;
    const int outW = halfCellW * cells;
    Kotonoha::SurfacePtr split(
        SDL_CreateSurface(outW, h, SDL_PIXELFORMAT_RGBA32));
    if (!split)
      return rgba;

    SDL_LockSurface(rgba.get());
    SDL_LockSurface(split.get());
    for (int y = 0; y < h; ++y) {
      const auto *srcRow =
          reinterpret_cast<const std::uint8_t *>(rgba->pixels) +
          static_cast<std::size_t>(y) * rgba->pitch;
      auto *dstRow = reinterpret_cast<std::uint8_t *>(split->pixels) +
                     static_cast<std::size_t>(y) * split->pitch;
      for (int c = 0; c < cells; ++c) {
        const int srcBaseX = c * fullCellW;
        const int dstBaseX = c * halfCellW;
        for (int x = 0; x < halfCellW; ++x) {
          const int rgbX = srcBaseX + x;
          const int maskX = srcBaseX + halfCellW + x;
          dstRow[(dstBaseX + x) * 4 + 0] = srcRow[rgbX * 4 + 0];
          dstRow[(dstBaseX + x) * 4 + 1] = srcRow[rgbX * 4 + 1];
          dstRow[(dstBaseX + x) * 4 + 2] = srcRow[rgbX * 4 + 2];
          const std::uint8_t maskLum = srcRow[maskX * 4 + 0];
          dstRow[(dstBaseX + x) * 4 + 3] =
              static_cast<std::uint8_t>(255u - maskLum);
        }
      }
    }
    SDL_UnlockSurface(split.get());
    SDL_UnlockSurface(rgba.get());
    return split;
  }

  if ((mode == TransMode::TopLeft || mode == TransMode::TopRight) &&
      !outHadNativeAlpha) {
    SDL_LockSurface(rgba.get());
    const auto *topRow = reinterpret_cast<const std::uint8_t *>(rgba->pixels);
    const int keyX = (mode == TransMode::TopLeft) ? 0 : (w - 1);
    const std::uint8_t kr = topRow[keyX * 4 + 0];
    const std::uint8_t kg = topRow[keyX * 4 + 1];
    const std::uint8_t kb = topRow[keyX * 4 + 2];
    for (int y = 0; y < h; ++y) {
      auto *row = reinterpret_cast<std::uint8_t *>(rgba->pixels) +
                  static_cast<std::size_t>(y) * rgba->pitch;
      for (int x = 0; x < w; ++x) {
        if (row[x * 4 + 0] == kr && row[x * 4 + 1] == kg &&
            row[x * 4 + 2] == kb) {
          row[x * 4 + 3] = 0;
        }
      }
    }
    SDL_UnlockSurface(rgba.get());
  }

  return rgba;
}

SDL_Color rgb24ToSdl(std::uint32_t rgb, std::uint8_t alpha = 255) {
  return SDL_Color{static_cast<std::uint8_t>((rgb >> 16) & 0xFFu),
                   static_cast<std::uint8_t>((rgb >> 8) & 0xFFu),
                   static_cast<std::uint8_t>(rgb & 0xFFu), alpha};
}

// Infers character name from Saya no Uta voice clip suffix (00xxxx00..06.ogg)
std::string inferCharacterFromVoicePath(std::string_view voicePath) {
  if (voicePath.size() < 6)
    return "Voice";
  const std::size_t dot = voicePath.rfind('.');
  if (dot == std::string_view::npos || dot < 2)
    return "Voice";
  const std::string_view suffix = voicePath.substr(dot - 2, 2);
  if (suffix == "00")
    return "Sakisaka Fuminori";
  if (suffix == "01")
    return "Saya";
  if (suffix == "02")
    return "Tonoh Kouji";
  if (suffix == "04")
    return "Takahata Oumi";
  if (suffix == "05")
    return "Tsukuba Yoh";
  if (suffix == "06")
    return "Tanbo Ryouko";
  if (suffix == "03")
    return "Suzumi Yousuke";
  return "Others";
}

} // namespace

class PonscripterMenu final : public Kotonoha::InGameMenu {
public:
  PonscripterMenu(Kotonoha::Engine &engineRef,
                  std::function<void(const std::string &)> onAction)
      : InGameMenu(engineRef), onAction_(std::move(onAction)) {}

protected:
  bool designSpace() const override { return false; }

  int itemCount() const override { return 4; }

  std::string itemLabel(int index) const override {
    switch (index) {
    case 0:
      return "Salvar";
    case 1:
      return "Carregar";
    case 2:
      return "Histórico";
    case 3:
      return "Tela inicial";
    default:
      return {};
    }
  }

  void activate(int index) override {
    static const char *actions[]{"save", "load", "lookback", "reset"};
    if (index < 0 || index >= 4)
      return;
    close();
    onAction_(actions[index]);
  }

  void renderPanel(Kotonoha::RendererRef renderer) override {
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 165);
    SDL_FRect full{0.0f, 0.0f, 800.0f, 600.0f};
    SDL_RenderFillRect(&renderer, &full);

    constexpr float menuW = 340.0f;
    constexpr float itemH = 30.0f;
    const float menuH = static_cast<float>(itemCount()) * itemH + 32.0f;
    const float menuX = (800.0f - menuW) * 0.5f;
    const float menuY = (600.0f - menuH) * 0.5f;
    SDL_SetRenderDrawColor(&renderer, 18, 22, 32, 235);
    SDL_FRect panel{menuX, menuY, menuW, menuH};
    SDL_RenderFillRect(&renderer, &panel);
    SDL_SetRenderDrawColor(&renderer, 160, 175, 195, 210);
    SDL_RenderRect(&renderer, &panel);
  }

  SDL_FRect itemRect(int index) const override {
    return {230.0f, 240.0f + static_cast<float>(index) * 30.0f, 340.0f, 30.0f};
  }

  void renderItem(Kotonoha::RendererRef renderer, int index,
                  bool focused) override {
    auto &ui = engine.GetVNUI();
    const auto &settings = engine.GetSettings();
    const SDL_Color selected = settings.vnTextColorOverride
                                   ? settings.vnSpeakerColor
                                   : SDL_Color{255, 153, 153, 255};
    const SDL_Color normal = settings.vnTextColorOverride
                                 ? settings.vnTextColor
                                 : SDL_Color{153, 255, 153, 255};
    ui.text(renderer, itemLabel(index), itemRect(index).x + 24.0f,
            itemRect(index).y + 5.0f, itemRect(index).w - 48.0f,
            focused ? selected : normal);
  }

private:
  std::function<void(const std::string &)> onAction_;
};

class PonscripterBacklog final : public Kotonoha::VNBacklog {
public:
  explicit PonscripterBacklog(Kotonoha::Engine &engineRef)
      : VNBacklog(engineRef) {}

  bool designSpace() const override { return false; }
  void open() override {
    VNBacklog::open();
    clampSelection();
  }
  void clear() override {
    VNBacklog::clear();
    selected = 0;
  }
  bool onEvent(Kotonoha::EventRef rawEvent) override {
    if (!isVisible())
      return false;
    const auto event = Kotonoha::MapControllerEvent(rawEvent);
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_B) {
        close();
        return true;
      }
      if (event.key.key == SDLK_UP || event.key.key == SDLK_LEFT ||
          event.key.key == SDLK_PAGEUP) {
        moveSelection(-1);
        return true;
      }
      if (event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT ||
          event.key.key == SDLK_PAGEDOWN) {
        moveSelection(1);
        return true;
      }
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
      moveSelection(event.wheel.y > 0 ? -1 : 1);
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_RIGHT) {
      close();
      return true;
    }
    return VNBacklog::onEvent(rawEvent);
  }

protected:
  void renderFrame(Kotonoha::RendererRef renderer) override {
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(&renderer, 12, 16, 22, 230);
    SDL_FRect box{40.0f, 50.0f, 720.0f, 500.0f};
    SDL_RenderFillRect(&renderer, &box);
    SDL_SetRenderDrawColor(&renderer, 150, 165, 190, 200);
    SDL_RenderRect(&renderer, &box);
  }

  void renderEntries(Kotonoha::RendererRef renderer) override {
    auto &ui = engine.GetVNUI();
    const auto &settings = engine.GetSettings();
    const SDL_Color speaker = settings.vnTextColorOverride
                                  ? settings.vnSpeakerColor
                                  : SDL_Color{255, 215, 100, 255};
    const SDL_Color dialogue = settings.vnTextColorOverride
                                   ? settings.vnTextColor
                                   : SDL_Color{235, 235, 240, 255};
    clampSelection();
    ui.text(renderer, "Historico de mensagens", 60.0f, 62.0f, 680.0f, speaker);
    const std::string count = entries.empty()
                                  ? "0/0"
                                  : std::to_string(selected + 1) + "/" +
                                        std::to_string(entries.size());
    ui.text(renderer, count + " - Roda / Setas | Clique direito: Fechar", 60.0f,
            82.0f, 680.0f, dialogue);
    if (entries.empty()) {
      ui.text(renderer, "Nenhuma mensagem no historico ainda.", 60.0f, 96.0f,
              680.0f, {170, 175, 185, 255});
      return;
    }
    const auto &entry = entries[selected];
    float y = 116.0f;
    if (!entry.speaker.empty())
      y += ui.text(renderer, entry.speaker, 60.0f, y, 680.0f, speaker);
    ui.text(renderer, entry.text, 60.0f, y, 680.0f, dialogue);
  }

private:
  void clampSelection() {
    selected = entries.empty()
                   ? 0
                   : std::clamp(selected, std::size_t{0}, entries.size() - 1);
  }
  void moveSelection(int delta) {
    if (entries.empty()) {
      selected = 0;
      return;
    }
    if (delta < 0) {
      if (selected > 0)
        --selected;
    } else if (delta > 0) {
      if (selected < entries.size() - 1)
        ++selected;
    }
  }
  std::size_t selected = 0;
};

struct PonscripterRuntime::Impl final : public HostCallbacks {
  PonscripterRuntime &runtime;
  std::shared_ptr<PonscripterAssets> assets;
  Kotonoha::Engine *engine = nullptr;

  // PONscripter-native implementations of Kotonoha shared UI components
  Kotonoha::VNBacklog *backlog = nullptr;
  Kotonoha::InGameMenu *inGameMenu = nullptr;
  std::size_t syncedBacklogCount = 0;

  // Indexed script nodes & translation tables for Kotonoha Debug / Translation
  // / Dubbing Applets
  struct IndexedSection {
    int routeId = 0;
    std::string label;
    std::size_t statementIndex = 0;
    std::vector<std::string> texts;
    std::vector<std::string> assets;
    std::vector<Kotonoha::DebugAudioAsset> audioLines;
  };
  std::vector<IndexedSection> sections;
  std::unordered_map<std::string, int> labelToRouteId;
  std::unordered_map<int, std::size_t> routeIdToSectionIndex;
  Kotonoha::Translation::SourceTexts cachedTranslationSources;

  // Image dimension & processed texture cache
  struct DimEntry {
    int w = 0;
    int h = 0;
    bool hasAlpha = true;
  };
  std::unordered_map<std::string, DimEntry> dimCache;
  std::unordered_map<std::string, ProcessedImage> imageCache;

  // Transition snapshot textures (800x600)
  Kotonoha::TexturePtr outgoingTarget;
  Kotonoha::TexturePtr incomingTarget;
  Kotonoha::TexturePtr postProcessTexture;
  int postProcessWidth = 0;
  int postProcessHeight = 0;
  bool captureOutgoingBeforeNextEffect = false;
  bool hasValidOutgoingSnapshot = false;

  // Audio players
  Kotonoha::PcmPlayer bgmPlayer;
  std::array<Kotonoha::PcmPlayer, 50> wavePlayers;
  int bgmVol = 100;
  int seVol = 100;
  int voiceVol = 100;

  // Video player
  std::unique_ptr<Kotonoha::MediaPlayer> videoPlayer;
  bool videoCanCancel = true;
  Kotonoha::VideoSkipGesture videoSkip;

  // Fonts mapped via h_mapfont
  std::array<std::unique_ptr<Kotonoha::UI>, 2> mappedUiFonts;

  // System menu state (rmenu / save / load / lookback)
  int systemMenuHoverIndex = -1;
  int saveLoadPage = 0;
  int backlogScrollOffset = 0;
  bool quitRequested = false;
  std::string statusBannerText;
  std::uint64_t statusBannerUntilTick = 0;

  explicit Impl(PonscripterRuntime &rt, std::shared_ptr<PonscripterAssets> a)
      : runtime(rt), assets(std::move(a)) {}

  void showBanner(std::string msg, std::uint64_t durationMs = 2200) {
    statusBannerText = std::move(msg);
    statusBannerUntilTick = SDL_GetTicks() + durationMs;
  }

  void ensureScriptIndex() {
    if (sections.empty() && !runtime.vm().script().statements().empty()) {
      buildKotonohaScriptIndex();
    }
  }

  // Builds Kotonoha DebugNode, Translation::SourceTexts, and VoiceDubbingApplet
  // audio/dialogue pairs across the entire loaded PONscripter script!
  void buildKotonohaScriptIndex() {
    sections.clear();
    labelToRouteId.clear();
    routeIdToSectionIndex.clear();
    cachedTranslationSources.clear();

    const auto &stmts = runtime.vm().script().statements();
    if (stmts.empty())
      return;

    IndexedSection currentSec;
    currentSec.routeId = 0;
    currentSec.label = "define";
    currentSec.statementIndex = 0;

    std::string pendingVoicePath;
    std::set<std::string> seenSecAssets;
    std::set<std::string> seenSecTexts;

    auto flushSection = [&]() {
      if (!currentSec.label.empty() || !currentSec.texts.empty() ||
          !currentSec.assets.empty()) {
        const std::size_t idx = sections.size();
        labelToRouteId[currentSec.label] = currentSec.routeId;
        routeIdToSectionIndex[currentSec.routeId] = idx;
        if (!currentSec.texts.empty()) {
          cachedTranslationSources[currentSec.routeId] = currentSec.texts;
        }
        sections.push_back(std::move(currentSec));
      }
      seenSecAssets.clear();
      seenSecTexts.clear();
      pendingVoicePath.clear();
    };

    for (std::size_t i = 0; i < stmts.size(); ++i) {
      const auto &st = stmts[i];
      if (st.isLabel) {
        flushSection();
        currentSec = IndexedSection{};
        currentSec.routeId = static_cast<int>(i);
        currentSec.label = st.labelName;
        currentSec.statementIndex = i;
        continue;
      }

      if (st.isText) {
        TextStyle dummyStyle;
        const auto fmt = TextFormatter::formatLine(st.text, dummyStyle, true);
        if (!fmt.plainText.empty()) {
          if (seenSecTexts.insert(fmt.plainText).second) {
            currentSec.texts.push_back(fmt.plainText);
          }
          if (!pendingVoicePath.empty()) {
            currentSec.audioLines.push_back(
                {pendingVoicePath,
                 inferCharacterFromVoicePath(pendingVoicePath), fmt.plainText});
            pendingVoicePath.clear();
          }
        }
        continue;
      }

      LineLexer lx(st.text, &runtime.vm().variables());
      const std::string cmd = lx.parseIdentifier();
      if (cmd == "dwave" || cmd == "dwaveloop") {
        const int ch = lx.parseIntExpr();
        lx.consumeComma();
        std::string p = lx.parseStringExpr();
        std::replace(p.begin(), p.end(), '\\', '/');
        if (!p.empty()) {
          if (assets) {
            const std::string res = assets->resolveActualPath(p);
            if (!res.empty())
              p = res;
          }
          if (seenSecAssets.insert(p).second)
            currentSec.assets.push_back(p);
          if (ch == 0 || p.rfind("voice/", 0) == 0)
            pendingVoicePath = p;
        }
      } else if (cmd == "mp3" || cmd == "mp3loop" || cmd == "bgm" ||
                 cmd == "wave") {
        std::string p = lx.parseStringExpr();
        std::replace(p.begin(), p.end(), '\\', '/');
        if (!p.empty()) {
          if (assets) {
            const std::string res = assets->resolveActualPath(p);
            if (!res.empty())
              p = res;
          }
          if (seenSecAssets.insert(p).second)
            currentSec.assets.push_back(p);
        }
      } else if (cmd == "bg") {
        const auto spec = parseTaggedString(lx.parseRawArg(), TransMode::Copy);
        if (!spec.imagePath.empty()) {
          std::string p = spec.imagePath;
          if (assets) {
            const std::string res = assets->resolveActualPath(p);
            if (!res.empty())
              p = res;
          }
          if (seenSecAssets.insert(p).second)
            currentSec.assets.push_back(p);
        }
      } else if (cmd == "ld" || cmd == "lsp" || cmd == "lsph") {
        if (cmd == "ld")
          (void)lx.parseIdentifier();
        else
          (void)lx.parseIntExpr();
        lx.consumeComma();
        const auto spec =
            parseTaggedString(lx.parseStringExpr(), TransMode::Alpha);
        if (spec.trans == TransMode::String && !spec.text.empty()) {
          if (seenSecTexts.insert(spec.text).second)
            currentSec.texts.push_back(spec.text);
        } else if (!spec.imagePath.empty()) {
          std::string p = spec.imagePath;
          if (assets) {
            const std::string res = assets->resolveActualPath(p);
            if (!res.empty())
              p = res;
          }
          if (seenSecAssets.insert(p).second)
            currentSec.assets.push_back(p);
        }
      } else if (cmd == "select" || cmd == "csel") {
        while (!lx.eof()) {
          const std::string optText =
              TextFormatter::formatUiLabel(lx.parseStringExpr(), true);
          if (!optText.empty() && seenSecTexts.insert(optText).second)
            currentSec.texts.push_back(optText);
          if (!lx.consumeComma())
            break;
          (void)lx.parseLabelTarget();
          if (!lx.consumeComma())
            break;
        }
      }
    }
    flushSection();
  }

  std::string translateLine(const std::string &original) const {
    if (!engine || original.empty())
      return original;
    const int routeId = runtime.translationRoute();
    return engine->GetTranslation().text(routeId, original);
  }

  std::filesystem::path saveRootDir() const {
    try {
      const auto dir = Kotonoha::saveDirectory();
      std::filesystem::create_directories(dir);
      return dir;
    } catch (...) {
      return {};
    }
  }

  std::filesystem::path globalSavePath() const {
    const auto dir = saveRootDir();
    return dir.empty() ? std::filesystem::path{} : (dir / "gloval.sav");
  }

  std::filesystem::path slotSavePath(int slot) const {
    const auto dir = saveRootDir();
    if (dir.empty())
      return {};
    return dir / ("save" + std::to_string(slot) + ".dat");
  }

  void loadPersistedGlobals() {
    const auto path = globalSavePath();
    if (path.empty() || !std::filesystem::exists(path))
      return;
    try {
      const auto bytes = Kotonoha::Filesystem::readLocalFile(path);
      std::string text(bytes.begin(), bytes.end());
      GlobalSaveData data;
      if (SaveSerializer::deserializeGlobals(text, data)) {
        runtime.vm().importGlobalSave(data);
      }
    } catch (...) {
    }
  }

  void onPersistGlobals() override {
    const auto path = globalSavePath();
    if (path.empty())
      return;
    try {
      const GlobalSaveData data = runtime.vm().exportGlobalSave();
      const std::string text = SaveSerializer::serializeGlobals(data);
      std::vector<std::uint8_t> bytes(text.begin(), text.end());
      std::string err;
      (void)Kotonoha::Filesystem::writeBinaryFileAtomically(path, bytes, &err);
    } catch (...) {
    }
  }

  SaveTimestamp querySaveSlot(int slot) const override {
    const auto path = slotSavePath(slot);
    if (path.empty() || !std::filesystem::exists(path))
      return {};
    try {
      const auto bytes = Kotonoha::Filesystem::readLocalFile(path);
      std::string text(bytes.begin(), bytes.end());
      return SaveSerializer::peekSlotTimestamp(text);
    } catch (...) {
      return {};
    }
  }

  bool onSaveSlot(int slot, const std::string &customTitle) override {
    const auto path = slotSavePath(slot);
    if (path.empty())
      return false;
    try {
      SlotSaveData data = runtime.vm().exportSlotSave();
      const std::time_t now = std::time(nullptr);
      const std::tm *lt = std::localtime(&now);
      data.timestamp.exists = true;
      if (lt) {
        data.timestamp.year = lt->tm_year + 1900;
        data.timestamp.month = lt->tm_mon + 1;
        data.timestamp.day = lt->tm_mday;
        data.timestamp.hour = lt->tm_hour;
        data.timestamp.minute = lt->tm_min;
      }
      data.timestamp.title =
          !customTitle.empty() ? customTitle : runtime.vm().currentLabel();
      const std::string text = SaveSerializer::serializeSlot(data);
      std::vector<std::uint8_t> bytes(text.begin(), text.end());
      std::string err;
      const bool ok =
          Kotonoha::Filesystem::writeBinaryFileAtomically(path, bytes, &err);
      onPersistGlobals();
      if (ok) {
        showBanner("Salvo no Slot #" + std::to_string(slot));
      }
      return ok;
    } catch (...) {
      return false;
    }
  }

  bool onLoadSlot(int slot) override {
    const auto path = slotSavePath(slot);
    if (path.empty() || !std::filesystem::exists(path))
      return false;
    try {
      const auto bytes = Kotonoha::Filesystem::readLocalFile(path);
      std::string text(bytes.begin(), bytes.end());
      SlotSaveData data;
      if (SaveSerializer::deserializeSlot(text, data)) {
        runtime.vm().importSlotSave(data);
        showBanner("Carregado do Slot #" + std::to_string(slot));
        return true;
      }
    } catch (...) {
    }
    return false;
  }

  bool queryImageSize(const std::string &path, int &outW, int &outH,
                      bool &outHasAlpha) override {
    if (!assets || path.empty())
      return false;
    const std::string resolved = assets->resolveActualPath(path);
    if (resolved.empty())
      return false;

    const auto it = dimCache.find(resolved);
    if (it != dimCache.end()) {
      outW = it->second.w;
      outH = it->second.h;
      outHasAlpha = it->second.hasAlpha;
      return true;
    }

    try {
      auto surf = assets->loadSurface(resolved);
      if (surf && surf->w > 0 && surf->h > 0) {
        const SDL_PixelFormatDetails *details =
            SDL_GetPixelFormatDetails(surf->format);
        const bool hasAlpha = (details && details->Amask != 0);
        dimCache[resolved] = DimEntry{surf->w, surf->h, hasAlpha};
        outW = surf->w;
        outH = surf->h;
        outHasAlpha = hasAlpha;
        return true;
      }
    } catch (...) {
    }
    return false;
  }

  SDL_Texture *getOrLoadTexture(Kotonoha::RendererRef renderer,
                                const TaggedImageSpec &spec) {
    if (!assets || spec.imagePath.empty())
      return nullptr;
    const std::string resolved = assets->resolveActualPath(spec.imagePath);
    if (resolved.empty())
      return nullptr;

    const auto &upscale = engine ? engine->GetUpscaler().getSettings()
                                 : Kotonoha::UpscaleSettings{};
    const std::string cacheKey =
        resolved + "|" + std::to_string(static_cast<int>(spec.trans)) + "|" +
        std::to_string(spec.numCells) + "|" +
        std::to_string(static_cast<int>(upscale.backend)) + "|" +
        std::to_string(static_cast<int>(upscale.preset)) + "|" +
        std::to_string(upscale.factor);
    auto it = imageCache.find(cacheKey);
    if (it != imageCache.end() && it->second.texture)
      return it->second.texture.get();

    try {
      auto rawSurf = assets->loadSurface(resolved);
      if (!rawSurf)
        return nullptr;
      bool hadAlpha = false;
      auto transSurf =
          buildTransSurface(rawSurf.get(), spec.trans, spec.numCells, hadAlpha);
      if (!transSurf)
        return nullptr;

      Kotonoha::TexturePtr tex(
          SDL_CreateTextureFromSurface(&renderer, transSurf.get()));
      if (!tex)
        return nullptr;
      SDL_SetTextureScaleMode(tex.get(), SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(tex.get(), spec.trans == TransMode::Copy
                                             ? SDL_BLENDMODE_NONE
                                             : SDL_BLENDMODE_BLEND);

      ProcessedImage entry;
      entry.width = transSurf->w;
      entry.height = transSurf->h;
      entry.hasNativeAlpha = hadAlpha;
      entry.surface = std::move(transSurf);
      if (engine && upscale.enabled()) {
        entry.texture = engine->GetUpscaler().scaled(*tex, upscale.factor);
      } else {
        entry.texture = std::move(tex);
      }
      SDL_Texture *rawPtr = entry.texture.get();
      imageCache[cacheKey] = std::move(entry);
      return rawPtr;
    } catch (...) {
      return nullptr;
    }
  }

  const ProcessedImage *getProcessedImage(const TaggedImageSpec &spec) {
    if (!assets || spec.imagePath.empty())
      return nullptr;
    const std::string resolved = assets->resolveActualPath(spec.imagePath);
    if (resolved.empty())
      return nullptr;
    const auto &upscale = engine ? engine->GetUpscaler().getSettings()
                                 : Kotonoha::UpscaleSettings{};
    const std::string cacheKey =
        resolved + "|" + std::to_string(static_cast<int>(spec.trans)) + "|" +
        std::to_string(spec.numCells) + "|" +
        std::to_string(static_cast<int>(upscale.backend)) + "|" +
        std::to_string(static_cast<int>(upscale.preset)) + "|" +
        std::to_string(upscale.factor);
    const auto it = imageCache.find(cacheKey);
    return it == imageCache.end() ? nullptr : &it->second;
  }

  void playClipOnPlayer(Kotonoha::PcmPlayer &player,
                        const std::string &channelName, const std::string &path,
                        bool loop, float gain) {
    if (!engine || !assets || path.empty())
      return;
    const std::string resolved = assets->resolveActualPath(path);
    if (resolved.empty())
      return;
    try {
      auto audioClip = assets->loadAudio(resolved);
      if (audioClip.data.empty())
        return;

      SDL_AudioSpec targetSpec{};
      targetSpec.format = SDL_AUDIO_S16;
      targetSpec.channels = std::clamp(audioClip.spec.channels, 1, 2);
      targetSpec.freq = audioClip.spec.freq > 0 ? audioClip.spec.freq : 44100;

      Uint8 *dstData = nullptr;
      int dstLen = 0;
      if (!SDL_ConvertAudioSamples(&audioClip.spec, audioClip.data.data(),
                                   static_cast<int>(audioClip.data.size()),
                                   &targetSpec, &dstData, &dstLen)) {
        return;
      }

      Kotonoha::PcmPlayer::Clip pcm;
      pcm.channels = targetSpec.channels;
      pcm.rate = targetSpec.freq;
      pcm.loop = loop;
      pcm.gain = std::clamp(gain, 0.0f, 1.0f);
      const std::size_t sampleCount =
          static_cast<std::size_t>(dstLen) / sizeof(std::int16_t);
      pcm.samples.resize(sampleCount);
      std::copy_n(reinterpret_cast<const std::int16_t *>(dstData), sampleCount,
                  pcm.samples.data());
      SDL_free(dstData);

      auto ch = engine->GetAudio().channel(channelName);
      player.Play(ch, std::move(pcm));
    } catch (...) {
    }
  }

  void onPlayBgm(const std::string &path, bool loop, int fadeInMs) override {
    (void)fadeInMs;
    const float gain = static_cast<float>(bgmVol) / 100.0f;
    playClipOnPlayer(bgmPlayer, "bgm", path, loop, gain);
  }

  void onStopBgm(int fadeOutMs) override {
    (void)fadeOutMs;
    bgmPlayer.Stop();
  }

  void onPlayWave(int channel, const std::string &path, bool loop) override {
    if (channel < 0 || channel >= static_cast<int>(wavePlayers.size()))
      return;
    const bool isVoice = (channel == 0 || path.rfind("voice/", 0) == 0);
    const float vol = static_cast<float>(isVoice ? voiceVol : seVol) / 100.0f;
    playClipOnPlayer(wavePlayers[channel], isVoice ? "voice" : "se", path, loop,
                     vol);
    if (isVoice && backlog) {
      const std::string resolved =
          assets ? assets->resolveActualPath(path) : path;
      backlog->setLastVoice(!resolved.empty() ? resolved : path);
    }
  }

  void onStopWave(int channel) override {
    if (channel < 0) {
      for (auto &wp : wavePlayers)
        wp.Stop();
    } else if (channel < static_cast<int>(wavePlayers.size())) {
      wavePlayers[channel].Stop();
    }
  }

  void onVolumeChanged(int bVol, int sVol, int vVol) override {
    bgmVol = bVol;
    seVol = sVol;
    voiceVol = vVol;
    bgmPlayer.SetGain(static_cast<float>(bgmVol) / 100.0f);
    wavePlayers[0].SetGain(static_cast<float>(voiceVol) / 100.0f);
    for (std::size_t i = 1; i < wavePlayers.size(); ++i)
      wavePlayers[i].SetGain(static_cast<float>(seVol) / 100.0f);
  }

  void onChannelVolumeChanged(int channel, int vol) override {
    if (channel >= 0 && channel < static_cast<int>(wavePlayers.size())) {
      wavePlayers[channel].SetGain(static_cast<float>(vol) / 100.0f);
    }
  }

  void onPlayVideo(const std::string &path, bool canCancel) override {
    videoCanCancel = canCancel;
    videoSkip.reset();
    if (!engine || !assets || path.empty()) {
      runtime.vm().finishVideo();
      return;
    }
    const std::string resolved = assets->resolveActualPath(path);
    if (resolved.empty()) {
      runtime.vm().finishVideo();
      return;
    }
    try {
      videoPlayer = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
      videoPlayer->open(*assets, resolved, false, "video");
      if (!videoPlayer->isOpen()) {
        videoPlayer.reset();
        runtime.vm().finishVideo();
      }
    } catch (...) {
      videoPlayer.reset();
      runtime.vm().finishVideo();
    }
  }

  void onCaptionChanged(const std::string &caption) override {
    if (engine)
      SDL_SetWindowTitle(&engine->GetWindow(), caption.c_str());
  }

  void onFullscreenChanged(bool fullscreen) override {
    if (!engine)
      return;
    try {
      auto s = engine->GetSettings();
      s.mode = fullscreen ? Kotonoha::WindowMode::Borderless
                          : Kotonoha::WindowMode::Windowed;
      engine->applySettings(s);
    } catch (...) {
    }
  }

  bool queryFullscreen() const override {
    if (!engine)
      return false;
    return engine->GetSettings().mode != Kotonoha::WindowMode::Windowed;
  }

  void onBeginEffect(const ActiveEffect &effect) override {
    (void)effect;
    captureOutgoingBeforeNextEffect = true;
  }

  void onFinishEffect() override { hasValidOutgoingSnapshot = false; }

  void onSystemCall(const std::string &kind) override {
    systemMenuHoverIndex = -1;
    if (kind == "lookback") {
      backlogScrollOffset =
          std::max(0, static_cast<int>(runtime.vm().backlogPages().size()) - 1);
      if (backlog)
        backlog->open();
      return;
    }
    if (kind == "rmenu") {
      if (inGameMenu)
        inGameMenu->open();
      return;
    }
    // Save/load keep the PONscripter-native overlay and slot format.
  }
};

PonscripterRuntime::PonscripterRuntime(
    std::shared_ptr<PonscripterAssets> assets)
    : Kotonoha::Runtime("PONscripter", "0.1.0",
                        "me.hirameki.kotonoha.ponscripter"),
      impl_(std::make_unique<Impl>(*this, assets)), assets_(std::move(assets)),
      vm_(impl_.get()) {
  if (!assets_) {
    assets_ = std::make_shared<PonscripterAssets>();
    impl_->assets = assets_;
  }
}

PonscripterRuntime::~PonscripterRuntime() { shutdown(); }

void PonscripterRuntime::shutdown() {
  if (!impl_)
    return;
  if (impl_->videoPlayer) {
    impl_->videoPlayer->stop();
    impl_->videoPlayer.reset();
  }
  impl_->bgmPlayer.Stop();
  for (auto &wp : impl_->wavePlayers)
    wp.Stop();
  ClearScenes();
  impl_->backlog = nullptr;
  impl_->inGameMenu = nullptr;
  for (auto &font : impl_->mappedUiFonts)
    font.reset();
  impl_->outgoingTarget.reset();
  impl_->incomingTarget.reset();
  impl_->postProcessTexture.reset();
  impl_->postProcessWidth = impl_->postProcessHeight = 0;
  impl_->imageCache.clear();
  if (impl_->assets)
    impl_->assets->clearCache();
  impl_->quitRequested = false;
  impl_->engine = nullptr;
}

SDL_Point PonscripterRuntime::viewport() const {
  const auto res = vm_.resolution();
  return SDL_Point{res.width > 0 ? res.width : 800,
                   res.height > 0 ? res.height : 600};
}

void PonscripterRuntime::init(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
  impl_->quitRequested = false;
  ClearScenes();

  // Install PONscripter-native implementations behind Kotonoha shared
  // component helpers. Generic UI code therefore resolves the native design.
  (void)Kotonoha::GetVNManagerRuntime(engine);
  impl_->backlog =
      &engine.configureSharedScene<Kotonoha::VNBacklog, PonscripterBacklog>();
  impl_->backlog->clear();
  impl_->backlog->setVoiceReplay([this](const std::string &voiceId) {
    impl_->onPlayWave(0, voiceId, false);
  });
  impl_->backlog->SetOnClose([this] {
    if (vm_.waitState() == WaitState::WaitSystemCall &&
        vm_.activeSystemCall() == "lookback")
      vm_.finishSystemCall();
  });
  impl_->syncedBacklogCount = 0;

  impl_->inGameMenu =
      &engine.configureSharedScene<Kotonoha::InGameMenu, PonscripterMenu>(
          [this](const std::string &action) {
            if (action == "reset")
              vm_.resetToStart();
            else
              vm_.enterSystemCall(action);
          });
  impl_->inGameMenu->SetOnClose([this] {
    if (vm_.waitState() == WaitState::WaitSystemCall &&
        vm_.activeSystemCall() == "rmenu")
      vm_.finishSystemCall();
  });

  AddScene(impl_->backlog, Kotonoha::GameState::Gameplay);
  AddScene(impl_->inGameMenu, Kotonoha::GameState::Gameplay);

  vm_.clear();

  auto scripts = assets_ ? assets_->discoverScripts()
                         : std::vector<std::pair<std::string, std::string>>{};
  if (!scripts.empty()) {
    for (const auto &[name, content] : scripts) {
      vm_.appendScriptFile(name, content);
    }
  } else {
    const char *fallback =
        ";mode800\n"
        "*define\n"
        "globalon\n"
        "caption \"PONscripter [Kotonoha]\"\n"
        "effect 10,10,250\n"
        "savename \"^~~ Salvar ~~\",\"^~~ Carregar ~~\",\"#\"\n"
        "rmenu \"^~~ Salvar ~~\",save,\"^~~ Carregar ~~\",load,\"^\",,\"^~~ "
        "Histórico ~~\",lookback,\"^\",,\"^~~ Voltar ~~\",reset\n"
        "game\n"
        "*start\n"
        "setwindow3 60,70,34,20,20,20,0,4,24,1,1,#000000,40,50\n"
        "bg #182030,1\n"
        "^PONscripter VM inicializado com sucesso.@\n"
        "br\n"
        "^Pressione clique ou Enter para avançar.\\\n"
        "end\n";
    vm_.appendScriptFile("00.utf", fallback);
  }

  vm_.start();
  impl_->buildKotonohaScriptIndex();
  impl_->loadPersistedGlobals();

  for (int i = 0; i < 2; ++i) {
    const std::string &fontRel = vm_.mappedFont(i);
    if (!fontRel.empty() && assets_) {
      const std::string localFont = assets_->localPath(fontRel);
      if (!localFont.empty()) {
        try {
          impl_->mappedUiFonts[i] = std::make_unique<Kotonoha::UI>(localFont);
        } catch (...) {
        }
      }
    }
  }

  const auto res = vm_.resolution();
  SDL_SetRenderLogicalPresentation(&engine.GetRenderer(), res.width, res.height,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);

  SetState(Kotonoha::GameState::Gameplay);
  vm_.tick(16.0);
}

void PonscripterRuntime::initGlobal(Kotonoha::Engine &engine) { (void)engine; }
void PonscripterRuntime::initSplash(Kotonoha::Engine &engine) { (void)engine; }
void PonscripterRuntime::initMenu(Kotonoha::Engine &engine) { (void)engine; }
void PonscripterRuntime::initGameplay(Kotonoha::Engine &engine) {
  impl_->engine = &engine;
}

void PonscripterRuntime::update() {
  Runtime::update();

  if (isStoryPaused())
    return;

  if (vm_.waitState() == WaitState::WaitVideo) {
    if (impl_->videoPlayer) {
      if (impl_->engine)
        impl_->videoPlayer->update(impl_->engine->GetRenderer());
      if (impl_->videoPlayer->finished()) {
        impl_->videoPlayer->stop();
        impl_->videoPlayer.reset();
        vm_.finishVideo();
      }
    } else {
      vm_.finishVideo();
    }
  }

  vm_.tick(1000.0 / 60.0);

  if (vm_.isHalted()) {
    if (impl_->engine && !impl_->quitRequested) {
      impl_->quitRequested = true;
      if (impl_->videoPlayer) {
        impl_->videoPlayer->stop();
        impl_->videoPlayer.reset();
      }
      impl_->bgmPlayer.Stop();
      for (auto &wp : impl_->wavePlayers)
        wp.Stop();
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
    return;
  }

  // Sync newly completed pages into PONscripter's VNBacklog override
  if (impl_->backlog) {
    const auto &pages = vm_.backlogPages();
    while (impl_->syncedBacklogCount < pages.size()) {
      impl_->backlog->addEntry("", pages[impl_->syncedBacklogCount++]);
      if (!vm_.lastVoicePath().empty() && assets_) {
        const std::string resolved =
            assets_->resolveActualPath(vm_.lastVoicePath());
        impl_->backlog->setLastVoice(!resolved.empty() ? resolved
                                                       : vm_.lastVoicePath());
      }
    }
  }
}

bool PonscripterRuntime::advanceStoryTime(double seconds) {
  if (seconds <= 0.0)
    return false;
  vm_.tick(seconds * 1000.0);
  return true;
}

void PonscripterRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                        float scale) {
  const auto res = vm_.resolution();
  const float viewW = static_cast<float>(res.width);
  const float viewH = static_cast<float>(res.height);

  if (vm_.waitState() == WaitState::WaitVideo && impl_->videoPlayer &&
      impl_->videoPlayer->isOpen()) {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    const SDL_FRect area{0.0f, 0.0f, viewW, viewH};
    impl_->videoPlayer->render(
        renderer, area, impl_->engine ? &impl_->engine->GetUpscaler() : nullptr,
        false);
    return;
  }

  auto &ui =
      impl_->engine ? impl_->engine->GetVNUI() : *impl_->mappedUiFonts[0];
  auto pickFont = [&](bool italic) -> Kotonoha::UI & {
    if (italic && impl_->mappedUiFonts[1])
      return *impl_->mappedUiFonts[1];
    if (impl_->mappedUiFonts[0])
      return *impl_->mappedUiFonts[0];
    return ui;
  };

  auto renderSceneContents = [&]() {
    // 1. Background layer (`bg`)
    const auto &bg = vm_.background();
    if (bg.trans == TransMode::DirectColor) {
      const SDL_Color c = rgb24ToSdl(bg.color);
      SDL_SetRenderDrawColor(&renderer, c.r, c.g, c.b, 255);
      SDL_RenderClear(&renderer);
    } else {
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
      SDL_RenderClear(&renderer);
      if (!bg.imagePath.empty()) {
        if (SDL_Texture *tex = impl_->getOrLoadTexture(renderer, bg)) {
          SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
          SDL_SetTextureAlphaMod(tex, 255);
          SDL_FRect dst{0.0f, 0.0f, viewW, viewH};
          SDL_RenderTexture(&renderer, tex, nullptr, &dst);
        }
      }
    }

    // 2. Character standing sprites (`ld l, c, r` in back-to-front order: l, r,
    // c)
    const auto &standing = vm_.standingSlots();
    for (int idx : {0, 2, 1}) {
      const auto &st = standing[idx];
      if (!st.loaded || st.spec.imagePath.empty())
        continue;
      if (SDL_Texture *tex = impl_->getOrLoadTexture(renderer, st.spec)) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(tex, &tw, &th);
        const auto *processed = impl_->getProcessedImage(st.spec);
        const float logicalW =
            processed ? static_cast<float>(processed->width) : tw;
        const float logicalH =
            processed ? static_cast<float>(processed->height) : th;
        const int cells = std::max(1, st.spec.numCells);
        const float cellW = tw / static_cast<float>(cells);
        const float logicalCellW = logicalW / static_cast<float>(cells);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(st.alpha));
        SDL_FRect src{0.0f, 0.0f, cellW, th};
        SDL_FRect dst{static_cast<float>(st.x), static_cast<float>(st.y),
                      logicalCellW, logicalH};
        SDL_RenderTexture(&renderer, tex, &src, &dst);
      }
    }

    // 3. Extended sprites (`lsp` 999 down to 0 — Sprite 0 is topmost!)
    const auto &sprites = vm_.sprites();
    for (int id = PonscripterVm::kMaxExtendedSprites - 1; id >= 0; --id) {
      const auto &sp = sprites[id];
      if (!sp.loaded || !sp.visible || sp.alpha <= 0 ||
          vm_.areSpritesHidden(sp.secondaryLayer))
        continue;

      if (sp.spec.trans == TransMode::String) {
        if (!sp.spec.text.empty()) {
          const std::string displayStr = impl_->translateLine(sp.spec.text);
          const float fh = static_cast<float>(sp.spec.fontHeight > 0
                                                  ? sp.spec.fontHeight
                                                  : vm_.windowSpec().fontH);
          const std::uint32_t colRgb =
              (sp.currentCell == 1 && sp.spec.hasHoverColor)
                  ? sp.spec.hoverColor
                  : sp.spec.color;
          auto &fontUi = pickFont(false);
          fontUi.textSized(renderer, displayStr, static_cast<float>(sp.x + 1),
                           static_cast<float>(sp.y + 1), viewW, fh,
                           SDL_Color{0, 0, 0, static_cast<Uint8>(sp.alpha)});
          fontUi.textSized(renderer, displayStr, static_cast<float>(sp.x),
                           static_cast<float>(sp.y), viewW, fh,
                           rgb24ToSdl(colRgb, static_cast<Uint8>(sp.alpha)));
        }
        continue;
      }

      if (sp.spec.trans == TransMode::DirectColor) {
        SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
        const SDL_Color c =
            rgb24ToSdl(sp.spec.color, static_cast<Uint8>(sp.alpha));
        SDL_SetRenderDrawColor(&renderer, c.r, c.g, c.b, c.a);
        SDL_FRect r{static_cast<float>(sp.x), static_cast<float>(sp.y), viewW,
                    viewH};
        SDL_RenderFillRect(&renderer, &r);
        continue;
      }

      if (SDL_Texture *tex = impl_->getOrLoadTexture(renderer, sp.spec)) {
        SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
        if (sp.blendMode == 1) {
          blend = SDL_BLENDMODE_ADD;
        } else if (sp.blendMode == 2) {
          blend = SDL_ComposeCustomBlendMode(
              SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE,
              SDL_BLENDOPERATION_REV_SUBTRACT,
              SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE,
              SDL_BLENDOPERATION_ADD);
        }
        // Texture objects are cached across sprites, so reset their blend mode
        // on every draw. The software backend may reject custom subtraction.
        if (!SDL_SetTextureBlendMode(tex, blend))
          SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(tex, &tw, &th);
        const auto *processed = impl_->getProcessedImage(sp.spec);
        const float logicalW =
            processed ? static_cast<float>(processed->width) : tw;
        const float logicalH =
            processed ? static_cast<float>(processed->height) : th;
        const int cells = std::max(1, sp.spec.numCells);
        const float cellW = tw / static_cast<float>(cells);
        const float logicalCellW = logicalW / static_cast<float>(cells);
        const int cellIdx = std::clamp(sp.currentCell, 0, cells - 1);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(sp.alpha));
        SDL_FRect src{cellIdx * cellW, 0.0f, cellW, th};
        SDL_FRect dst{static_cast<float>(sp.x), static_cast<float>(sp.y),
                      logicalCellW, logicalH};
        SDL_RenderTexture(&renderer, tex, &src, &dst);
      }
    }

    // 4. Bar gauges (`bar` 0..15)
    for (const auto &bar : vm_.bars()) {
      if (!bar.active || bar.width <= 0 || bar.height <= 0)
        continue;
      const float ratio =
          std::clamp(static_cast<float>(bar.value) /
                         static_cast<float>(std::max(1, bar.maxValue)),
                     0.0f, 1.0f);
      const float fillW = bar.width * ratio;
      if (fillW > 0.5f) {
        const SDL_Color c = rgb24ToSdl(bar.colorRgb, 255);
        SDL_SetRenderDrawColor(&renderer, c.r, c.g, c.b, 255);
        SDL_FRect rect{static_cast<float>(bar.x), static_cast<float>(bar.y),
                       fillW, static_cast<float>(bar.height)};
        SDL_RenderFillRect(&renderer, &rect);
      }
    }

    // 5. `btn` hover highlight slice from `btndef`
    if (vm_.hoveredButtonId() > 0 && !vm_.btndefSpec().imagePath.empty()) {
      for (const auto &btn : vm_.activeButtons()) {
        if (btn.id == vm_.hoveredButtonId() &&
            btn.kind == ButtonDef::Kind::RectBtn) {
          if (SDL_Texture *tex =
                  impl_->getOrLoadTexture(renderer, vm_.btndefSpec())) {
            SDL_SetTextureAlphaMod(tex, 255);
            SDL_FRect src{static_cast<float>(btn.srcX),
                          static_cast<float>(btn.srcY),
                          static_cast<float>(btn.w), static_cast<float>(btn.h)};
            SDL_FRect dst{static_cast<float>(btn.x), static_cast<float>(btn.y),
                          static_cast<float>(btn.w), static_cast<float>(btn.h)};
            SDL_RenderTexture(&renderer, tex, &src, &dst);
          }
          break;
        }
      }
    }

    // cselbtn is a selectable text rectangle rendered by the shared GL
    // accelerated VN UI, never by SDL_ttf. It is independent of the dialogue
    // window so *customsel can position choices anywhere on screen.
    for (const auto &btn : vm_.activeButtons()) {
      if (btn.kind != ButtonDef::Kind::TextBtn || btn.text.empty())
        continue;
      const bool hovered = vm_.hoveredButtonId() == btn.id;
      const std::uint32_t color =
          hovered ? vm_.selectHoverColor() : vm_.selectIdleColor();
      auto &fontUi = pickFont(false);
      const std::string text = impl_->translateLine(btn.text);
      const float textSize = static_cast<float>(
          std::max(1, vm_.windowSpec().fontH));
      if (vm_.windowSpec().shadow) {
        fontUi.textSized(renderer, text,
                         static_cast<float>(btn.x + 1),
                         static_cast<float>(btn.y + 1),
                         static_cast<float>(std::max(1, btn.w)), textSize,
                         SDL_Color{0, 0, 0, 255});
      }
      fontUi.textSized(renderer, text, static_cast<float>(btn.x),
                       static_cast<float>(btn.y),
                       static_cast<float>(std::max(1, btn.w)), textSize,
                       rgb24ToSdl(color, 255));
    }

    // 6. Text Window Layer (`setwindow3` backdrop + proportional glyphs +
    // live Kotonoha translation override + `select` options + wait cursor)
    if (vm_.isTextWindowVisible()) {
      const auto &win = vm_.windowSpec();
      if (!win.backdrop.imagePath.empty()) {
        if (SDL_Texture *tex =
                impl_->getOrLoadTexture(renderer, win.backdrop)) {
          SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
          float tw = 0.0f, th = 0.0f;
          SDL_GetTextureSize(tex, &tw, &th);
          const auto *processed = impl_->getProcessedImage(win.backdrop);
          const float logicalW =
              processed ? static_cast<float>(processed->width) : tw;
          const float logicalH =
              processed ? static_cast<float>(processed->height) : th;
          SDL_SetTextureAlphaMod(tex, 255);
          SDL_FRect dst{static_cast<float>(win.winX),
                        static_cast<float>(win.winY), logicalW, logicalH};
          SDL_RenderTexture(&renderer, tex, nullptr, &dst);
        }
      } else if (win.backdrop.trans == TransMode::DirectColor) {
        SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
        const SDL_Color c = rgb24ToSdl(win.backdrop.color, 190);
        SDL_SetRenderDrawColor(&renderer, c.r, c.g, c.b, c.a);
        SDL_FRect box{static_cast<float>(win.winX),
                      static_cast<float>(win.winY),
                      static_cast<float>(win.textAreaWidth() + 40),
                      static_cast<float>(win.textAreaHeight() + 40)};
        SDL_RenderFillRect(&renderer, &box);
      }

      const float startX = static_cast<float>(win.textX);
      const float maxX = startX + static_cast<float>(win.textAreaWidth());
      const float lineStep = static_cast<float>(win.fontH + win.pitchY);
      const float fontSize = static_cast<float>(win.fontH);
      float penX = startX;
      float penY = static_cast<float>(win.textY);

      std::size_t charsRemaining = vm_.typewriterCharIndex();
      std::string lastOverriddenLineKey;

      for (const auto &span : vm_.currentPageSpans()) {
        if (span.isLineBreak) {
          penX = startX;
          penY += lineStep;
          lastOverriddenLineKey.clear();
          continue;
        }
        if (charsRemaining == 0)
          break;

        // Check if Kotonoha Translation Applet has an active override for this
        // lineKey!
        std::string spanText = span.text;
        if (!span.lineKey.empty()) {
          const std::string translatedLine = impl_->translateLine(span.lineKey);
          if (translatedLine != span.lineKey) {
            if (lastOverriddenLineKey == span.lineKey) {
              // Already rendered the overridden line on the first span of this
              // line
              charsRemaining = (charsRemaining >= span.text.size())
                                   ? (charsRemaining - span.text.size())
                                   : 0;
              continue;
            }
            lastOverriddenLineKey = span.lineKey;
            spanText = translatedLine;
          }
        }

        const std::size_t revealLen =
            (charsRemaining >= spanText.size())
                ? spanText.size()
                : Kotonoha::utf8Prefix(spanText, charsRemaining);
        charsRemaining = (charsRemaining >= span.text.size())
                             ? (charsRemaining - span.text.size())
                             : 0;
        if (revealLen == 0)
          continue;

        const std::string visibleRun = spanText.substr(0, revealLen);
        auto &fontUi = pickFont(span.style.italic);
        const SDL_Color fg = rgb24ToSdl(span.style.colorRgb, 255);

        std::size_t p = 0;
        while (p < visibleRun.size()) {
          std::size_t nextSpace = visibleRun.find(' ', p);
          std::string word = (nextSpace == std::string::npos)
                                 ? visibleRun.substr(p)
                                 : visibleRun.substr(p, nextSpace - p + 1);
          p = (nextSpace == std::string::npos) ? visibleRun.size()
                                               : (nextSpace + 1);

          const SDL_FPoint measured =
              fontUi.measureSized(word, 2000.0f, fontSize);
          if (penX > startX && penX + measured.x > maxX) {
            penX = startX;
            penY += lineStep;
          }
          if (win.shadow) {
            fontUi.textSized(renderer, word, penX + 1.0f, penY + 1.0f, 2000.0f,
                             fontSize, SDL_Color{0, 0, 0, 255});
          }
          fontUi.textSized(renderer, word, penX, penY, 2000.0f, fontSize, fg);
          penX += measured.x;
        }
      }

      // Render `select` options inside the text window if in WaitSelect
      if (vm_.waitState() == WaitState::WaitSelect) {
        if (penX > startX) {
          penX = startX;
          penY += lineStep;
        }
        auto &fontUi = pickFont(false);
        for (std::size_t i = 0; i < vm_.selectOptions().size(); ++i) {
          const auto &opt = vm_.selectOptions()[i];
          const std::string optDisplay = impl_->translateLine(opt.text);
          const SDL_FRect optRect{
              startX, penY, static_cast<float>(win.textAreaWidth()), lineStep};
          const bool hovered =
              (vm_.mouseX() >= optRect.x &&
               vm_.mouseX() < optRect.x + optRect.w &&
               vm_.mouseY() >= optRect.y && vm_.mouseY() >= optRect.y &&
               vm_.mouseY() < optRect.y + optRect.h);
          const std::uint32_t col =
              hovered ? vm_.selectHoverColor() : vm_.selectIdleColor();
          if (win.shadow) {
            fontUi.textSized(renderer, optDisplay, startX + 1.0f, penY + 1.0f,
                             optRect.w, fontSize, SDL_Color{0, 0, 0, 255});
          }
          fontUi.textSized(renderer, optDisplay, startX, penY, optRect.w,
                           fontSize, rgb24ToSdl(col, 255));
          penY += lineStep;
        }
      }

      // Render animated wait cursor (`cur00.png` for `@`, `cur01.png` for `\`)
      if (!vm_.isTypewriterActive() &&
          (vm_.waitState() == WaitState::WaitClickInline ||
           vm_.waitState() == WaitState::WaitClickPage)) {
        const int curIdx =
            (vm_.waitState() == WaitState::WaitClickInline) ? 0 : 1;
        const auto &cur = vm_.cursors()[curIdx];
        if (cur.configured && !cur.spec.imagePath.empty()) {
          if (SDL_Texture *tex = impl_->getOrLoadTexture(renderer, cur.spec)) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(tex, &tw, &th);
            const auto *processed = impl_->getProcessedImage(cur.spec);
            const float logicalW =
                processed ? static_cast<float>(processed->width) : tw;
            const float logicalH =
                processed ? static_cast<float>(processed->height) : th;
            const int cells = std::max(1, cur.spec.numCells);
            const float cellW = tw / static_cast<float>(cells);
            const float logicalCellW = logicalW / static_cast<float>(cells);
            const int cIdx = std::clamp(cur.currentCell, 0, cells - 1);
            const float cx = cur.absolute ? static_cast<float>(cur.offsetX)
                                          : (penX + 2.0f + cur.offsetX);
            const float cy = cur.absolute ? static_cast<float>(cur.offsetY)
                                          : (penY + cur.offsetY);
            SDL_SetTextureAlphaMod(tex, 255);
            SDL_FRect src{cIdx * cellW, 0.0f, cellW, th};
            SDL_FRect dst{cx, cy, logicalCellW, logicalH};
            SDL_RenderTexture(&renderer, tex, &src, &dst);
          }
        }
      }
    }
  };

  // Retained manual drawing commands operate on an independent framebuffer.
  // The VM publishes them only on draw; drafts do not appear before commit.
  // Replaying the immutable snapshot uses GPU textures and the shared UI font.
  auto renderManualFrame = [&]() {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    for (const ManualDrawOp &op : vm_.manualDrawFrame()) {
      if (op.kind == ManualDrawOp::Kind::Clear ||
          op.kind == ManualDrawOp::Kind::Fill) {
        const SDL_Color color = rgb24ToSdl(op.colorRgb);
        SDL_SetRenderDrawColor(&renderer, color.r, color.g, color.b, 255);
        SDL_RenderClear(&renderer);
        continue;
      }
      if (op.kind == ManualDrawOp::Kind::Text) {
        if (op.text.empty())
          continue;
        pickFont(false).textSized(
            renderer, impl_->translateLine(op.text),
            static_cast<float>(op.x), static_cast<float>(op.y),
            static_cast<float>(std::max(1, op.width)),
            static_cast<float>(std::max(1, op.height)),
            rgb24ToSdl(op.colorRgb, 255));
        continue;
      }

      const bool background = op.kind == ManualDrawOp::Kind::Background;
      if (op.spec.trans == TransMode::DirectColor) {
        const SDL_Color color = rgb24ToSdl(op.spec.color);
        SDL_SetRenderDrawColor(&renderer, color.r, color.g, color.b,
                               static_cast<Uint8>(op.alpha));
        SDL_FRect rect{static_cast<float>(op.x), static_cast<float>(op.y),
                       background ? viewW : static_cast<float>(op.width),
                       background ? viewH : static_cast<float>(op.height)};
        SDL_RenderFillRect(&renderer, &rect);
        continue;
      }
      if (op.spec.trans == TransMode::String) {
        if (op.spec.text.empty())
          continue;
        pickFont(false).textSized(
            renderer, impl_->translateLine(op.spec.text),
            static_cast<float>(op.x), static_cast<float>(op.y),
            static_cast<float>(std::max(1, op.width > 0 ? op.width : 2000)),
            static_cast<float>(std::max(1, op.spec.fontHeight)),
            rgb24ToSdl(op.spec.color, static_cast<Uint8>(op.alpha)));
        continue;
      }
      if (op.spec.imagePath.empty())
        continue;
      SDL_Texture *texture = impl_->getOrLoadTexture(renderer, op.spec);
      if (!texture)
        continue;
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(op.alpha));
      float textureW = 0.0f, textureH = 0.0f;
      SDL_GetTextureSize(texture, &textureW, &textureH);
      const auto *processed = impl_->getProcessedImage(op.spec);
      const float logicalW =
          processed ? static_cast<float>(processed->width) : textureW;
      const float logicalH =
          processed ? static_cast<float>(processed->height) : textureH;
      const int cells = background ? 1 : std::max(1, op.spec.numCells);
      const float cellW = textureW / cells;
      const float logicalCellW = logicalW / cells;
      const int cell = std::clamp(op.cell, 0, cells - 1);
      SDL_FRect source{cell * cellW, 0.0f, cellW, textureH};
      if (op.kind == ManualDrawOp::Kind::Blit) {
        source = SDL_FRect{static_cast<float>(op.srcX),
                           static_cast<float>(op.srcY),
                           static_cast<float>(op.srcW),
                           static_cast<float>(op.srcH)};
      }
      float width = background ? viewW : logicalCellW;
      float height = background ? viewH : logicalH;
      if (op.kind == ManualDrawOp::Kind::Blit) {
        width = static_cast<float>(op.width);
        height = static_cast<float>(op.height);
      }
      if (op.affine) {
        const float halfW = width * 0.5f, halfH = height * 0.5f;
        const SDL_FPoint corners[] = {{-halfW, -halfH}, {halfW, -halfH},
                                      {halfW, halfH}, {-halfW, halfH}};
        const float matrixA = op.transformA / 1000.0f;
        const float matrixB = op.transformB / 1000.0f;
        const float matrixC = op.transformC / 1000.0f;
        const float matrixD = op.transformD / 1000.0f;
        const float left = source.x / textureW;
        const float right = (source.x + source.w) / textureW;
        const float top = source.y / textureH;
        const float bottom = (source.y + source.h) / textureH;
        const SDL_FPoint uvs[] = {{left, top}, {right, top},
                                  {right, bottom}, {left, bottom}};
        SDL_Vertex vertices[4]{};
        for (int i = 0; i < 4; ++i) {
          vertices[i].position = SDL_FPoint{
              static_cast<float>(op.x) + corners[i].x * matrixA +
                  corners[i].y * matrixB,
              static_cast<float>(op.y) + corners[i].x * matrixC +
                  corners[i].y * matrixD};
          vertices[i].tex_coord = uvs[i];
          vertices[i].color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};
        }
        const int indices[] = {0, 1, 2, 0, 2, 3};
        SDL_SetTextureAlphaMod(texture, 255);
        for (auto &vertex : vertices)
          vertex.color.a = static_cast<float>(op.alpha) / 255.0f;
        SDL_RenderGeometry(&renderer, texture, vertices, 4, indices, 6);
      } else {
        width *= std::abs(op.scaleX) / 100.0f;
        height *= std::abs(op.scaleY) / 100.0f;
        if (width < 0.01f || height < 0.01f)
          continue;
        SDL_FRect dest{static_cast<float>(op.x), static_cast<float>(op.y),
                       width, height};
        if (op.rotation != 0 || op.scaleX < 0 || op.scaleY < 0) {
          const SDL_FlipMode flip = static_cast<SDL_FlipMode>(
              (op.scaleX < 0 ? SDL_FLIP_HORIZONTAL : 0) |
              (op.scaleY < 0 ? SDL_FLIP_VERTICAL : 0));
          SDL_RenderTextureRotated(&renderer, texture, &source, &dest,
                                   static_cast<double>(op.rotation), nullptr,
                                   flip);
        } else {
          SDL_RenderTexture(&renderer, texture, &source, &dest);
        }
      }
    }
  };

  // Apply a short-lived directional screen shake to the game layers only.
  // Native modal overlays retain stable input coordinates and do not shake.
  const auto shake = vm_.quakeOffset();
  SDL_Rect originalViewport{};
  const bool shaking = shake.first != 0 || shake.second != 0;
  if (shaking) {
    SDL_GetRenderViewport(&renderer, &originalViewport);
    SDL_Rect shifted = originalViewport;
    shifted.x += shake.first;
    shifted.y += shake.second;
    SDL_SetRenderViewport(&renderer, &shifted);
  }

  if (vm_.hasManualDrawFrame())
    renderManualFrame();
  else
    renderSceneContents();

  if (shaking)
    SDL_SetRenderViewport(&renderer, &originalViewport);

  // SDL3's high-level renderer cannot bind a custom fragment shader. Process
  // only when these optional legacy effects are enabled; text and sprites are
  // still rasterized by the accelerated UI before this readback. Native modal
  // overlays draw later and intentionally do not inherit game color effects.
  if (vm_.isMonochromeEnabled() || vm_.negativeMode() != 0) {
    Kotonoha::SurfacePtr readback(SDL_RenderReadPixels(&renderer, nullptr));
    if (readback) {
      Kotonoha::SurfacePtr rgba(
          SDL_ConvertSurface(readback.get(), SDL_PIXELFORMAT_RGBA32));
      if (rgba && rgba->w > 0 && rgba->h > 0 &&
          SDL_LockSurface(rgba.get())) {
        applyScenePixelEffects(
            static_cast<std::uint8_t *>(rgba->pixels), rgba->w, rgba->h,
            rgba->pitch, vm_.isMonochromeEnabled(), vm_.monochromeColor(),
            vm_.negativeMode());
        SDL_UnlockSurface(rgba.get());
        if (!impl_->postProcessTexture ||
            impl_->postProcessWidth != rgba->w ||
            impl_->postProcessHeight != rgba->h) {
          impl_->postProcessTexture.reset(SDL_CreateTexture(
              &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
              rgba->w, rgba->h));
          impl_->postProcessWidth = rgba->w;
          impl_->postProcessHeight = rgba->h;
        }
        if (impl_->postProcessTexture) {
          SDL_Texture *texture = impl_->postProcessTexture.get();
          if (SDL_UpdateTexture(texture, nullptr, rgba->pixels, rgba->pitch)) {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            SDL_FRect fill{0.0f, 0.0f, viewW, viewH};
            SDL_RenderTexture(&renderer, texture, nullptr, &fill);
          }
        }
      }
    }
  }

  // 7. PONscripter-native modal overlays. Shared menu/backlog components are
  // injected below as PonscripterMenu/PonscripterBacklog overrides.
  const bool nativeSharedModal =
      ((impl_->backlog && impl_->backlog->isVisible()) ||
       (impl_->inGameMenu && impl_->inGameMenu->IsOpen()));
  if (vm_.waitState() == WaitState::WaitSystemCall && !nativeSharedModal) {
    const std::string &kind = vm_.activeSystemCall();
    auto &fontUi = pickFont(false);

    if (kind == "rmenu") {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 165);
      SDL_FRect full{0.0f, 0.0f, viewW, viewH};
      SDL_RenderFillRect(&renderer, &full);

      const float itemH = 30.0f;
      const float menuW = 340.0f;
      const float menuH =
          static_cast<float>(vm_.rmenuEntries().size()) * itemH + 32.0f;
      const float menuX = (viewW - menuW) * 0.5f;
      const float menuY = (viewH - menuH) * 0.5f;

      SDL_SetRenderDrawColor(&renderer, 18, 22, 32, 235);
      SDL_FRect panel{menuX, menuY, menuW, menuH};
      SDL_RenderFillRect(&renderer, &panel);
      SDL_SetRenderDrawColor(&renderer, 160, 175, 195, 210);
      SDL_RenderRect(&renderer, &panel);

      float y = menuY + 16.0f;
      for (std::size_t i = 0; i < vm_.rmenuEntries().size(); ++i) {
        const auto &entry = vm_.rmenuEntries()[i];
        if (!entry.command.empty() && !entry.label.empty()) {
          const bool hovered =
              (vm_.mouseX() >= menuX && vm_.mouseX() < menuX + menuW &&
               vm_.mouseY() >= y && vm_.mouseY() < y + itemH);
          const std::uint32_t col =
              hovered ? vm_.rmenuHoverColor() : vm_.rmenuIdleColor();
          fontUi.textSized(renderer, entry.label, menuX + 24.0f, y + 4.0f,
                           menuW - 48.0f, 19.0f, rgb24ToSdl(col, 255));
        }
        y += itemH;
      }
    } else if (kind == "save" || kind == "load") {
      // Draw a semi-transparent dark backdrop if *saya_save / *saya_load didn't
      // already load sprite 8 (`data\f05.png`)
      if (!vm_.sprites()[8].loaded || !vm_.sprites()[8].visible) {
        SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(&renderer, 12, 16, 24, 225);
        SDL_FRect box{40.0f, 50.0f, 720.0f, 500.0f};
        SDL_RenderFillRect(&renderer, &box);
        SDL_SetRenderDrawColor(&renderer, 140, 160, 190, 200);
        SDL_RenderRect(&renderer, &box);
      }

      const bool isSave = (kind == "save");
      const std::string &title =
          isSave ? vm_.saveMenuTitle() : vm_.loadMenuTitle();
      fontUi.textSized(renderer, title, 75.0f, 64.0f, 420.0f, 22.0f,
                       SDL_Color{255, 230, 140, 255});
      fontUi.textSized(renderer, "[Clique direito / ESC: Voltar]", 500.0f,
                       68.0f, 240.0f, 15.0f, SDL_Color{200, 210, 225, 255});

      const int totalSlots = vm_.saveNumberCount();
      const float startY = 104.0f;
      const float rowH = 41.0f;
      for (int i = 0; i < std::min(totalSlots, 20); ++i) {
        const int slotNo = i + 1;
        const int col = i / 10;
        const int row = i % 10;
        const float sx = 68.0f + col * 336.0f;
        const float sy = startY + row * rowH;
        const SDL_FRect slotRect{sx, sy, 320.0f, rowH - 5.0f};
        const bool hovered = (vm_.mouseX() >= slotRect.x &&
                              vm_.mouseX() < slotRect.x + slotRect.w &&
                              vm_.mouseY() >= slotRect.y &&
                              vm_.mouseY() < slotRect.y + slotRect.h);

        SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(&renderer, hovered ? 60 : 24, hovered ? 40 : 28,
                               hovered ? 50 : 38, hovered ? 210 : 155);
        SDL_RenderFillRect(&renderer, &slotRect);
        SDL_SetRenderDrawColor(&renderer, hovered ? 255 : 95,
                               hovered ? 155 : 105, hovered ? 155 : 125, 190);
        SDL_RenderRect(&renderer, &slotRect);

        const SaveTimestamp ts = impl_->querySaveSlot(slotNo);
        char labelBuf[160];
        if (ts.exists) {
          std::snprintf(
              labelBuf, sizeof(labelBuf), "%s%02d  %02d/%02d %02d:%02d  [%s]",
              vm_.saveSlotPrefix().c_str(), slotNo, ts.day, ts.month, ts.hour,
              ts.minute, ts.title.empty() ? "Cena" : ts.title.c_str());
        } else {
          std::snprintf(labelBuf, sizeof(labelBuf), "%s%02d  ---- Vazio ----",
                        vm_.saveSlotPrefix().c_str(), slotNo);
        }
        const std::uint32_t colorRgb =
            hovered ? vm_.rmenuHoverColor()
                    : (ts.exists ? vm_.rmenuIdleColor() : 0xBBBBBBu);
        fontUi.textSized(renderer, labelBuf, sx + 10.0f, sy + 8.0f, 300.0f,
                         16.0f, rgb24ToSdl(colorRgb, 255));
      }
    } else if (kind == "lookback") {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 12, 16, 22, 230);
      SDL_FRect box{40.0f, 50.0f, 720.0f, 500.0f};
      SDL_RenderFillRect(&renderer, &box);
      SDL_SetRenderDrawColor(&renderer, 150, 165, 190, 200);
      SDL_RenderRect(&renderer, &box);

      const auto &pages = vm_.backlogPages();
      fontUi.textSized(
          renderer,
          "Histórico de mensagens (" +
              std::to_string(pages.empty() ? 0
                                           : impl_->backlogScrollOffset + 1) +
              "/" + std::to_string(pages.size()) +
              ") — Roda do mouse / Setas | Clique direito: Fechar",
          60.0f, 62.0f, 680.0f, 16.0f, SDL_Color{255, 220, 130, 255});
      if (!pages.empty()) {
        const int idx = std::clamp(impl_->backlogScrollOffset, 0,
                                   static_cast<int>(pages.size()) - 1);
        fontUi.textSized(renderer, pages[idx], 60.0f, 96.0f, 680.0f, 19.0f,
                         SDL_Color{235, 235, 240, 255});
      } else {
        fontUi.textSized(renderer, "Nenhuma mensagem no histórico ainda.",
                         60.0f, 96.0f, 680.0f, 19.0f,
                         SDL_Color{170, 175, 185, 255});
      }
    }
  }

  // Toast status banner (e.g. "Salvo no Slot #1" / "Quicksave (F5)")
  if (!impl_->statusBannerText.empty() &&
      SDL_GetTicks() < impl_->statusBannerUntilTick) {
    auto &fontUi = pickFont(false);
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(&renderer, 20, 32, 48, 225);
    SDL_FRect toast{20.0f, 16.0f, 360.0f, 34.0f};
    SDL_RenderFillRect(&renderer, &toast);
    SDL_SetRenderDrawColor(&renderer, 120, 210, 255, 230);
    SDL_RenderRect(&renderer, &toast);
    fontUi.textSized(renderer, impl_->statusBannerText, 32.0f, 23.0f, 336.0f,
                     16.0f, SDL_Color{240, 250, 255, 255});
  }

  // Render active PONscripter shared-component overrides (PonscripterBacklog,
  // PonscripterMenu).
  Runtime::renderGameplay(renderer, scale);
}

bool PonscripterRuntime::onEvent(Kotonoha::EventRef rawEvent) {
  if (vm_.waitState() == WaitState::WaitVideo && impl_->videoPlayer) {
    if (impl_->videoSkip.accept(rawEvent) && impl_->videoCanCancel) {
      impl_->videoPlayer->stop();
      impl_->videoPlayer.reset();
      vm_.finishVideo();
    }
    return true;
  }
  // 1. PONscripter's native shared-component overrides handle open modals.
  if ((impl_->backlog && impl_->backlog->isVisible()) ||
      (impl_->inGameMenu && impl_->inGameMenu->IsOpen())) {
    return Runtime::onEvent(rawEvent);
  }

  const SDL_Event event = Kotonoha::MapControllerEvent(rawEvent);

  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    vm_.updateMousePos(static_cast<int>(std::lround(event.motion.x)),
                       static_cast<int>(std::lround(event.motion.y)));
    return false;
  }

  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    const int mx = static_cast<int>(std::lround(event.button.x));
    const int my = static_cast<int>(std::lround(event.button.y));
    vm_.updateMousePos(mx, my);

    if (event.button.button == SDL_BUTTON_RIGHT) {
      return vm_.clickAt(mx, my, true);
    }

    if (event.button.button == SDL_BUTTON_LEFT) {
      if (vm_.waitState() == WaitState::WaitSystemCall) {
        const std::string kind = vm_.activeSystemCall();
        const auto res = vm_.resolution();
        const float viewW = static_cast<float>(res.width);
        const float viewH = static_cast<float>(res.height);

        if (kind == "rmenu") {
          const float itemH = 30.0f;
          const float menuW = 340.0f;
          const float menuH =
              static_cast<float>(vm_.rmenuEntries().size()) * itemH + 32.0f;
          const float menuX = (viewW - menuW) * 0.5f;
          const float menuY = (viewH - menuH) * 0.5f;
          float y = menuY + 16.0f;
          for (const auto &entry : vm_.rmenuEntries()) {
            if (!entry.command.empty() && mx >= menuX && mx < menuX + menuW &&
                my >= y && my < y + itemH) {
              if (entry.command == "reset") {
                vm_.resetToStart();
                return true;
              }
              if (entry.command == "end") {
                vm_.enterSystemCall("end");
                return true;
              }
              if (entry.command == "save" || entry.command == "load" ||
                  entry.command == "lookback") {
                // Transition directly from rmenu into the selected modal
                // (`save`, `load`, or `lookback`) while preserving
                // `preSystemCallWaitState_`!
                vm_.enterSystemCall(entry.command);
                return true;
              }
              if (entry.command == "windowerase") {
                vm_.finishSystemCall();
                return true;
              }
              vm_.finishSystemCall();
              return true;
            }
            y += itemH;
          }
          // Clicking outside the rmenu box closes rmenu
          if (mx < menuX || mx > menuX + menuW || my < menuY ||
              my > menuY + menuH) {
            vm_.finishSystemCall();
          }
          return true;
        }

        if (kind == "save" || kind == "load") {
          const bool isSave = (kind == "save");
          const int totalSlots = vm_.saveNumberCount();
          const float startY = 104.0f;
          const float rowH = 41.0f;
          for (int i = 0; i < std::min(totalSlots, 20); ++i) {
            const int slotNo = i + 1;
            const int col = i / 10;
            const int row = i % 10;
            const float sx = 68.0f + col * 336.0f;
            const float sy = startY + row * rowH;
            if (mx >= sx && mx < sx + 320.0f && my >= sy &&
                my < sy + rowH - 5.0f) {
              if (isSave) {
                if (impl_->onSaveSlot(slotNo, "")) {
                  vm_.finishSystemCall();
                }
              } else {
                if (impl_->onLoadSlot(slotNo)) {
                  vm_.finishSystemCall();
                }
              }
              return true;
            }
          }
          return true;
        }

        if (kind == "lookback") {
          vm_.finishSystemCall();
          return true;
        }
      }

      if (vm_.waitState() == WaitState::WaitSelect) {
        const auto &win = vm_.windowSpec();
        const float startX = static_cast<float>(win.textX);
        const float lineStep = static_cast<float>(win.fontH + win.pitchY);
        float penY = static_cast<float>(win.textY);
        for (const auto &span : vm_.currentPageSpans()) {
          if (span.isLineBreak)
            penY += lineStep;
        }
        for (std::size_t i = 0; i < vm_.selectOptions().size(); ++i) {
          if (mx >= startX &&
              mx < startX + static_cast<float>(win.textAreaWidth()) &&
              my >= penY && my < penY + lineStep) {
            return vm_.selectChoice(static_cast<int>(i));
          }
          penY += lineStep;
        }
        return false;
      }

      return vm_.clickAt(mx, my, false);
    }
  }

  if (event.type == SDL_EVENT_MOUSE_WHEEL &&
      vm_.waitState() == WaitState::WaitSystemCall &&
      vm_.activeSystemCall() == "lookback") {
    const auto &pages = vm_.backlogPages();
    if (!pages.empty()) {
      if (event.wheel.y > 0)
        impl_->backlogScrollOffset =
            std::max(0, impl_->backlogScrollOffset - 1);
      else if (event.wheel.y < 0)
        impl_->backlogScrollOffset = std::min(
            static_cast<int>(pages.size()) - 1, impl_->backlogScrollOffset + 1);
    }
    return true;
  }

  if (event.type == SDL_EVENT_KEY_DOWN) {
    if (!event.key.repeat) {
      // F5: Quicksave into Slot #1 + Kotonoha quicksave.vn
      if (event.key.key == SDLK_F5) {
        impl_->onSaveSlot(1, "");
        saveGame((impl_->saveRootDir() / "quicksave.vn").string());
        return true;
      }
      // F6: Quickload from Slot #1 or Kotonoha quicksave.vn
      if (event.key.key == SDLK_F6) {
        if (!impl_->onLoadSlot(1)) {
          const auto qpath = impl_->saveRootDir() / "quicksave.vn";
          if (std::filesystem::exists(qpath))
            loadGame(qpath.string());
        }
        return true;
      }
      // B / PageUp (outside lookback): Open PONscripter VNBacklog (with voice replay!)
      if ((event.key.key == SDLK_B ||
           (event.key.key == SDLK_PAGEUP &&
            !(vm_.waitState() == WaitState::WaitSystemCall &&
              vm_.activeSystemCall() == "lookback"))) &&
          impl_->backlog) {
        impl_->backlog->toggle();
        return true;
      }
      // F2/F3: Open the PONscripter-native save/load system calls.
      if (event.key.key == SDLK_F2) {
        vm_.enterSystemCall("save");
        return true;
      }
      if (event.key.key == SDLK_F3) {
        vm_.enterSystemCall("load");
        return true;
      }
    }

    if (vm_.waitState() == WaitState::WaitSystemCall &&
        vm_.activeSystemCall() == "lookback") {
      const auto &pages = vm_.backlogPages();
      if (!pages.empty()) {
        if (event.key.key == SDLK_UP || event.key.key == SDLK_PAGEUP) {
          impl_->backlogScrollOffset =
              std::max(0, impl_->backlogScrollOffset - 1);
          return true;
        }
        if (event.key.key == SDLK_DOWN || event.key.key == SDLK_PAGEDOWN) {
          impl_->backlogScrollOffset =
              std::min(static_cast<int>(pages.size()) - 1,
                       impl_->backlogScrollOffset + 1);
          return true;
        }
      }
    }

    if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
      vm_.setSkipMode(true);
      return true;
    }
    if (event.key.key == SDLK_S && !event.key.repeat) {
      vm_.setSkipMode(!vm_.isSkipMode());
      return true;
    }
    if (event.key.key == SDLK_A && !event.key.repeat) {
      vm_.setAutoMode(!vm_.isAutoMode());
      return true;
    }
    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK) {
      if (vm_.rightClick())
        return true;
      return Runtime::onEvent(rawEvent);
    }
    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
      return handleAdvanceAction();
    }
  }

  if (event.type == SDL_EVENT_KEY_UP) {
    if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL) {
      vm_.setSkipMode(false);
      return true;
    }
  }

  return Runtime::onEvent(rawEvent);
}

bool PonscripterRuntime::handleAdvanceAction() {
  if (vm_.waitState() == WaitState::WaitSelect &&
      !vm_.selectOptions().empty()) {
    return vm_.selectChoice(0);
  }
  return vm_.advance();
}

void PonscripterRuntime::saveGame(const std::string &path) {
  const SlotSaveData data = vm_.exportSlotSave();
  const std::string payload = SaveSerializer::serializeSlot(data);
  if (impl_->engine) {
    Kotonoha::VNSave save;
    save.nodeId = std::max(0, translationRoute());
    save.gameData = payload;
    save.write(path);
  } else {
    std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
    std::string err;
    if (!Kotonoha::Filesystem::writeBinaryFileAtomically(
            std::filesystem::path(path), bytes, &err)) {
      throw std::runtime_error("PONscripter save failed: " + err);
    }
  }
  impl_->onPersistGlobals();
  impl_->showBanner("Jogo salvo com sucesso");
}

void PonscripterRuntime::loadGame(const std::string &path) {
  std::string payload;
  try {
    const auto vnSave = Kotonoha::VNSave::read(path);
    payload = vnSave.gameData;
  } catch (...) {
    const auto bytes = Kotonoha::Filesystem::readLocalFile(path);
    payload.assign(bytes.begin(), bytes.end());
  }
  SlotSaveData data;
  if (!SaveSerializer::deserializeSlot(payload, data)) {
    throw std::runtime_error("Arquivo de save PONscripter inválido");
  }
  vm_.importSlotSave(data);
  impl_->showBanner("Jogo carregado com sucesso");
}

std::string PonscripterRuntime::debugNode() const {
  return std::to_string(translationRoute());
}

std::vector<Kotonoha::DebugNode> PonscripterRuntime::debugNodes() const {
  impl_->ensureScriptIndex();
  std::vector<Kotonoha::DebugNode> nodes;
  nodes.reserve(impl_->sections.size());
  for (const auto &sec : impl_->sections) {
    nodes.push_back({std::to_string(sec.routeId), "*" + sec.label, sec.assets});
  }
  return nodes;
}

void PonscripterRuntime::debugJumpToNode(const std::string &id) {
  impl_->ensureScriptIndex();
  // Accept either routeId integer string ("4141") or label name ("*010" /
  // "010")
  int routeId = -1;
  try {
    std::size_t pos = 0;
    const int parsed = std::stoi(id, &pos);
    if (pos == id.size())
      routeId = parsed;
  } catch (...) {
  }

  if (routeId >= 0) {
    const auto it = impl_->routeIdToSectionIndex.find(routeId);
    if (it != impl_->routeIdToSectionIndex.end()) {
      const auto &sec = impl_->sections[it->second];
      vm_.clearCurrentPage(false);
      if (vm_.jumpToLabel(sec.label)) {
        vm_.tick(16.0);
        return;
      }
    }
  }

  if (vm_.jumpToLabel(id)) {
    vm_.tick(16.0);
    return;
  }
  throw std::invalid_argument("Unknown PONscripter node/label: " + id);
}

void PonscripterRuntime::debugRestartScene() {
  const std::string lbl = vm_.currentLabel();
  if (lbl.empty() || !vm_.jumpToLabel(lbl))
    throw std::runtime_error("No active PONscripter label to restart");
  vm_.tick(16.0);
}

std::string PonscripterRuntime::dialogueText() const {
  return vm_.currentPagePlainText();
}

std::string PonscripterRuntime::dialogueSpeaker() const {
  if (!vm_.lastVoicePath().empty())
    return inferCharacterFromVoicePath(vm_.lastVoicePath());
  return {};
}

int PonscripterRuntime::translationRoute() const {
  impl_->ensureScriptIndex();
  const auto it = impl_->labelToRouteId.find(vm_.currentLabel());
  if (it != impl_->labelToRouteId.end())
    return it->second;
  return static_cast<int>(vm_.currentSectionId());
}

std::vector<int> PonscripterRuntime::translationRoutes() const {
  impl_->ensureScriptIndex();
  std::vector<int> routes;
  routes.reserve(impl_->sections.size());
  for (const auto &sec : impl_->sections) {
    if (!sec.texts.empty())
      routes.push_back(sec.routeId);
  }
  return routes;
}

Kotonoha::Translation::SourceTexts
PonscripterRuntime::translationSources() const {
  impl_->ensureScriptIndex();
  return impl_->cachedTranslationSources;
}

std::vector<std::string> PonscripterRuntime::activeTranslationKeys() const {
  std::vector<std::string> keys = vm_.currentPageLineKeys();
  if (vm_.waitState() == WaitState::WaitSelect) {
    for (const auto &opt : vm_.selectOptions()) {
      if (!opt.text.empty())
        keys.push_back(opt.text);
    }
  }
  for (const auto &sp : vm_.sprites()) {
    if (sp.loaded && sp.visible && sp.spec.trans == TransMode::String &&
        !sp.spec.text.empty()) {
      keys.push_back(sp.spec.text);
    }
  }
  return keys;
}

std::vector<Kotonoha::DebugAudioAsset> PonscripterRuntime::debugSceneAudio() {
  impl_->ensureScriptIndex();
  std::vector<Kotonoha::DebugAudioAsset> out;
  const int routeId = translationRoute();
  const auto it = impl_->routeIdToSectionIndex.find(routeId);
  if (it != impl_->routeIdToSectionIndex.end()) {
    out = impl_->sections[it->second].audioLines;
  }

  // Also ensure the currently active voice / SE / BGM are present at the top
  auto addIfMissing = [&](const std::string &rawPath,
                          const std::string &character,
                          const std::string &lineText) {
    if (rawPath.empty())
      return;
    const std::string resolved =
        assets_ ? assets_->resolveActualPath(rawPath) : rawPath;
    const std::string id = !resolved.empty() ? resolved : rawPath;
    if (std::none_of(out.begin(), out.end(),
                     [&](const auto &a) { return a.id == id; })) {
      out.insert(out.begin(), {id, character, lineText});
    }
  };

  if (!vm_.lastVoicePath().empty()) {
    const std::string lastLine = !vm_.currentPageLineKeys().empty()
                                     ? vm_.currentPageLineKeys().back()
                                     : vm_.currentPagePlainText();
    addIfMissing(vm_.lastVoicePath(),
                 inferCharacterFromVoicePath(vm_.lastVoicePath()), lastLine);
  }
  if (!vm_.lastSePath().empty()) {
    addIfMissing(vm_.lastSePath(), "SFX", "");
  }
  if (!vm_.activeBgm().empty()) {
    addIfMissing(vm_.activeBgm(), "BGM", "");
  }
  return out;
}

std::optional<Kotonoha::AudioClip>
PonscripterRuntime::debugOriginalAudio(const std::string &id) {
  if (!assets_ || !assets_->contains(id, Kotonoha::AssetSource::Original))
    return std::nullopt;
  return assets_->loadAudio(id, Kotonoha::AssetSource::Original);
}

void PonscripterRuntime::debugOverlayAssetChanged(const std::string &id) {
  impl_->imageCache.clear();
  impl_->dimCache.clear();
  if (assets_)
    assets_->clearCache();
  // Replay voice immediately if the user just dubbed or discarded the current
  // voice clip
  if (!id.empty() && !vm_.lastVoicePath().empty()) {
    const std::string normId = ArchiveReader::normalizePath(id);
    const std::string normVoice =
        ArchiveReader::normalizePath(vm_.lastVoicePath());
    if (normId == normVoice) {
      impl_->onPlayWave(0, vm_.lastVoicePath(), false);
    }
  }
}

} // namespace Ponscripter
