#include <Kotonoha/utils/Log.h>
#include <Kotonoha/Init.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfDebugGui.hpp>
#include <Kotonoha/routing/SchoolDaysBackLog.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysInputPolicy.hpp>
#include <Kotonoha/routing/KtrfSceneTransaction.hpp>
#include <Kotonoha/routing/SchoolDaysSaveProfile.hpp>
#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>
#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>
#include <Kotonoha/routing/SchoolDaysCommentDialog.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>
#include <Kotonoha/routing/SchoolDaysDisplayPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysEpisodeOverlay.hpp>
#include <Kotonoha/routing/SchoolDaysFrontendTransition.hpp>
#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysPlayDataSession.hpp>
#include <Kotonoha/routing/SchoolDaysPlaybackPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysReplayPlaybackSession.hpp>
#include <Kotonoha/routing/SchoolDaysSystemUi.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>
#include <Kotonoha/routing/SchoolDaysTitleProfile.hpp>
#include <Kotonoha/Main.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/Display.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/GameplayFlow.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/specials/Upscaling.h>
#include <Kotonoha/utils/OverlayRender.h>
extern "C" {
#include <Kotonoha/renders/ImageRender.h>
}
#include <SDL3_ttf/SDL_ttf.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <memory>
#include <limits>
#include <mutex>
#include <string>

using Kotonoha::global;

namespace {

struct KtrfLaunchOptions {
  bool requested = false;
  bool valid = true;
  bool uiVariantSpecified = false;
  bool assetsRootSpecified = false;
  bool gpkRootSpecified = false;
  bool scriptDirSpecified = false;
  std::string scriptDir;
  std::string routePath;
  std::string orsRoot;
  std::string assetsRoot;
  std::string gpkRoot;
  Kotonoha::SchoolDaysSaveFeelModel::Variant uiVariant =
      Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
};

std::unique_ptr<Kotonoha::SchoolDaysKtrfAppController> ktrfController;
std::unique_ptr<Kotonoha::KtrfSceneTransaction> ktrfSceneTransaction;
std::unique_ptr<Kotonoha::KtrfSceneTransaction> ktrfReplaySceneTransaction;
std::unique_ptr<Kotonoha::SchoolDaysSystemUi> ktrfSystemUi;
std::unique_ptr<Kotonoha::SchoolDaysMenuBarPresenter> ktrfMenuBar;
std::unique_ptr<Kotonoha::SchoolDaysBackLogModel> ktrfBackLog;
std::unique_ptr<Kotonoha::SchoolDaysBackLogPresenter> ktrfBackLogPresenter;
Kotonoha::SchoolDaysBackLogStore ktrfBackLogStore;
std::mutex ktrfBackLogMutex;
int ktrfBackLogHistoryIndex = 0;
std::string ktrfBackLogLastSceneKey;
std::unique_ptr<Kotonoha::SchoolDaysPlayDataSession> ktrfPlayDataPlayback;
std::unique_ptr<Kotonoha::SchoolDaysReplayPlaybackSession> ktrfReplayPlayback;
std::unique_ptr<Kotonoha::SchoolDaysSaveStorage> ktrfSaveStorage;
std::unique_ptr<Kotonoha::SchoolDaysConfigStorage> ktrfConfigStorage;
std::unique_ptr<Kotonoha::Audio> ktrfTitleAudio;
Kotonoha_time *ktrfTitleAudioClock = nullptr;
Kotonoha_audioDecode *ktrfTitleBgm = nullptr;
enum class KtrfFrontendBgmOwner : uint8_t { None, Title, System };
KtrfFrontendBgmOwner ktrfFrontendBgmOwner = KtrfFrontendBgmOwner::None;
Kotonoha::SchoolDaysSaveProfile ktrfSaveProfile;
bool ktrfUiVariantSpecified = false;
Kotonoha::SchoolDaysSaveFeelModel::Variant ktrfUiVariant =
    Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
Kotonoha::SchoolDaysDisplayPolicy::RetailState ktrfRetailDisplayState{};
Kotonoha::SchoolDaysDisplayPolicy::RetailBackendState
    ktrfRetailDisplayBackendState{};
Kotonoha::SchoolDaysRetailConfigPolicy::State ktrfRetailConfigState =
    Kotonoha::SchoolDaysRetailConfigPolicy::Defaults();
int ktrfSchoolDaysSpeedIndex = 0;
std::string ktrfAssetsRoot;
int ktrfSaveLoadMetadataPage = -1;
Kotonoha::SchoolDaysConfigMenuModel::Page ktrfConfigPage =
    Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
bool ktrfTitleReplayActive = false;
enum class KtrfReplayHostModal : uint8_t {
  None,
  Save,
  Load,
  Config,
  MainMenuConfirm,
};
KtrfReplayHostModal ktrfReplayHostModal = KtrfReplayHostModal::None;
enum class KtrfBackLogOwner : uint8_t { None, Narrative, PlayData };
KtrfBackLogOwner ktrfBackLogOwner = KtrfBackLogOwner::None;
int ktrfRouteMapProjectionMajor = -1;
int ktrfRouteMapProjectionSubpage = -1;
enum class KtrfFrontendHandoff : uint8_t {
  None,
  TitleStart,
  TitleLoad,
  TitleConfig,
  TitleReplay,
  LoadToTitle,
  ConfigToTitle,
  ReplayToTitle,
};
Kotonoha::SchoolDaysFrontendTransition ktrfFrontendTransition;
KtrfFrontendHandoff ktrfFrontendHandoff = KtrfFrontendHandoff::None;
bool ktrfTitleHasPresented = false;
Kotonoha::SchoolDaysEpisodeOverlay ktrfEpisodeOverlay;
Kotonoha::Gameplay *ktrfEpisodeOverlayOwner = nullptr;
SDL_Texture *ktrfEpisodeTelopTexture = nullptr;
int ktrfEpisodeTextureNumber = 0;
// Runtime-observed retail MenuBar rule: the left transport button first places
// the current physical scene at its start; activating it again while still in
// that start-navigation state asks RouteProc for the authenticated predecessor.
// Track the physical Gameplay identity rather than inventing a millisecond
// threshold (no such threshold was proven in SysMenuSDHQ.dll).
Kotonoha::Gameplay *ktrfMenuBackStartGameplay = nullptr;

void DestroySchoolDaysEpisodeOverlayTexture() {
  if (ktrfEpisodeTelopTexture != nullptr) {
    SDL_DestroyTexture(ktrfEpisodeTelopTexture);
    ktrfEpisodeTelopTexture = nullptr;
  }
  ktrfEpisodeTextureNumber = 0;
}

void ResetSchoolDaysEpisodeOverlay() {
  ktrfEpisodeOverlay.Reset();
  ktrfEpisodeOverlayOwner = nullptr;
  DestroySchoolDaysEpisodeOverlayTexture();
}

bool ArmSchoolDaysEpisodeOverlay(const std::string &sourceSceneKey,
                                 const std::string &destinationSceneKey,
                                 Kotonoha::Gameplay *owner) {
  DestroySchoolDaysEpisodeOverlayTexture();
  ktrfEpisodeOverlayOwner = nullptr;
  if (!ktrfEpisodeOverlay.ArmForNaturalHandoff(sourceSceneKey,
                                               destinationSceneKey)) {
    SDL_LogError(0,
                 "[SD-EPISODE] natural handoff %s -> %s did not map to Episode 2..6",
                 sourceSceneKey.c_str(), destinationSceneKey.c_str());
    return false;
  }
  if (owner == nullptr) {
    ktrfEpisodeOverlay.Reset();
    SDL_LogError(0, "[SD-EPISODE] handoff destination has no Gameplay owner");
    return false;
  }
  ktrfEpisodeOverlayOwner = owner;
  SDL_Log("[SD-EPISODE] armed Episode %d scene=%s",
          ktrfEpisodeOverlay.EpisodeNumber(), destinationSceneKey.c_str());
  return true;
}

bool EnsureSchoolDaysEpisodeOverlayTexture() {
  if (!ktrfEpisodeOverlay.Armed()) return false;
  const int episode = ktrfEpisodeOverlay.EpisodeNumber();
  if (ktrfEpisodeTelopTexture != nullptr &&
      ktrfEpisodeTextureNumber == episode) {
    return true;
  }

  DestroySchoolDaysEpisodeOverlayTexture();
  const std::string logical =
      Kotonoha::SchoolDaysEpisodeOverlay::TelopLogicalPath(episode);
  if (logical.empty()) {
    SDL_SetError("School Days Episode overlay has no retail TELOP mapping");
    return false;
  }
  const auto asset =
      Kotonoha::ResolveSchoolDaysAssetPath(ktrfAssetsRoot.c_str(), logical);
  if (!asset.present || asset.collision || asset.physical.empty()) {
    SDL_SetError("School Days retail Episode TELOP asset could not be resolved");
    return false;
  }
  ktrfEpisodeTelopTexture =
      KotonohaImageCreateTexture(global.render, asset.physical.c_str(), -1, -1);
  if (ktrfEpisodeTelopTexture == nullptr) return false;
  if (!SDL_SetTextureBlendMode(ktrfEpisodeTelopTexture, SDL_BLENDMODE_BLEND) ||
      !SDL_SetTextureScaleMode(ktrfEpisodeTelopTexture, SDL_SCALEMODE_LINEAR)) {
    DestroySchoolDaysEpisodeOverlayTexture();
    return false;
  }
  ktrfEpisodeTextureNumber = episode;
  return true;
}

bool RenderSchoolDaysEpisodeOverlay(Kotonoha::Gameplay *play) {
  if (ktrfController == nullptr || play == nullptr ||
      play != ktrfEpisodeOverlayOwner ||
      ktrfController->FlowState() !=
          Kotonoha::SchoolDaysFlowController::State::Gameplay ||
      !ktrfEpisodeOverlay.AppliesTo(ktrfController->CurrentScene().sceneKey)) {
    return true;
  }

  const uint64_t sceneMs = KotonohaTimeGet(play->tm);
  const uint8_t alpha = Kotonoha::SchoolDaysEpisodeOverlay::Alpha(sceneMs);
  if (alpha == 0u) return true;
  if (!EnsureSchoolDaysEpisodeOverlayTexture()) return false;

  // TELOP01..06 are the retail 800x452 transparent full-frame overlays.  Draw
  // the complete asset through the same Gameplay canvas placement as the
  // authored WMV/image layers: this preserves the original caption position,
  // antialiasing, shadow and per-pixel alpha without recreating any typography.
  int outputWidth = 0;
  int outputHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(global.render, &outputWidth, &outputHeight) ||
      outputWidth <= 0 || outputHeight <= 0) {
    return false;
  }
  constexpr float aspectRatio = 16.0f / 9.0f;
  SDL_FRect destination{};
  const float renderWidth = static_cast<float>(outputWidth);
  const float renderHeight = static_cast<float>(outputHeight);
  const float outputAspect = renderWidth / renderHeight;
  if (outputAspect < aspectRatio) {
    destination.w = renderWidth;
    destination.h = renderWidth / aspectRatio;
    destination.x = 0.0f;
    destination.y = (renderHeight - destination.h) * 0.5f;
  } else {
    destination.h = renderHeight;
    destination.w = renderHeight * aspectRatio;
    destination.y = 0.0f;
    destination.x = (renderWidth - destination.w) * 0.5f;
  }
  if (!SDL_SetTextureAlphaMod(ktrfEpisodeTelopTexture, alpha)) return false;
  return SDL_RenderTexture(global.render, ktrfEpisodeTelopTexture, nullptr,
                           &destination);
}

void ClearTemporaryPlaybackPause() {
  Kotonoha::Gameplay *temporary =
      ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()
          ? ktrfReplayPlayback->CurrentGameplay()
          : (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()
                 ? ktrfPlayDataPlayback->CurrentGameplay()
                 : nullptr);
  if (temporary != nullptr) {
    temporary->SetUserPaused(&global, false);
  } else {
    global.config.paused = false;
  }
}

bool EnsureSchoolDaysSubtitleStyles() {
  if (global.config.assStylesData != nullptr &&
      global.config.assStylesSize > 0u) {
    return true;
  }

  const char *basePath = SDL_GetBasePath();
  if (basePath == nullptr || *basePath == '\0') {
    SDL_LogError(0, "[SD-SUBTITLES] SDL_GetBasePath failed: %s",
                 SDL_GetError());
    return false;
  }

  const std::filesystem::path stylePath =
      std::filesystem::path(basePath) / "assets" / "school-days" /
      "styles.skot";
  size_t styleSize = 0;
  void *styleData = SDL_LoadFile(stylePath.string().c_str(), &styleSize);
  if (styleData == nullptr || styleSize == 0u) {
    if (styleData != nullptr) SDL_free(styleData);
    SDL_LogError(0, "[SD-SUBTITLES] default styles unavailable: %s (%s)",
                 stylePath.string().c_str(), SDL_GetError());
    return false;
  }

  global.config.assStylesData = static_cast<char *>(styleData);
  global.config.assStylesSize = styleSize;
  SDL_Log("[SD-SUBTITLES] default styles loaded bytes=%llu",
          static_cast<unsigned long long>(styleSize));
  return true;
}

bool BeginSchoolDaysFrontendHandoff(
    KtrfFrontendHandoff handoff,
    Kotonoha::SchoolDaysFrontendTransition::Profile profile) {
  if (handoff == KtrfFrontendHandoff::None ||
      ktrfFrontendHandoff != KtrfFrontendHandoff::None ||
      ktrfFrontendTransition.Active(SDL_GetTicks())) {
    return false;
  }
  ktrfFrontendHandoff = handoff;
  ktrfFrontendTransition.Begin(profile, SDL_GetTicks());
  return true;
}

Kotonoha::SchoolDaysConfigMenuModel::Variant SchoolDaysConfigVariant() {
  using Source = Kotonoha::SchoolDaysSaveFeelModel::Variant;
  using Target = Kotonoha::SchoolDaysConfigMenuModel::Variant;
  switch (ktrfUiVariant) {
  case Source::Standard: return Target::Standard;
  case Source::Wide: return Target::Wide;
  case Source::WideFull: return Target::WideFull;
  case Source::WideNote: return Target::WideNote;
  }
  return Target::Standard;
}

Kotonoha::SchoolDaysDisplayPolicy::RetailBackendState
SchoolDaysRetailBackendSeed(
    Kotonoha::SchoolDaysSaveFeelModel::Variant variant) {
  const auto projected =
      Kotonoha::SchoolDaysDisplayPolicy::CanonicalRetailStateForVariant(variant);
  // Kotonoha has no separate legacy per-device logical profile.  Seed B and D
  // identically from the selected authored family, then preserve them as
  // distinct fields so subsequent Config transitions follow the retail
  // projection contract exactly.
  return {projected.effectiveDisplayType, projected.effectiveDisplayType,
          projected.windowMode, projected.typeMiniNote};
}

Kotonoha::SchoolDaysDisplayPolicy::RetailConfigInputs
SchoolDaysRetailConfigInputs() {
  // Modern Kotonoha can present all four authored logical families through the
  // existing aspect-fit viewport.  Report those logical capabilities directly;
  // never derive them from the current monitor/window dimensions.  guardCc=0
  // selects the retail path that keeps B and D synchronized for this backend.
  return {0, 1, 1};
}

void RefreshSchoolDaysRetailDisplayProjection() {
  ktrfRetailDisplayState =
      Kotonoha::SchoolDaysDisplayPolicy::ProjectRetailState(
          ktrfRetailDisplayBackendState);
  ktrfUiVariant = Kotonoha::SchoolDaysDisplayPolicy::SelectRetailVariant(
      ktrfRetailDisplayState);
}

struct SchoolDaysModernDisplayPanelState {
  int selectedResolution = -1;
  int hoveredRow = -1;
  int hoveredResolution = -1;
  int hoveredUpscaler = -1;
  int resolutionScroll = 0;
  bool resolutionListOpen = false;
  bool upscalerListOpen = false;
};

SchoolDaysModernDisplayPanelState ktrfModernDisplayPanel;

bool ApplySchoolDaysModernAspectPreference() {
  const int preference = std::clamp(global.config.contentAspectPreference.load(), 0, 2);
  Kotonoha::SchoolDaysSaveFeelModel::Variant requested = ktrfUiVariant;
  if (preference == 1) {
    requested = Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
  } else if (preference == 2) {
    requested = Kotonoha::SchoolDaysSaveFeelModel::Variant::Wide;
  } else {
    int outputWidth = 0;
    int outputHeight = 0;
    Kotonoha::SchoolDaysDisplayPolicy::Decision decision;
    if (!SDL_GetCurrentRenderOutputSize(global.render, &outputWidth, &outputHeight) ||
        !Kotonoha::SchoolDaysDisplayPolicy::SelectAuto(
            outputWidth, outputHeight, &decision)) {
      return false;
    }
    requested = decision.variant;
  }

  ktrfRetailDisplayBackendState = SchoolDaysRetailBackendSeed(requested);
  RefreshSchoolDaysRetailDisplayProjection();
  return true;
}

bool ApplySchoolDaysRetailWindowModeToPhysicalDisplay() {
  const bool full = ktrfRetailDisplayBackendState.windowMode != 0;
  const auto mode = full ? Kotonoha::DisplayMode::BorderlessFullscreen
                         : Kotonoha::DisplayMode::Windowed;
  const SDL_DisplayID display =
      Kotonoha::ResolveDisplayIndex(global.config.displayIndex.load());
  if (!Kotonoha::SetDisplay(global, mode, display)) {
    SDL_LogError(0,
                 "[SD-CONFIG] failed to synchronize retail WINDOW/FULL with physical display: %s",
                 SDL_GetError());
    return false;
  }
  return true;
}

SDL_FRect SchoolDaysModernConfigViewportRect() {
  int width = 0;
  int height = 0;
  if (global.render == nullptr ||
      !SDL_GetCurrentRenderOutputSize(global.render, &width, &height) ||
      width <= 0 || height <= 0) {
    return {};
  }
  const auto layout = Kotonoha::SchoolDaysConfigMenuRenderPlan::LayoutFor(
      SchoolDaysConfigVariant());
  const float scale = std::min(
      static_cast<float>(width) / static_cast<float>(layout.logicalWidth),
      static_cast<float>(height) / static_cast<float>(layout.logicalHeight));
  const float viewportWidth = layout.logicalWidth * scale;
  const float viewportHeight = layout.logicalHeight * scale;
  return {(width - viewportWidth) * 0.5f, (height - viewportHeight) * 0.5f,
          viewportWidth, viewportHeight};
}

SDL_FRect SchoolDaysModernDisplayTabRect() {
  const SDL_FRect viewport = SchoolDaysModernConfigViewportRect();
  if (viewport.w <= 0.0f || viewport.h <= 0.0f) return {};
  const auto layout = Kotonoha::SchoolDaysConfigMenuRenderPlan::LayoutFor(
      SchoolDaysConfigVariant());
  const float viewportScale = viewport.w / layout.logicalWidth;
  const float x = (398.0f - 0.5f) * layout.scale;
  const float y = (54.0f - 0.5f + layout.yOffset) * layout.scale;
  const float w = (194.0f + 1.0f) * layout.scale;
  const float h = (38.0f + 1.0f) * layout.scale;
  return {viewport.x + x * viewportScale,
          viewport.y + y * viewportScale,
          w * viewportScale, h * viewportScale};
}

SDL_FRect SchoolDaysModernDisplayPanelRect() {
  const SDL_FRect viewport = SchoolDaysModernConfigViewportRect();
  const SDL_FRect tab = SchoolDaysModernDisplayTabRect();
  if (viewport.w <= 0.0f || tab.h <= 0.0f) return {};
  const float margin = std::max(14.0f, viewport.w * 0.018f);
  const float y = tab.y + tab.h + std::max(8.0f, viewport.h * 0.012f);
  return {viewport.x + margin, y,
          viewport.w - margin * 2.0f,
          std::max(0.0f, viewport.y + viewport.h - y - margin)};
}

std::vector<Kotonoha::DisplayResolution> SchoolDaysCurrentResolutions() {
  SDL_DisplayID display = Kotonoha::ResolveDisplayIndex(global.config.displayIndex.load());
  if (display == 0 && global.window != nullptr)
    display = SDL_GetDisplayForWindow(global.window);
  return Kotonoha::EnumerateDisplayResolutions(display);
}

void FormatSchoolDaysResolution(const Kotonoha::DisplayResolution &r,
                                char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0u) return;
  const float rounded = std::round(r.refreshRate);
  if (std::fabs(r.refreshRate - rounded) < 0.01f) {
    SDL_snprintf(buffer, bufferSize, "%dx%d @ %.0f Hz",
                 r.width, r.height, r.refreshRate);
  } else {
    SDL_snprintf(buffer, bufferSize, "%dx%d @ %.2f Hz",
                 r.width, r.height, r.refreshRate);
  }
}

bool SchoolDaysPointInRect(float x, float y, const SDL_FRect &rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y &&
         y < rect.y + rect.h;
}

float SchoolDaysModernDisplayRowHeight(const SDL_FRect &panel) {
  return std::min(64.0f, std::max(44.0f, panel.h / 7.2f));
}

SDL_FRect SchoolDaysModernDisplayRowRect(const SDL_FRect &panel, int row) {
  const float rowHeight = SchoolDaysModernDisplayRowHeight(panel);
  return {panel.x + 18.0f,
          panel.y + 20.0f + row * rowHeight - 4.0f,
          panel.w - 36.0f,
          rowHeight - 8.0f};
}

struct SchoolDaysResolutionPopupLayout {
  SDL_FRect rect{};
  float headerHeight = 0.0f;
  float rowHeight = 0.0f;
  int visibleRows = 0;
};

SchoolDaysResolutionPopupLayout SchoolDaysResolutionPopup(
    const SDL_FRect &panel) {
  SchoolDaysResolutionPopupLayout out;
  out.rowHeight = std::min(44.0f, std::max(34.0f,
      SchoolDaysModernDisplayRowHeight(panel) * 0.72f));
  out.headerHeight = out.rowHeight;
  out.rect = {panel.x + panel.w * 0.44f,
              panel.y + 16.0f,
              panel.w * 0.54f - 16.0f,
              panel.h - 32.0f};
  out.visibleRows = std::max(
      1, static_cast<int>((out.rect.h - out.headerHeight - 12.0f) /
                          out.rowHeight));
  return out;
}

struct SchoolDaysUpscalerOption {
  const char *label;
  bool enabled;
  Kotonoha_UpscalerPreset preset;
};

constexpr SchoolDaysUpscalerOption kSchoolDaysUpscalerOptions[] = {
    {"OFF", false, KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16},
    {"Anime Light - ArtCNN C4F16", true,
     KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16},
    {"Anime Clean - ArtCNN C4F16 DS", true,
     KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16_DS},
    {"Anime Quality - ArtCNN C4F32", true,
     KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F32},
    {"Legacy - Anime4K B-S", true,
     KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K},
};

SchoolDaysResolutionPopupLayout SchoolDaysUpscalerPopup(
    const SDL_FRect &panel) {
  auto out = SchoolDaysResolutionPopup(panel);
  out.visibleRows = static_cast<int>(std::size(kSchoolDaysUpscalerOptions));
  out.rect.h = out.headerHeight + out.visibleRows * out.rowHeight + 12.0f;
  return out;
}

int SchoolDaysCurrentUpscalerOption() {
  if (!Kotonoha::RuntimeUpscalingEnabled()) return 0;
  const auto preset = Kotonoha::RuntimeUpscalingPreset();
  for (int i = 1; i < static_cast<int>(std::size(kSchoolDaysUpscalerOptions)); ++i)
    if (kSchoolDaysUpscalerOptions[i].preset == preset) return i;
  return 4;
}

void ApplySchoolDaysUpscalerOption(int index) {
  if (index < 0 || index >= static_cast<int>(std::size(kSchoolDaysUpscalerOptions)))
    return;
  const auto &option = kSchoolDaysUpscalerOptions[index];
  if (option.enabled) {
    Kotonoha::RuntimeSetUpscalingPreset(option.preset);
    Kotonoha::RuntimeSetUpscalingEnabled(true);
  } else {
    Kotonoha::RuntimeSetUpscalingEnabled(false);
  }
}

bool ApplySchoolDaysResolutionSelection(int index) {
  const auto resolutions = SchoolDaysCurrentResolutions();
  if (index < 0 || index >= static_cast<int>(resolutions.size())) return false;

  ktrfModernDisplayPanel.selectedResolution = index;
  const auto &r = resolutions[static_cast<size_t>(index)];
  const auto mode = static_cast<Kotonoha::DisplayMode>(
      std::clamp(global.config.displayMode.load(), 0, 2));
  const SDL_DisplayID display =
      Kotonoha::ResolveDisplayIndex(global.config.displayIndex.load());

  if (mode == Kotonoha::DisplayMode::Windowed) {
    global.config.windowSize[0] = r.width;
    global.config.windowSize[1] = r.height;
    global.config.dirtyConfig = true;
    return Kotonoha::SetDisplay(global, mode, display);
  }
  if (mode == Kotonoha::DisplayMode::ExclusiveFullscreen) {
    return Kotonoha::SetDisplay(global, mode, display,
                                r.width, r.height, r.refreshRate);
  }

  // Borderless follows the desktop/native mode by definition. Keep the chosen
  // mode as the preferred exclusive resolution so switching to Exclusive later
  // has an explicit player-selected target.
  global.config.fullscreenSize[0] = r.width;
  global.config.fullscreenSize[1] = r.height;
  global.config.fullscreenRefreshMilliHz =
      static_cast<int>(r.refreshRate * 1000.0f + 0.5f);
  global.config.dirtyConfig = true;
  return true;
}

bool RenderSchoolDaysModernDisplayPanel(Kotonoha::Kotonoha *app) {
  if (app == nullptr || global.render == nullptr || ktrfSystemUi == nullptr ||
      !ktrfSystemUi->IsConfigOpen()) {
    return true;
  }
  TTF_Font *font = nullptr;
  if (const auto it = app->fonts.find(
          Kotonoha::SchoolDaysSubtitleTypography::PrimaryFontKey);
      it != app->fonts.end()) {
    font = it->second;
  }
  if (font == nullptr) return true;

  // Always cover/rename the removed SOMCON tab, even while General/Sound is
  // selected. This keeps the original authored tab geometry without exposing a
  // dead hardware surface in the commercial remaster.
  const SDL_FRect tab = SchoolDaysModernDisplayTabRect();
  if (tab.w > 0.0f && tab.h > 0.0f) {
    const bool active =
        ktrfConfigPage == Kotonoha::SchoolDaysConfigMenuModel::Page::Display;
    SDL_SetRenderDrawBlendMode(global.render, SDL_BLENDMODE_BLEND);
    if (active)
      SDL_SetRenderDrawColor(global.render, 186, 205, 244, 255);
    else
      SDL_SetRenderDrawColor(global.render, 235, 238, 242, 252);
    if (!SDL_RenderFillRect(global.render, &tab)) return false;
    SDL_SetRenderDrawColor(global.render, 118, 122, 132, 255);
    if (!SDL_RenderRect(global.render, &tab)) return false;
    const char *tabText = "DISPLAY OPTIONS";
    SDL_Surface *tabSurface = Kotonoha::SchoolDaysSubtitleTypography::Render(
        font, tabText, SDL_strlen(tabText));
    if (tabSurface != nullptr) {
      SDL_Texture *tabTexture = SDL_CreateTextureFromSurface(global.render, tabSurface);
      if (tabTexture != nullptr) {
        const float scale = std::min(
            0.42f, std::min((tab.w - 18.0f) / std::max(1, tabSurface->w),
                            (tab.h - 10.0f) / std::max(1, tabSurface->h)));
        SDL_FRect dst{tab.x + (tab.w - tabSurface->w * scale) * 0.5f,
                      tab.y + (tab.h - tabSurface->h * scale) * 0.5f,
                      tabSurface->w * scale, tabSurface->h * scale};
        (void)SDL_RenderTexture(global.render, tabTexture, nullptr, &dst);
        SDL_DestroyTexture(tabTexture);
      }
      SDL_DestroySurface(tabSurface);
    }
  }

  if (ktrfConfigPage != Kotonoha::SchoolDaysConfigMenuModel::Page::Display)
    return true;

  const SDL_FRect panel = SchoolDaysModernDisplayPanelRect();
  if (panel.w <= 0.0f || panel.h <= 0.0f) return true;

  SDL_SetRenderDrawBlendMode(global.render, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(global.render, 12, 15, 24, 242);
  if (!SDL_RenderFillRect(global.render, &panel)) return false;
  SDL_SetRenderDrawColor(global.render, 255, 255, 255, 64);
  if (!SDL_RenderRect(global.render, &panel)) return false;

  const auto resolutions = SchoolDaysCurrentResolutions();
  int selected = ktrfModernDisplayPanel.selectedResolution;
  if (selected < 0 || selected >= static_cast<int>(resolutions.size())) {
    selected = 0;
    const int wantedW = global.config.displayMode.load() ==
                                static_cast<int>(Kotonoha::DisplayMode::ExclusiveFullscreen)
                            ? global.config.fullscreenSize[0].load()
                            : global.config.windowSize[0].load();
    const int wantedH = global.config.displayMode.load() ==
                                static_cast<int>(Kotonoha::DisplayMode::ExclusiveFullscreen)
                            ? global.config.fullscreenSize[1].load()
                            : global.config.windowSize[1].load();
    for (int i = 0; i < static_cast<int>(resolutions.size()); ++i) {
      if (resolutions[static_cast<size_t>(i)].width == wantedW &&
          resolutions[static_cast<size_t>(i)].height == wantedH) {
        selected = i;
        break;
      }
    }
    ktrfModernDisplayPanel.selectedResolution = selected;
  }

  const int aspect = std::clamp(global.config.contentAspectPreference.load(), 0, 2);
  const char *aspectLabel[] = {"Auto", "4:3", "16:9"};
  const int mode = std::clamp(global.config.displayMode.load(), 0, 2);
  const char *modeLabel = mode == static_cast<int>(Kotonoha::DisplayMode::Windowed)
                              ? "Windowed"
                              : mode == static_cast<int>(Kotonoha::DisplayMode::BorderlessFullscreen)
                                    ? "Borderless"
                                    : "Exclusive";
  char displayText[64];
  SDL_snprintf(displayText, sizeof(displayText), "Display %d",
               global.config.displayIndex.load() + 1);
  char resolutionText[96] = "Desktop/native";
  if (!resolutions.empty()) {
    const auto &r = resolutions[static_cast<size_t>(selected)];
    FormatSchoolDaysResolution(r, resolutionText, sizeof(resolutionText));
  }
  const int upscalerOption = SchoolDaysCurrentUpscalerOption();
  const char *upscaleText = kSchoolDaysUpscalerOptions[upscalerOption].label;
  char aspectText[96];
  char modeText[96];
  char monitorText[96];
  char resText[128];
  char upscaleLabel[128];
  SDL_snprintf(aspectText, sizeof(aspectText), "Aspect: %s", aspectLabel[aspect]);
  SDL_snprintf(modeText, sizeof(modeText), "Mode: %s", modeLabel);
  SDL_snprintf(monitorText, sizeof(monitorText), "Monitor: %s", displayText);
  SDL_snprintf(resText, sizeof(resText), "Resolution: %s", resolutionText);
  SDL_snprintf(upscaleLabel, sizeof(upscaleLabel), "Upscaling: %s", upscaleText);

  const char *labels[6] = {"DISPLAY / REMASTER", aspectText, modeText,
                           monitorText, resText, upscaleLabel};
  const float rowHeight = SchoolDaysModernDisplayRowHeight(panel);
  for (int row = 1; row <= 5; ++row) {
    const SDL_FRect box = SchoolDaysModernDisplayRowRect(panel, row);
    const bool hovered = ktrfModernDisplayPanel.hoveredRow == row;
    SDL_SetRenderDrawColor(global.render,
                           hovered ? 65 : 255,
                           hovered ? 92 : 255,
                           hovered ? 148 : 255,
                           hovered ? 118 : 18);
    if (!SDL_RenderFillRect(global.render, &box)) return false;
    SDL_SetRenderDrawColor(global.render, 255, 255, 255,
                           hovered ? 170 : 54);
    if (!SDL_RenderRect(global.render, &box)) return false;
  }
  for (int row = 0; row < 6; ++row) {
    SDL_Surface *surface = Kotonoha::SchoolDaysSubtitleTypography::Render(
        font, labels[row], SDL_strlen(labels[row]));
    if (surface == nullptr) continue;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(global.render, surface);
    if (texture != nullptr) {
      const float scale = row == 0 ? 0.56f : 0.50f;
      SDL_FRect dst{panel.x + 28.0f,
                    panel.y + 20.0f + row * rowHeight,
                    surface->w * scale, surface->h * scale};
      (void)SDL_RenderTexture(global.render, texture, nullptr, &dst);
      SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
  }

  // Persistent affordance plus a stronger hover cue: unlike the authored
  // bitmap controls, these modern rows are text-driven and otherwise look like
  // passive labels. The marker makes it obvious that every row is selectable.
  for (int row = 1; row <= 5; ++row) {
    const SDL_FRect box = SchoolDaysModernDisplayRowRect(panel, row);
    const bool hovered = ktrfModernDisplayPanel.hoveredRow == row;
    const char *marker =
        (row == 4 && ktrfModernDisplayPanel.resolutionListOpen) ||
                (row == 5 && ktrfModernDisplayPanel.upscalerListOpen)
            ? "^"
            : (hovered ? "SELECT  >" : ">");
    SDL_Surface *markerSurface = Kotonoha::SchoolDaysSubtitleTypography::Render(
        font, marker, SDL_strlen(marker));
    if (markerSurface == nullptr) continue;
    SDL_Texture *markerTexture =
        SDL_CreateTextureFromSurface(global.render, markerSurface);
    if (markerTexture != nullptr) {
      const float scale = hovered ? 0.38f : 0.34f;
      const float w = markerSurface->w * scale;
      const float h = markerSurface->h * scale;
      SDL_FRect dst{box.x + box.w - w - 14.0f,
                    box.y + (box.h - h) * 0.5f, w, h};
      (void)SDL_RenderTexture(global.render, markerTexture, nullptr, &dst);
      SDL_DestroyTexture(markerTexture);
    }
    SDL_DestroySurface(markerSurface);
  }

  if (ktrfModernDisplayPanel.resolutionListOpen && !resolutions.empty()) {
    const auto popup = SchoolDaysResolutionPopup(panel);
    const int maxScroll = std::max(
        0, static_cast<int>(resolutions.size()) - popup.visibleRows);
    ktrfModernDisplayPanel.resolutionScroll = std::clamp(
        ktrfModernDisplayPanel.resolutionScroll, 0, maxScroll);

    SDL_SetRenderDrawColor(global.render, 8, 11, 18, 252);
    if (!SDL_RenderFillRect(global.render, &popup.rect)) return false;
    SDL_SetRenderDrawColor(global.render, 190, 205, 235, 230);
    if (!SDL_RenderRect(global.render, &popup.rect)) return false;

    const char *heading = "RESOLUTIONS  -  WHEEL TO SCROLL";
    SDL_Surface *headingSurface =
        Kotonoha::SchoolDaysSubtitleTypography::Render(
            font, heading, SDL_strlen(heading));
    if (headingSurface != nullptr) {
      SDL_Texture *headingTexture =
          SDL_CreateTextureFromSurface(global.render, headingSurface);
      if (headingTexture != nullptr) {
        const float scale = 0.34f;
        SDL_FRect dst{popup.rect.x + 12.0f, popup.rect.y + 5.0f,
                      headingSurface->w * scale, headingSurface->h * scale};
        (void)SDL_RenderTexture(global.render, headingTexture, nullptr, &dst);
        SDL_DestroyTexture(headingTexture);
      }
      SDL_DestroySurface(headingSurface);
    }

    for (int visible = 0; visible < popup.visibleRows; ++visible) {
      const int index = ktrfModernDisplayPanel.resolutionScroll + visible;
      if (index >= static_cast<int>(resolutions.size())) break;
      const SDL_FRect item{
          popup.rect.x + 8.0f,
          popup.rect.y + popup.headerHeight + visible * popup.rowHeight,
          popup.rect.w - 16.0f,
          popup.rowHeight - 3.0f};
      const bool hovered =
          ktrfModernDisplayPanel.hoveredResolution == index;
      const bool chosen = selected == index;
      if (hovered || chosen) {
        SDL_SetRenderDrawColor(global.render,
                               chosen ? 57 : 50,
                               chosen ? 83 : 64,
                               chosen ? 144 : 90,
                               hovered ? 220 : 150);
        if (!SDL_RenderFillRect(global.render, &item)) return false;
      }
      SDL_SetRenderDrawColor(global.render, 255, 255, 255,
                             hovered ? 150 : 32);
      if (!SDL_RenderRect(global.render, &item)) return false;

      const auto &r = resolutions[static_cast<size_t>(index)];
      char itemText[128];
      char modeText[96];
      FormatSchoolDaysResolution(r, modeText, sizeof(modeText));
      SDL_snprintf(itemText, sizeof(itemText), "%s %s",
                   chosen ? ">" : " ", modeText);
      SDL_Surface *itemSurface =
          Kotonoha::SchoolDaysSubtitleTypography::Render(
              font, itemText, SDL_strlen(itemText));
      if (itemSurface == nullptr) continue;
      SDL_Texture *itemTexture =
          SDL_CreateTextureFromSurface(global.render, itemSurface);
      if (itemTexture != nullptr) {
        const float scale = 0.38f;
        const float h = itemSurface->h * scale;
        SDL_FRect dst{item.x + 10.0f,
                      item.y + (item.h - h) * 0.5f,
                      itemSurface->w * scale, h};
        (void)SDL_RenderTexture(global.render, itemTexture, nullptr, &dst);
        SDL_DestroyTexture(itemTexture);
      }
      SDL_DestroySurface(itemSurface);
    }
  }

  if (ktrfModernDisplayPanel.upscalerListOpen) {
    const auto popup = SchoolDaysUpscalerPopup(panel);
    SDL_SetRenderDrawColor(global.render, 8, 11, 18, 252);
    if (!SDL_RenderFillRect(global.render, &popup.rect)) return false;
    SDL_SetRenderDrawColor(global.render, 190, 205, 235, 230);
    if (!SDL_RenderRect(global.render, &popup.rect)) return false;

    const char *heading = "ANIME UPSCALER PRESETS";
    SDL_Surface *headingSurface =
        Kotonoha::SchoolDaysSubtitleTypography::Render(
            font, heading, SDL_strlen(heading));
    if (headingSurface != nullptr) {
      SDL_Texture *headingTexture =
          SDL_CreateTextureFromSurface(global.render, headingSurface);
      if (headingTexture != nullptr) {
        const float scale = 0.34f;
        SDL_FRect dst{popup.rect.x + 12.0f, popup.rect.y + 5.0f,
                      headingSurface->w * scale, headingSurface->h * scale};
        (void)SDL_RenderTexture(global.render, headingTexture, nullptr, &dst);
        SDL_DestroyTexture(headingTexture);
      }
      SDL_DestroySurface(headingSurface);
    }

    const int chosen = SchoolDaysCurrentUpscalerOption();
    for (int index = 0;
         index < static_cast<int>(std::size(kSchoolDaysUpscalerOptions)); ++index) {
      const SDL_FRect item{
          popup.rect.x + 8.0f,
          popup.rect.y + popup.headerHeight + index * popup.rowHeight,
          popup.rect.w - 16.0f,
          popup.rowHeight - 3.0f};
      const bool hovered = ktrfModernDisplayPanel.hoveredUpscaler == index;
      const bool selectedPreset = chosen == index;
      if (hovered || selectedPreset) {
        SDL_SetRenderDrawColor(global.render,
                               selectedPreset ? 57 : 50,
                               selectedPreset ? 83 : 64,
                               selectedPreset ? 144 : 90,
                               hovered ? 220 : 150);
        if (!SDL_RenderFillRect(global.render, &item)) return false;
      }
      SDL_SetRenderDrawColor(global.render, 255, 255, 255,
                             hovered ? 150 : 32);
      if (!SDL_RenderRect(global.render, &item)) return false;

      char itemText[160];
      SDL_snprintf(itemText, sizeof(itemText), "%s %s",
                   selectedPreset ? ">" : " ",
                   kSchoolDaysUpscalerOptions[index].label);
      SDL_Surface *itemSurface =
          Kotonoha::SchoolDaysSubtitleTypography::Render(
              font, itemText, SDL_strlen(itemText));
      if (itemSurface == nullptr) continue;
      SDL_Texture *itemTexture =
          SDL_CreateTextureFromSurface(global.render, itemSurface);
      if (itemTexture != nullptr) {
        const float scale = 0.34f;
        const float h = itemSurface->h * scale;
        SDL_FRect dst{item.x + 10.0f, item.y + (item.h - h) * 0.5f,
                      itemSurface->w * scale, h};
        (void)SDL_RenderTexture(global.render, itemTexture, nullptr, &dst);
        SDL_DestroyTexture(itemTexture);
      }
      SDL_DestroySurface(itemSurface);
    }
  }
  return true;
}

bool HandleSchoolDaysModernDisplayPanel(const SDL_Event *event) {
  if (event == nullptr || global.render == nullptr ||
      ktrfSystemUi == nullptr || !ktrfSystemUi->IsConfigOpen() ||
      ktrfConfigPage != Kotonoha::SchoolDaysConfigMenuModel::Page::Display) {
    return false;
  }
  const bool pointerEvent = event->type == SDL_EVENT_MOUSE_MOTION ||
                            event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  if (!pointerEvent && event->type != SDL_EVENT_MOUSE_WHEEL) return false;

  const SDL_FRect panel = SchoolDaysModernDisplayPanelRect();
  if (panel.w <= 0.0f || panel.h <= 0.0f) return false;

  if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    if (!ktrfModernDisplayPanel.resolutionListOpen) return false;
    const auto resolutions = SchoolDaysCurrentResolutions();
    const auto popup = SchoolDaysResolutionPopup(panel);
    const int maxScroll = std::max(
        0, static_cast<int>(resolutions.size()) - popup.visibleRows);
    if (event->wheel.y > 0.0f)
      --ktrfModernDisplayPanel.resolutionScroll;
    else if (event->wheel.y < 0.0f)
      ++ktrfModernDisplayPanel.resolutionScroll;
    ktrfModernDisplayPanel.resolutionScroll = std::clamp(
        ktrfModernDisplayPanel.resolutionScroll, 0, maxScroll);
    ktrfModernDisplayPanel.hoveredResolution = -1;
    return true;
  }

  SDL_Event converted = *event;
  if (!SDL_ConvertEventToRenderCoordinates(global.render, &converted)) return false;
  const float x = event->type == SDL_EVENT_MOUSE_MOTION
                      ? converted.motion.x
                      : converted.button.x;
  const float y = event->type == SDL_EVENT_MOUSE_MOTION
                      ? converted.motion.y
                      : converted.button.y;

  int row = -1;
  for (int candidate = 1; candidate <= 5; ++candidate) {
    if (SchoolDaysPointInRect(
            x, y, SchoolDaysModernDisplayRowRect(panel, candidate))) {
      row = candidate;
      break;
    }
  }

  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    ktrfModernDisplayPanel.hoveredRow = row;
    ktrfModernDisplayPanel.hoveredResolution = -1;
    ktrfModernDisplayPanel.hoveredUpscaler = -1;
    if (ktrfModernDisplayPanel.resolutionListOpen) {
      const auto resolutions = SchoolDaysCurrentResolutions();
      const auto popup = SchoolDaysResolutionPopup(panel);
      if (SchoolDaysPointInRect(x, y, popup.rect)) {
        const float relativeY = y - popup.rect.y - popup.headerHeight;
        if (relativeY >= 0.0f) {
          const int visible = static_cast<int>(relativeY / popup.rowHeight);
          if (visible >= 0 && visible < popup.visibleRows) {
            const int index =
                ktrfModernDisplayPanel.resolutionScroll + visible;
            if (index >= 0 && index < static_cast<int>(resolutions.size()))
              ktrfModernDisplayPanel.hoveredResolution = index;
          }
        }
      }
    }
    if (ktrfModernDisplayPanel.upscalerListOpen) {
      const auto popup = SchoolDaysUpscalerPopup(panel);
      if (SchoolDaysPointInRect(x, y, popup.rect)) {
        const float relativeY = y - popup.rect.y - popup.headerHeight;
        if (relativeY >= 0.0f) {
          const int index = static_cast<int>(relativeY / popup.rowHeight);
          if (index >= 0 &&
              index < static_cast<int>(std::size(kSchoolDaysUpscalerOptions)))
            ktrfModernDisplayPanel.hoveredUpscaler = index;
        }
      }
    }
    return SchoolDaysPointInRect(x, y, panel);
  }

  if (converted.button.button != SDL_BUTTON_LEFT) return false;

  if (ktrfModernDisplayPanel.resolutionListOpen) {
    const auto resolutions = SchoolDaysCurrentResolutions();
    const auto popup = SchoolDaysResolutionPopup(panel);
    if (SchoolDaysPointInRect(x, y, popup.rect)) {
      const float relativeY = y - popup.rect.y - popup.headerHeight;
      if (relativeY >= 0.0f) {
        const int visible = static_cast<int>(relativeY / popup.rowHeight);
        const int index = ktrfModernDisplayPanel.resolutionScroll + visible;
        if (visible >= 0 && visible < popup.visibleRows && index >= 0 &&
            index < static_cast<int>(resolutions.size())) {
          (void)ApplySchoolDaysResolutionSelection(index);
          ktrfModernDisplayPanel.resolutionListOpen = false;
          ktrfModernDisplayPanel.hoveredResolution = -1;
        }
      }
      return true;
    }
    if (row != 4) {
      ktrfModernDisplayPanel.resolutionListOpen = false;
      ktrfModernDisplayPanel.hoveredResolution = -1;
    }
  }

  if (ktrfModernDisplayPanel.upscalerListOpen) {
    const auto popup = SchoolDaysUpscalerPopup(panel);
    if (SchoolDaysPointInRect(x, y, popup.rect)) {
      const float relativeY = y - popup.rect.y - popup.headerHeight;
      if (relativeY >= 0.0f) {
        const int index = static_cast<int>(relativeY / popup.rowHeight);
        if (index >= 0 &&
            index < static_cast<int>(std::size(kSchoolDaysUpscalerOptions))) {
          ApplySchoolDaysUpscalerOption(index);
          ktrfModernDisplayPanel.upscalerListOpen = false;
          ktrfModernDisplayPanel.hoveredUpscaler = -1;
        }
      }
      return true;
    }
    if (row != 5) {
      ktrfModernDisplayPanel.upscalerListOpen = false;
      ktrfModernDisplayPanel.hoveredUpscaler = -1;
    }
  }

  if (!SchoolDaysPointInRect(x, y, panel)) return false;
  if (row <= 0 || row > 5) return true;

  if (row == 1) {
    ktrfModernDisplayPanel.resolutionListOpen = false;
    ktrfModernDisplayPanel.upscalerListOpen = false;
    const int next = (std::clamp(global.config.contentAspectPreference.load(), 0, 2) + 1) % 3;
    global.config.contentAspectPreference = next;
    global.config.dirtyConfig = true;
    if (!ApplySchoolDaysModernAspectPreference()) return true;
    ktrfSystemUi->Close(); // reopen immediately next Iterate with new resource family
    return true;
  }

  if (row == 2) {
    ktrfModernDisplayPanel.resolutionListOpen = false;
    ktrfModernDisplayPanel.upscalerListOpen = false;
    const int current = std::clamp(global.config.displayMode.load(), 0, 2);
    // UI order: Windowed -> Borderless -> Exclusive.
    int next = 0;
    if (current == static_cast<int>(Kotonoha::DisplayMode::Windowed))
      next = static_cast<int>(Kotonoha::DisplayMode::BorderlessFullscreen);
    else if (current == static_cast<int>(Kotonoha::DisplayMode::BorderlessFullscreen))
      next = static_cast<int>(Kotonoha::DisplayMode::ExclusiveFullscreen);
    else
      next = static_cast<int>(Kotonoha::DisplayMode::Windowed);
    const auto resolutions = SchoolDaysCurrentResolutions();
    int w = 0, h = 0; float hz = 0.0f;
    const int selected = ktrfModernDisplayPanel.selectedResolution;
    if (selected >= 0 && selected < static_cast<int>(resolutions.size())) {
      const auto &r = resolutions[static_cast<size_t>(selected)];
      w = r.width; h = r.height; hz = r.refreshRate;
    }
    if (Kotonoha::SetDisplay(global, static_cast<Kotonoha::DisplayMode>(next),
                             Kotonoha::ResolveDisplayIndex(global.config.displayIndex.load()),
                             w, h, hz)) {
      // The modern physical modes project back to the original two-state
      // WINDOW/FULL control: Windowed => WINDOW, either fullscreen mode => FULL.
      ktrfRetailDisplayBackendState.windowMode =
          next == static_cast<int>(Kotonoha::DisplayMode::Windowed) ? 0 : 1;
      RefreshSchoolDaysRetailDisplayProjection();
      if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
    }
    return true;
  }

  if (row == 3) {
    ktrfModernDisplayPanel.resolutionListOpen = false;
    ktrfModernDisplayPanel.upscalerListOpen = false;
    int displayCount = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);
    if (displays != nullptr && displayCount > 0) {
      const int next = (global.config.displayIndex.load() + 1) % displayCount;
      global.config.displayIndex = next;
      global.config.dirtyConfig = true;
      ktrfModernDisplayPanel.selectedResolution = -1;
      ktrfModernDisplayPanel.resolutionScroll = 0;
      Kotonoha::DisplayMode mode = Kotonoha::DisplayMode::Windowed;
      (void)Kotonoha::GetDisplayMode(global, &mode);
      (void)Kotonoha::SetDisplay(global, mode, displays[next]);
    }
    SDL_free(displays);
    return true;
  }

  if (row == 4) {
    ktrfModernDisplayPanel.upscalerListOpen = false;
    const auto resolutions = SchoolDaysCurrentResolutions();
    if (resolutions.empty()) return true;
    ktrfModernDisplayPanel.resolutionListOpen =
        !ktrfModernDisplayPanel.resolutionListOpen;
    if (ktrfModernDisplayPanel.resolutionListOpen) {
      const auto popup = SchoolDaysResolutionPopup(panel);
      const int selected = std::clamp(
          ktrfModernDisplayPanel.selectedResolution, 0,
          static_cast<int>(resolutions.size()) - 1);
      const int maxScroll = std::max(
          0, static_cast<int>(resolutions.size()) - popup.visibleRows);
      ktrfModernDisplayPanel.resolutionScroll = std::clamp(
          selected - popup.visibleRows / 2, 0, maxScroll);
    }
    return true;
  }

  if (row == 5) {
    ktrfModernDisplayPanel.resolutionListOpen = false;
    ktrfModernDisplayPanel.upscalerListOpen =
        !ktrfModernDisplayPanel.upscalerListOpen;
    ktrfModernDisplayPanel.hoveredUpscaler = -1;
    return true;
  }
  return true;
}

bool ApplySchoolDaysRetailAudioAndUiState() {
  Kotonoha_SetMenuVoiceEnabled(&global, ktrfRetailConfigState.menVoice);
  Kotonoha_SetMessageLayerEnabled(&global,
                                           ktrfRetailConfigState.textView);
  if (global.sound == nullptr) return false;

  auto *sound = static_cast<Kotonoha::Sound *>(global.sound);
  struct ChannelProjection {
    const char *name;
    Kotonoha::SchoolDaysRetailConfigPolicy::NativeAudioBank bank;
  };
  const ChannelProjection projections[] = {
      {"BGM", Kotonoha::SchoolDaysRetailConfigPolicy::NativeAudioBank::Bgm},
      {"Se", Kotonoha::SchoolDaysRetailConfigPolicy::NativeAudioBank::Se},
      {"Voice", Kotonoha::SchoolDaysRetailConfigPolicy::NativeAudioBank::Voice},
  };
  for (const auto &projection : projections) {
    auto *channel = sound->GetChannelByName(projection.name);
    if (channel == nullptr) return false;
    const float gain =
        ktrfRetailConfigState.mute
            ? 0.0f
            : Kotonoha::SchoolDaysRetailConfigPolicy::LinearGain(
                  ktrfRetailConfigState, projection.bank);
    channel->volume.store(gain, std::memory_order_relaxed);
  }
  return true;
}

bool CacheSchoolDaysRetailConfigState(const char *reason) {
  if (ktrfConfigStorage == nullptr) return false;
  std::string error;
  if (!ktrfConfigStorage->CacheState(ktrfRetailConfigState, &error)) {
    SDL_LogError(0, "[SD-CONFIG] cache update failed (%s): %s",
                 reason != nullptr ? reason : "unspecified", error.c_str());
    return false;
  }
  return true;
}

Kotonoha::SchoolDaysReplayModel::Variant SchoolDaysReplayVariant() {
  using Source = Kotonoha::SchoolDaysSaveFeelModel::Variant;
  using Target = Kotonoha::SchoolDaysReplayModel::Variant;
  switch (ktrfUiVariant) {
  case Source::Standard: return Target::Standard;
  case Source::Wide: return Target::Wide;
  case Source::WideFull: return Target::WideFull;
  case Source::WideNote: return Target::WideNote;
  }
  return Target::Standard;
}

Kotonoha::SchoolDaysRouteMapModel::Variant SchoolDaysRouteMapVariant() {
  using Source = Kotonoha::SchoolDaysSaveFeelModel::Variant;
  using Target = Kotonoha::SchoolDaysRouteMapModel::Variant;
  switch (ktrfUiVariant) {
  case Source::Standard: return Target::Standard;
  case Source::Wide: return Target::Wide;
  case Source::WideFull: return Target::WideFull;
  case Source::WideNote: return Target::WideNote;
  }
  return Target::Standard;
}

Kotonoha::SchoolDaysBackLogModel::Variant SchoolDaysBackLogVariant() {
  using Source = Kotonoha::SchoolDaysSaveFeelModel::Variant;
  using Target = Kotonoha::SchoolDaysBackLogModel::Variant;
  switch (ktrfUiVariant) {
  case Source::Standard: return Target::Standard;
  case Source::Wide: return Target::Wide;
  case Source::WideFull: return Target::WideFull;
  case Source::WideNote: return Target::WideNote;
  }
  return Target::Standard;
}

std::u16string Utf8ToUtf16(const char *text) {
  if (text == nullptr || *text == '\0') return {};
  char *converted = SDL_iconv_string("UTF-16LE", "UTF-8", text,
                                    SDL_strlen(text) + 1u);
  if (converted == nullptr) return {};
  const char16_t *units = reinterpret_cast<const char16_t *>(converted);
  size_t count = 0u;
  while (units[count] != 0) ++count;
  std::u16string result(units, units + count);
  SDL_free(converted);
  return result;
}

std::string Utf16ToUtf8(const std::u16string &text) {
  if (text.empty()) return {};
  char *converted = SDL_iconv_string(
      "UTF-8", "UTF-16LE", reinterpret_cast<const char *>(text.data()),
      text.size() * sizeof(char16_t));
  if (converted == nullptr) return {};
  std::string result(converted);
  SDL_free(converted);
  return result;
}

void AppendSchoolDaysBackLogText(void *, const char *resolvedCharacter,
                                 const char *resolvedText) {
  std::lock_guard<std::mutex> lock(ktrfBackLogMutex);
  ktrfBackLogStore.AppendLogMessage(ktrfBackLogHistoryIndex++,
                                    Utf8ToUtf16(resolvedCharacter),
                                    Utf8ToUtf16(resolvedText));
}

void ResetSchoolDaysBackLog() {
  std::lock_guard<std::mutex> lock(ktrfBackLogMutex);
  ktrfBackLogPresenter.reset();
  ktrfBackLog.reset();
  ktrfBackLogStore.ResetTransient();
  ktrfBackLogHistoryIndex = 0;
  ktrfBackLogLastSceneKey.clear();
  ktrfBackLogOwner = KtrfBackLogOwner::None;
}

void ObserveSchoolDaysBackLogScriptChange() {
  const std::string *sceneKey = nullptr;
  if (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen() &&
      ktrfReplayHostModal == KtrfReplayHostModal::None) {
    sceneKey = &ktrfPlayDataPlayback->CurrentScene().sceneKey;
  } else if (ktrfController != nullptr &&
             ktrfController->FlowState() ==
                 Kotonoha::SchoolDaysFlowController::State::Gameplay &&
             !ktrfController->HasPendingHandoff()) {
    sceneKey = &ktrfController->CurrentScene().sceneKey;
  }
  if (sceneKey == nullptr) return;
  if (sceneKey->empty() || *sceneKey == ktrfBackLogLastSceneKey) return;
  std::lock_guard<std::mutex> lock(ktrfBackLogMutex);
  // Native ScriptChange keeps a transient SceneKey -> manager+0x08 count map.
  // Revisiting a script truncates abandoned-branch LogMessages before that
  // script becomes the active producer again.
  ktrfBackLogStore.ObserveScriptChange(*sceneKey);
  ktrfBackLogLastSceneKey = *sceneKey;
}

bool BackLogPointerToLogical(SDL_Renderer *renderer, const SDL_Event *event,
                             int *logicalX, int *logicalY) {
  if (renderer == nullptr || event == nullptr || logicalX == nullptr ||
      logicalY == nullptr || ktrfBackLog == nullptr ||
      !ktrfBackLog->IsOpen()) {
    return false;
  }
  float wx = 0.0f, wy = 0.0f;
  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    wx = event->motion.x;
    wy = event->motion.y;
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    wx = event->button.x;
    wy = event->button.y;
  } else {
    return false;
  }
  float rx = 0.0f, ry = 0.0f;
  int ow = 0, oh = 0;
  if (!SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &rx, &ry) ||
      !SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh)) {
    return false;
  }
  const auto layout = Kotonoha::SchoolDaysBackLogModel::LayoutFor(
      ktrfBackLog->CurrentOrientation(), ktrfBackLog->CurrentVariant());
  const float scale = std::min(static_cast<float>(ow) / layout.logicalWidth,
                               static_cast<float>(oh) / layout.logicalHeight);
  if (!(scale > 0.0f)) return false;
  const float vx = (static_cast<float>(ow) - layout.logicalWidth * scale) * 0.5f;
  const float vy = (static_cast<float>(oh) - layout.logicalHeight * scale) * 0.5f;
  *logicalX = static_cast<int>((rx - vx) / scale);
  *logicalY = static_cast<int>((ry - vy) / scale);
  return true;
}

bool OpenSchoolDaysBackLog(Kotonoha::Kotonoha* app) {
  if (ktrfBackLog != nullptr && ktrfBackLog->IsOpen()) return true;
  if (app == nullptr || global.render == nullptr || !ktrfUiVariantSpecified) {
    return false;
  }

  Kotonoha::Gameplay *current = nullptr;
  Kotonoha::KtrfSceneTransaction *transaction = nullptr;
  KtrfBackLogOwner owner = KtrfBackLogOwner::None;
  if (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen() &&
      ktrfReplayHostModal == KtrfReplayHostModal::None) {
    current = ktrfPlayDataPlayback->CurrentGameplay();
    transaction = ktrfReplaySceneTransaction.get();
    owner = KtrfBackLogOwner::PlayData;
  } else if (ktrfController != nullptr && ktrfSceneTransaction != nullptr &&
             ktrfController->FlowState() ==
                 Kotonoha::SchoolDaysFlowController::State::Gameplay) {
    current = ktrfController->CurrentGameplay();
    transaction = ktrfSceneTransaction.get();
    owner = KtrfBackLogOwner::Narrative;
  }
  if (current == nullptr || transaction == nullptr || owner == KtrfBackLogOwner::None)
    return false;

  TTF_Font* primaryFont = nullptr;
  TTF_Font* japaneseFallback = nullptr;
  if (const auto it = app->fonts.find(
          Kotonoha::SchoolDaysSubtitleTypography::PrimaryFontKey);
      it != app->fonts.end())
    primaryFont = it->second;
  if (const auto it = app->fonts.find(
          Kotonoha::SchoolDaysSubtitleTypography::JapaneseFallbackFontKey);
      it != app->fonts.end())
    japaneseFallback = it->second;
  if (primaryFont == nullptr) primaryFont = japaneseFallback;
  if (primaryFont == nullptr) {
    SDL_LogError(0, "[SD-BACKLOG] UI font chain is unavailable");
    return false;
  }
  SDL_SetAtomicInt(&global.textLogCaptureEnabled, 0);
  SDL_SetRenderTarget(global.render, nullptr);
  // Immediately after a CurrentScene swap the transaction may already own the
  // only valid outgoing composition as a carry lease.  That lease is exactly
  // the frozen frame BackLog needs; trying to prepare a second frame is
  // rejected by KtrfSceneTransaction and must not turn this click into a fatal
  // application result.
  if (!transaction->HasLease() &&
      !transaction->PrepareExplicit(global.render, current, "backlog-modal")) {
    SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);
    return false;
  }
  auto model = std::make_unique<Kotonoha::SchoolDaysBackLogModel>();
  auto presenter = std::make_unique<Kotonoha::SchoolDaysBackLogPresenter>();
  std::string error;
  {
    std::lock_guard<std::mutex> lock(ktrfBackLogMutex);
    // Shipped FILMENGINE.INI has BackLogType=0; BackLogView's host +0x64
    // branch maps zero to the Horizon resources.
    if (!model->Open(ktrfAssetsRoot, ktrfBackLogStore,
                     Kotonoha::SchoolDaysBackLogModel::Orientation::Horizon,
                     SchoolDaysBackLogVariant(), &error)) {
      transaction->CancelPreparedFor(current, "backlog-open-failed");
      SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);
      SDL_LogError(0, "[SD-BACKLOG] open failed: %s", error.c_str());
      return false;
    }
  }
  if (!presenter->Open(global.render, ktrfAssetsRoot, model->CurrentOrientation(),
                       ktrfSaveProfile.useEnglish, primaryFont,
                       japaneseFallback, &error)) {
    model->Close();
    transaction->CancelPreparedFor(current, "backlog-skin-open-failed");
    SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);
    SDL_LogError(0, "[SD-BACKLOG] skin open failed: %s", error.c_str());
    return false;
  }
  // BackLog owns a frozen snapshot of the current narrative. Exclude the modal
  // wall time from the Gameplay clock as well; the first Gameplay::Main after
  // close reconciles loading/user-pause intent before deciding Resume/Pause.
  current->Pause();
  ktrfBackLogOwner = owner;
  ktrfBackLog = std::move(model);
  ktrfBackLogPresenter = std::move(presenter);
  return true;
}

void CloseSchoolDaysBackLog() {
  Kotonoha::Gameplay *current = nullptr;
  Kotonoha::KtrfSceneTransaction *transaction = nullptr;
  if (ktrfBackLogOwner == KtrfBackLogOwner::PlayData &&
      ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()) {
    current = ktrfPlayDataPlayback->CurrentGameplay();
    transaction = ktrfReplaySceneTransaction.get();
  } else if (ktrfBackLogOwner == KtrfBackLogOwner::Narrative &&
             ktrfController != nullptr) {
    current = ktrfController->CurrentGameplay();
    transaction = ktrfSceneTransaction.get();
  }
  ktrfBackLogPresenter.reset();
  if (ktrfBackLog != nullptr) ktrfBackLog->Close();
  ktrfBackLog.reset();
  if (transaction != nullptr && current != nullptr) {
    transaction->CancelPreparedFor(current, "backlog-close");
    if (!current->UserPaused(&global)) current->Resume();
  }
  ktrfBackLogOwner = KtrfBackLogOwner::None;
  SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);
}

bool RenderSchoolDaysBackLog(Kotonoha::Kotonoha *app) {
  if (app == nullptr || global.render == nullptr || ktrfBackLog == nullptr ||
      !ktrfBackLog->IsOpen() || ktrfBackLogPresenter == nullptr ||
      !ktrfBackLogPresenter->IsOpen()) {
    return false;
  }
  std::string skinError;
  if (!ktrfBackLogPresenter->Render(global.render, *ktrfBackLog, &skinError)) {
    SDL_LogError(0, "[SD-BACKLOG] native skin render failed: %s",
                 skinError.c_str());
    return false;
  }
  (void)app;
  return true;
}

bool ApplySchoolDaysRouteMapProjection(
    const Kotonoha::SchoolDaysKtrfAppController::RouteMapProjection& projection) {
  if (ktrfSystemUi == nullptr || !ktrfSystemUi->IsRouteMapOpen()) return false;
  const auto page = ktrfSystemUi->RouteMap().CurrentPageInfo();
  if (ktrfSystemUi->RouteMap().Major() != projection.major ||
      ktrfSystemUi->RouteMap().Subpage() != projection.subpage ||
      page.nodeCount <= 0 ||
      page.nodeCount > static_cast<int>(projection.available.size())) {
    return false;
  }
  for (int i = 0; i < page.nodeCount; ++i) {
    ktrfSystemUi->SetRouteMapNodeAvailable(
        i, projection.available[static_cast<size_t>(i)]);
  }
  ktrfSystemUi->SetRouteMapCurrentMarker(projection.currentMarker);
  ktrfRouteMapProjectionMajor = projection.major;
  ktrfRouteMapProjectionSubpage = projection.subpage;
  return true;
}

bool RefreshSchoolDaysRouteMapProjection() {
  if (ktrfController == nullptr || ktrfSystemUi == nullptr ||
      !ktrfSystemUi->IsRouteMapOpen()) {
    return false;
  }
  Kotonoha::SchoolDaysKtrfAppController::RouteMapProjection projection;
  Kotonoha_KtrfError error{};
  if (!ktrfController->BuildRouteMapProjection(
          ktrfSystemUi->RouteMap().Major(), ktrfSystemUi->RouteMap().Subpage(),
          &projection, &error)) {
    SDL_LogError(0, "[SD-ROUTEMAP] page projection failed: %s", error.message);
    return false;
  }
  if (!ApplySchoolDaysRouteMapProjection(projection)) {
    SDL_LogError(0, "[SD-ROUTEMAP] page projection/model mismatch");
    return false;
  }
  return true;
}

bool RefreshSchoolDaysReplayAvailability() {
  if (ktrfController == nullptr || ktrfSystemUi == nullptr ||
      !ktrfSystemUi->IsReplayOpen()) {
    return true;
  }

  Kotonoha_KtrfError error{};
  for (int i = 0; i < Kotonoha::SchoolDaysReplayModel::HSceneUnlockCount(); ++i) {
    const char *key = Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(i);
    int64_t value = 0;
    if (key == nullptr || !ktrfController->ReadGlobalInt(key, &value, &error)) {
      SDL_LogError(0, "[SD-REPLAY] HScene unlock read failed for %s: %s",
                   key != nullptr ? key : "<null>", error.message);
      return false;
    }
    ktrfSystemUi->SetReplayHSceneAvailable(i, value != 0);
  }

  // Native SceneView asks the host +0x9C for the same SaveConfig projection used
  // by SaveLoad: DATE, STORY and optional _Sub comment/name.  Openability still
  // gates the call, but the returned metadata is also the row's visible text.
  if (ktrfSaveStorage == nullptr) {
    SDL_LogError(0, "[SD-REPLAY] PlayData storage is unavailable");
    return false;
  }
  std::vector<Kotonoha::SchoolDaysSaveService::SlotMetadata> replayMetadata(100u);
  for (int slot = 0; slot < 100; ++slot) {
    bool openable = false;
    std::string storageError;
    if (!ktrfSaveStorage->ProbeSaveFile(slot, &openable, &storageError)) {
      SDL_LogError(0, "[SD-REPLAY] PlayData slot %d probe failed: %s", slot,
                   storageError.c_str());
      return false;
    }
    Kotonoha_KtrfError metadataError{};
    if (!ktrfController->ReadSaveSlotMetadata(
            ktrfSaveProfile, slot, openable,
            &replayMetadata[static_cast<size_t>(slot)], &metadataError)) {
      SDL_LogError(0, "[SD-REPLAY] PlayData slot %d metadata failed: %s", slot,
                   metadataError.message);
      return false;
    }
    ktrfSystemUi->SetReplayPlayDataAvailable(
        slot, replayMetadata[static_cast<size_t>(slot)].occupied);
  }
  std::string replayMetadataError;
  if (!ktrfSystemUi->SetReplayPlayDataMetadata(replayMetadata,
                                               &replayMetadataError)) {
    SDL_LogError(0, "[SD-REPLAY] PlayData metadata projection failed: %s",
                 replayMetadataError.c_str());
    return false;
  }

  if (ktrfSystemUi->Replay().SceneCheckOpen()) {
    const int selector = ktrfSystemUi->Replay().SceneCheckKey();
    for (int choice = 0; choice < 4; ++choice) {
      const char *key =
          Kotonoha::SchoolDaysReplayModel::SceneCheckAvailabilityKey(selector,
                                                                     choice);
      bool available = false;
      if (key != nullptr) {
        int64_t value = 0;
        if (!ktrfController->ReadGlobalInt(key, &value, &error)) {
          SDL_LogError(0,
                       "[SD-REPLAY] SceneCheck availability read failed for %s: %s",
                       key, error.message);
          return false;
        }
        available = value != 0;
      }
      ktrfSystemUi->SetReplaySceneCheckChoiceAvailable(choice, available);
    }
  }
  return true;
}

Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState
CurrentSchoolDaysConfigVisualState() {
  Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState state;

  // Wide/Full now project from the authenticated logical/backend state.  They
  // remain completely independent of SDL window size/fullscreen state.
  state.defWideCurrent =
      ktrfRetailDisplayState.effectiveDisplayType == 1 ? 4 : 5;
  state.defFullCurrent =
      ktrfRetailDisplayState.windowMode == 0 ? 6 : 7;
  state.defSkipCurrent = ktrfRetailConfigState.skip ? 8 : 9;
  state.defSuperSkipCurrent = ktrfRetailConfigState.superSkip ? 10 : 11;
  state.defDrawMessageCurrent = ktrfRetailConfigState.textView ? 12 : 13;
  state.soundMenVoiceCurrent = ktrfRetailConfigState.menVoice ? 10 : 11;
  state.soundMuteCurrent = ktrfRetailConfigState.mute ? 12 : 13;
  // ConfigMenu visual order is BGM, SE, Voice; native runtime ids are 2,1,0.
  state.soundLevels = {{ktrfRetailConfigState.bgmVolume,
                        ktrfRetailConfigState.seVolume,
                        ktrfRetailConfigState.voiceVolume}};
  return state;
}

bool EnsureSchoolDaysMenuBar() {
  if (ktrfMenuBar != nullptr && ktrfMenuBar->IsOpen()) return true;
  if (ktrfController == nullptr || global.render == nullptr ||
      !ktrfUiVariantSpecified) {
    return false;
  }

  int64_t feeling001 = 0;
  int64_t feeling002 = 0;
  Kotonoha_KtrfError error{};
  if (!ktrfController->ReadSessionInt("001", &feeling001, &error) ||
      !ktrfController->ReadSessionInt("002", &feeling002, &error) ||
      feeling001 < std::numeric_limits<int>::min() ||
      feeling001 > std::numeric_limits<int>::max() ||
      feeling002 < std::numeric_limits<int>::min() ||
      feeling002 > std::numeric_limits<int>::max()) {
    SDL_LogError(0, "[SD-MENUBAR] initial feeling read failed: %s",
                 error.message);
    return false;
  }

  auto presenter = std::make_unique<Kotonoha::SchoolDaysMenuBarPresenter>();
  std::string uiError;
  if (!presenter->Open(global.render, ktrfAssetsRoot, ktrfUiVariant,
                       static_cast<int>(feeling001),
                       static_cast<int>(feeling002), &uiError)) {
    SDL_LogError(0, "[SD-MENUBAR] open failed: %s", uiError.c_str());
    return false;
  }
  // Native ordinary Gameplay host projection: full bar enabled (+140==0),
  // transport unlocked (+110==0), menu actions uninhibited (+104==0), and the
  // speed row capability present (+88!=0). SuperSkip is the independent Config
  // flag and must not gate speed controls. Playback rate is projected from the
  // exact retail index table {1,2,4,12,24} into Time + per-decoder audio/video.
  presenter->SetRuntimeState(ktrfRetailConfigState.autoMode,
                             global.config.paused, ktrfSchoolDaysSpeedIndex,
                             ktrfRetailConfigState.superSkip,
                             true, false, false, false);
  ktrfMenuBar = std::move(presenter);
  return true;
}

void StopSchoolDaysTitleBgm() {
  if (ktrfTitleAudio != nullptr && ktrfTitleBgm != nullptr) {
    ktrfTitleAudio->RemoveMedia(ktrfTitleBgm);
  }
  ktrfTitleBgm = nullptr;
  ktrfTitleAudio.reset();
  if (ktrfTitleAudioClock != nullptr) {
    Kotonoha_timeDestroy(ktrfTitleAudioClock);
    ktrfTitleAudioClock = nullptr;
  }
  ktrfFrontendBgmOwner = KtrfFrontendBgmOwner::None;
}

bool StartSchoolDaysFrontendBgm(const std::string &stem,
                                KtrfFrontendBgmOwner owner,
                                const char *label) {
  if (ktrfTitleBgm != nullptr && ktrfFrontendBgmOwner == owner) return true;
  if (stem.empty() || global.sound == nullptr ||
      owner == KtrfFrontendBgmOwner::None) {
    return false;
  }
  if (ktrfTitleBgm != nullptr || ktrfTitleAudio != nullptr ||
      ktrfTitleAudioClock != nullptr) {
    StopSchoolDaysTitleBgm();
  }

  const auto resolved = Kotonoha::ResolveSchoolDaysAssetPath(
      ktrfAssetsRoot.c_str(), stem + "_loop.ogg");
  if (!resolved.present || resolved.collision) {
    SDL_LogError(0, "[SD-FRONTEND-BGM] %s loop asset could not be resolved",
                 label != nullptr ? label : "frontend");
    return false;
  }

  ktrfTitleAudioClock = Kotonoha_timeNew(false);
  if (ktrfTitleAudioClock == nullptr) return false;
  ktrfTitleAudio = std::make_unique<Kotonoha::Audio>(
      static_cast<Kotonoha::Sound *>(global.sound), ktrfTitleAudioClock);
  ktrfTitleBgm = ktrfTitleAudio->AddMedia(
      resolved.physical.c_str(), 0u, 0u, true, "BGM", true);
  if (ktrfTitleBgm == nullptr) {
    StopSchoolDaysTitleBgm();
    return false;
  }
  ktrfFrontendBgmOwner = owner;
  SDL_Log("[SD-FRONTEND-BGM] owner=%s stem=%s",
          label != nullptr ? label : "frontend", stem.c_str());
  return true;
}

bool StartSchoolDaysTitleBgm(
    const Kotonoha::SchoolDaysEndBackgroundResolver::Profile &profile) {
  return StartSchoolDaysFrontendBgm(profile.titleBgmStem,
                                    KtrfFrontendBgmOwner::Title, "Title");
}

bool StartSchoolDaysSystemBgm() {
  if (ktrfTitleBgm != nullptr &&
      ktrfFrontendBgmOwner == KtrfFrontendBgmOwner::System) {
    return true;
  }
  Kotonoha::SchoolDaysEndBackgroundResolver::Profile profile;
  std::string error;
  if (!Kotonoha::SchoolDaysTitleProfile::Load(ktrfAssetsRoot, &profile, &error)) {
    SDL_LogError(0, "[SD-REPLAY] SystemBGM profile load failed: %s",
                 error.c_str());
    return false;
  }
  return StartSchoolDaysFrontendBgm(profile.systemBgmStem,
                                    KtrfFrontendBgmOwner::System, "System");
}

const std::string *SchoolDaysUiSfxStem(
    Kotonoha::SchoolDaysSystemUi::UiSfxCue cue) {
  using Cue = Kotonoha::SchoolDaysSystemUi::UiSfxCue;
  switch (cue) {
  case Cue::Cancel: return &ktrfSaveProfile.seCancel;
  case Cue::Select: return &ktrfSaveProfile.seSelect;
  case Cue::Click: return &ktrfSaveProfile.seClick;
  case Cue::Up: return &ktrfSaveProfile.seUp;
  case Cue::Down: return &ktrfSaveProfile.seDown;
  case Cue::View: return &ktrfSaveProfile.seView;
  case Cue::Open: return &ktrfSaveProfile.seOpen;
  case Cue::None: break;
  }
  return nullptr;
}

bool PlaySchoolDaysUiSfx(Kotonoha::SchoolDaysSystemUi::UiSfxCue cue) {
  if (cue == Kotonoha::SchoolDaysSystemUi::UiSfxCue::None) return true;
  const std::string *stem = SchoolDaysUiSfxStem(cue);
  if (stem == nullptr || stem->empty()) return false;

  // FILMENGINE.INI stores system-SE stems. The shipped School Days HQ assets
  // for all seven authenticated presets are Ogg/Vorbis files; the resolver is
  // case-insensitive and therefore preserves the original logical stem while
  // selecting the actual packaged filename.
  const auto resolved = Kotonoha::ResolveSchoolDaysAssetPath(
      ktrfAssetsRoot.c_str(), *stem + ".ogg");
  if (!resolved.present || resolved.collision) {
    SDL_LogError(0, "[SD-UI-SE] preset id=%d asset could not be resolved",
                 static_cast<int>(cue));
    return false;
  }

  Kotonoha::Audio *audio = ktrfTitleAudio.get();
  if (audio == nullptr && ktrfReplayPlayback != nullptr &&
      ktrfReplayPlayback->IsOpen()) {
    if (Kotonoha::Gameplay *gameplay = ktrfReplayPlayback->CurrentGameplay())
      audio = gameplay->audio;
  }
  if (audio == nullptr && ktrfPlayDataPlayback != nullptr &&
      ktrfPlayDataPlayback->IsOpen()) {
    if (Kotonoha::Gameplay *gameplay = ktrfPlayDataPlayback->CurrentGameplay())
      audio = gameplay->audio;
  }
  if (audio == nullptr && ktrfController != nullptr) {
    if (Kotonoha::Gameplay *gameplay = ktrfController->CurrentGameplay())
      audio = gameplay->audio;
  }
  if (audio == nullptr) return false;
  if (audio->AddMedia(resolved.physical.c_str(), 0u, 0u, false, "Se", true) ==
      nullptr) {
    SDL_LogError(0, "[SD-UI-SE] preset id=%d playback failed",
                 static_cast<int>(cue));
    return false;
  }
  return true;
}

bool FlushSchoolDaysGlobalFlag(const char *reason) {
  if (ktrfController == nullptr || ktrfSaveStorage == nullptr ||
      !ktrfController->HasGlobalFlag()) {
    return true;
  }
  std::vector<uint8_t> bytes;
  Kotonoha_KtrfError error{};
  if (!ktrfController->SerializeGlobalFlag(&bytes, &error)) {
    SDL_LogError(0, "[SD-SAVE] GlobalFlag %s serialize failed: %s",
                 reason != nullptr ? reason : "flush", error.message);
    return false;
  }
  std::string storageError;
  if (!ktrfSaveStorage->WriteGlobalFlagBytes(bytes, &storageError)) {
    SDL_LogError(0, "[SD-SAVE] GlobalFlag %s write failed: %s",
                 reason != nullptr ? reason : "flush", storageError.c_str());
    return false;
  }
  return true;
}

bool CurrentSaveLocalTime(Kotonoha::SchoolDaysSaveMetadata::LocalTime *out) {
  if (out == nullptr) return false;
  SDL_Time now = 0;
  SDL_DateTime local{};
  if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &local, true)) {
    return false;
  }
  out->year = local.year;
  out->month = local.month;
  out->day = local.day;
  out->weekday = local.day_of_week;
  out->hour = local.hour;
  out->minute = local.minute;
  return true;
}

bool ReadSaveSlotMetadataForUi(
    int slot, Kotonoha::SchoolDaysSaveService::SlotMetadata *metadata) {
  if (ktrfController == nullptr || ktrfSaveStorage == nullptr ||
      metadata == nullptr) {
    return false;
  }
  bool openable = false;
  std::string storageError;
  if (!ktrfSaveStorage->ProbeSaveFile(slot, &openable, &storageError)) {
    SDL_LogError(0, "[SD-SAVELOAD] slot probe failed: %s",
                 storageError.c_str());
    return false;
  }
  Kotonoha_KtrfError error{};
  if (!ktrfController->ReadSaveSlotMetadata(ktrfSaveProfile, slot, openable,
                                            metadata, &error)) {
    SDL_LogError(0, "[SD-SAVELOAD] metadata read failed: %s", error.message);
    return false;
  }
  return true;
}

bool RefreshSaveLoadMetadataForUi() {
  if (ktrfSystemUi == nullptr || !ktrfSystemUi->IsSaveLoadOpen()) return false;
  const int page = ktrfSystemUi->SaveLoad().Page();
  if (page < 0 || page > 9) return false;
  std::vector<Kotonoha::SchoolDaysSaveService::SlotMetadata> rows(10u);
  for (int row = 0; row < 10; ++row) {
    if (!ReadSaveSlotMetadataForUi(page * 10 + row,
                                   &rows[static_cast<size_t>(row)])) {
      // A single corrupt/openable slot must not poison the entire page. The
      // default metadata row is unavailable/empty, matching the safest UI
      // projection while preserving every other valid slot.
      rows[static_cast<size_t>(row)] = {};
    }
  }
  std::string uiError;
  if (!ktrfSystemUi->SetSaveLoadMetadata(rows, &uiError)) {
    SDL_LogError(0, "[SD-SAVELOAD] metadata atlas refresh failed: %s",
                 uiError.c_str());
    return false;
  }
  ktrfSaveLoadMetadataPage = page;
  return true;
}

bool ExecuteSaveLoadWrite(
    const Kotonoha::SchoolDaysSaveLoadModel::Output &output) {
  if (output.action !=
          Kotonoha::SchoolDaysSaveLoadModel::Action::SaveSlotRequested ||
      ktrfController == nullptr || ktrfSaveStorage == nullptr) {
    return false;
  }

  Kotonoha::SchoolDaysSaveMetadata::LocalTime time;
  if (!CurrentSaveLocalTime(&time)) {
    SDL_LogError(0, "[SD-SAVELOAD] local time query failed");
    return false;
  }
  Kotonoha::SchoolDaysSaveMetadata::Result metadata;
  Kotonoha_KtrfError error{};
  if (!ktrfController->BuildSaveMetadata(time, ktrfSaveProfile.useEnglish,
                                         output.comment, &metadata, &error)) {
    SDL_LogError(0, "[SD-SAVELOAD] metadata build failed: %s", error.message);
    return false;
  }

  Kotonoha::SchoolDaysSaveService::Plan plan;
  if (!ktrfController->PlanSaveSlot(ktrfSaveProfile, output.slot,
                                    metadata.primary, metadata.sub, &plan,
                                    &error)) {
    SDL_LogError(0, "[SD-SAVELOAD] save plan failed: %s", error.message);
    return false;
  }
  std::string storageError;
  if (!ktrfSaveStorage->WriteSaveFile(plan, &storageError)) {
    SDL_LogError(0, "[SD-SAVELOAD] SaveFile write failed: %s",
                 storageError.c_str());
    return false;
  }
  // Native 0x42AEA0 publishes the two type-8 SaveConfig values after SLog
  // success, then flushes GlobalFlag.  Do not roll this live mutation back if
  // the final disk flush fails; the original has already crossed that boundary.
  if (!ktrfController->CommitSavePlan(plan, &error)) {
    SDL_LogError(0, "[SD-SAVELOAD] metadata commit failed: %s", error.message);
    return false;
  }
  if (!ktrfSaveStorage->WriteGlobalFlag(plan, &storageError)) {
    SDL_LogError(0, "[SD-SAVELOAD] GlobalFlag flush failed: %s",
                 storageError.c_str());
    return false;
  }
  if (ktrfController->FlowState() ==
      Kotonoha::SchoolDaysFlowController::State::SaveLoad) {
    if (!ktrfController->SubmitSaveLoadResult(
            Kotonoha::SchoolDaysFlowController::SaveLoadResult::StayOpen,
            &error)) {
      SDL_LogError(0, "[SD-SAVELOAD] StayOpen result failed: %s", error.message);
      return false;
    }
  } else if (ktrfController->FlowState() !=
             Kotonoha::SchoolDaysFlowController::State::GameplaySave) {
    SDL_LogError(0, "[SD-SAVELOAD] SAVE write completed outside a SAVE view");
    return false;
  }
  ktrfSaveLoadMetadataPage = -1;
  return true;
}

bool ExecutePlayDataSaveLoadWrite(
    const Kotonoha::SchoolDaysSaveLoadModel::Output &output) {
  if (output.action !=
          Kotonoha::SchoolDaysSaveLoadModel::Action::SaveSlotRequested ||
      ktrfPlayDataPlayback == nullptr || !ktrfPlayDataPlayback->IsOpen() ||
      ktrfReplayHostModal != KtrfReplayHostModal::Save ||
      ktrfSaveStorage == nullptr || ktrfController == nullptr) {
    return false;
  }

  Kotonoha::SchoolDaysSaveMetadata::LocalTime time;
  if (!CurrentSaveLocalTime(&time)) return false;
  Kotonoha::SchoolDaysSaveMetadata::Result metadata;
  Kotonoha_KtrfError error{};
  auto &session = ktrfPlayDataPlayback->Session();
  if (!Kotonoha::SchoolDaysSaveMetadata::Build(
          session.State(), time, ktrfSaveProfile.useEnglish, output.comment,
          &metadata, &error)) {
    SDL_LogError(0, "[SD-PLAYDATA-SAVE] metadata build failed: %s",
                 error.message);
    return false;
  }

  Kotonoha::SchoolDaysSaveService::Plan plan;
  if (!Kotonoha::SchoolDaysSaveService::PlanSlot(
          ktrfSaveProfile, output.slot, metadata.primary, metadata.sub,
          session.Persistence(), session.State(), &plan, &error)) {
    SDL_LogError(0, "[SD-PLAYDATA-SAVE] save plan failed: %s", error.message);
    return false;
  }
  std::string storageError;
  if (!ktrfSaveStorage->WriteSaveFile(plan, &storageError)) {
    SDL_LogError(0, "[SD-PLAYDATA-SAVE] SaveFile write failed: %s",
                 storageError.c_str());
    return false;
  }
  if (!Kotonoha::SchoolDaysSaveService::Commit(
          plan, &session.Persistence(), &error)) {
    SDL_LogError(0, "[SD-PLAYDATA-SAVE] private metadata commit failed: %s",
                 error.message);
    return false;
  }
  if (!ktrfSaveStorage->WriteGlobalFlag(plan, &storageError)) {
    SDL_LogError(0, "[SD-PLAYDATA-SAVE] GlobalFlag flush failed: %s",
                 storageError.c_str());
    return false;
  }
  // Retail has one active FILMEngine/global flag owner.  Kotonoha intentionally
  // isolates PlayData's session/router, so publish only the committed global
  // image back to the retained controller after disk success; never copy the
  // private route/session state into normal narrative gameplay.
  if (!ktrfController->LoadGlobalFlag(plan.globalFlagBytes, &error)) {
    SDL_LogError(0, "[SD-PLAYDATA-SAVE] shared GlobalFlag publish failed: %s",
                 error.message);
    return false;
  }
  ktrfSaveLoadMetadataPage = -1;
  return true;
}

bool ClosePlayDataSaveLoadUi() {
  if (ktrfPlayDataPlayback == nullptr || !ktrfPlayDataPlayback->IsOpen() ||
      ktrfSystemUi == nullptr ||
      (ktrfReplayHostModal != KtrfReplayHostModal::Save &&
       ktrfReplayHostModal != KtrfReplayHostModal::Load)) {
    return false;
  }
  Kotonoha::Gameplay *current = ktrfPlayDataPlayback->CurrentGameplay();
  ktrfSystemUi->Close();
  ktrfSaveLoadMetadataPage = -1;
  ktrfReplayHostModal = KtrfReplayHostModal::None;
  if (current != nullptr && !current->UserPaused(&global)) current->Resume();
  return true;
}

bool ExecutePlayDataLoadSlot(int slot) {
  if (slot < 0 || slot >= 100 || ktrfPlayDataPlayback == nullptr ||
      !ktrfPlayDataPlayback->IsOpen() || ktrfSaveStorage == nullptr ||
      ktrfReplayHostModal != KtrfReplayHostModal::Load) {
    return false;
  }
  std::vector<uint8_t> bytes;
  std::string storageError;
  if (!ktrfSaveStorage->ReadSaveFile(slot, &bytes, &storageError)) {
    SDL_LogError(0, "[SD-PLAYDATA-LOAD] SaveFile read failed slot=%d: %s",
                 slot, storageError.c_str());
    return false;
  }

  Kotonoha_KtrfError error{};
  const bool oracleOnly = ktrfPlayDataPlayback->ChoiceReplayLatchArmed();
  if (oracleOnly) {
    if (!ktrfPlayDataPlayback->ReplaceRecordedChoiceOracle(bytes, &error)) {
      SDL_LogError(0, "[SD-PLAYDATA-LOAD] slot=%d tag4 oracle apply failed: %s",
                   slot, error.message);
      return false;
    }
    return ClosePlayDataSaveLoadUi();
  }

  // Once PlayData's +0x98 one-shot latch has been consumed, native SceneView
  // stops using the tag4-only reader. It performs the ordinary full SaveFile
  // load on the active FILMEngine and then its outer mode resolver leaves
  // SceneView for mode 1 (normal gameplay). Kotonoha keeps PlayData in an
  // isolated KTRF session, so the faithful equivalent is to load the same slot
  // transactionally into the retained narrative controller and then retire the
  // temporary PlayData owner. Carry the active PlayData GlobalFlag image across
  // that ownership boundary before the first loaded gameplay frame runs.
  std::vector<uint8_t> playDataGlobal;
  const bool hasPlayDataGlobal =
      ktrfPlayDataPlayback->Session().Persistence().HasGlobalFlag();
  if (hasPlayDataGlobal &&
      !ktrfPlayDataPlayback->Session().SerializeGlobalFlag(&playDataGlobal,
                                                           &error)) {
    SDL_LogError(0, "[SD-PLAYDATA-LOAD] active GlobalFlag snapshot failed: %s",
                 error.message);
    return false;
  }
  if (ktrfController == nullptr || !ktrfController->LoadSaveFile(bytes, &error)) {
    SDL_LogError(0, "[SD-PLAYDATA-LOAD] normal gameplay promotion failed slot=%d: %s",
                 slot, error.message);
    return false;
  }
  if (hasPlayDataGlobal &&
      !ktrfController->LoadGlobalFlag(playDataGlobal, &error)) {
    // The bytes were produced by this same runtime immediately above, so this
    // is an invariant failure after the full-load transaction has already
    // crossed into normal gameplay. Finish the owner handoff rather than leave
    // two visible owners alive, and surface the diagnostic loudly.
    SDL_LogError(0, "[SD-PLAYDATA-LOAD] promoted GlobalFlag apply failed: %s",
                 error.message);
  }

  if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
  if (ktrfReplaySceneTransaction != nullptr) ktrfReplaySceneTransaction->Reset();
  ktrfSystemUi->Close();
  ktrfSaveLoadMetadataPage = -1;
  ktrfReplayHostModal = KtrfReplayHostModal::None;
  ktrfPlayDataPlayback.reset();
  ResetSchoolDaysBackLog();
  SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);
  ktrfMenuBackStartGameplay = nullptr;
  ktrfMenuBar.reset();
  return true;
}

bool CloseGameplaySaveLoadUi() {
  if (ktrfController == nullptr || ktrfSystemUi == nullptr) return false;
  const auto state = ktrfController->FlowState();
  if (state != Kotonoha::SchoolDaysFlowController::State::GameplaySave &&
      state != Kotonoha::SchoolDaysFlowController::State::GameplayLoad) {
    return false;
  }
  Kotonoha_KtrfError error{};
  if (!ktrfController->CloseGameplayModal(&error)) {
    SDL_LogError(0, "[SD-SAVELOAD] Gameplay modal close failed: %s",
                 error.message);
    return false;
  }
  if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
  ktrfSystemUi->Close();
  ktrfSaveLoadMetadataPage = -1;
  return true;
}

bool CloseSaveLoadUi() {
  if (ktrfController == nullptr || ktrfSystemUi == nullptr) return false;
  Kotonoha::Gameplay *before = ktrfController->CurrentGameplay();
  const std::string sourceSceneKey = ktrfController->CurrentScene().sceneKey;
  bool prepared = false;
  if (before != nullptr && ktrfSceneTransaction != nullptr &&
      global.render != nullptr) {
    ktrfSceneTransaction->Reset();
    SDL_SetRenderTarget(global.render, nullptr);
    SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
    if (!SDL_RenderClear(global.render)) return false;
    std::string uiError;
    if (!ktrfSystemUi->Render(global.render, &uiError)) {
      SDL_LogError(0, "[SD-SAVELOAD] close composition failed: %s",
                   uiError.c_str());
      return false;
    }
    prepared = ktrfSceneTransaction->PrepareExplicit(
        global.render, before, "saveload-close");
    if (!prepared) return false;
  }

  Kotonoha_KtrfError error{};
  if (!ktrfController->SubmitSaveLoadResult(
          Kotonoha::SchoolDaysFlowController::SaveLoadResult::Close, &error)) {
    if (prepared && ktrfSceneTransaction != nullptr) {
      ktrfSceneTransaction->CancelPreparedFor(before, "saveload-close-failed");
    }
    SDL_LogError(0, "[SD-SAVELOAD] close result failed: %s", error.message);
    return false;
  }

  if (ktrfSceneTransaction != nullptr) {
    if (ktrfController->FlowState() ==
        Kotonoha::SchoolDaysFlowController::State::Gameplay) {
      Kotonoha::Gameplay *after = ktrfController->CurrentGameplay();
      if (before == nullptr || after == nullptr || after == before ||
          !ktrfSceneTransaction->CommitIfSwapped(
              before, after, "saveload-close")) {
        return false;
      }
      // The TELOP overlay is presentation-only.  A route/handoff that is not
      // covered by the still-partial retail TELOP evidence must never turn a
      // successful SaveLoad close into a narrative failure.
      (void)ArmSchoolDaysEpisodeOverlay(
          sourceSceneKey, ktrfController->CurrentScene().sceneKey, after);
    } else {
      // Terminal SaveLoad closes into TitleView without a narrative swap.
      ktrfSceneTransaction->Reset();
      ktrfMenuBackStartGameplay = nullptr;
      ktrfMenuBar.reset();
    }
  }
  ktrfSystemUi->Close();
  ktrfSaveLoadMetadataPage = -1;
  return true;
}

bool CancelTitleLoadUi() {
  if (ktrfController == nullptr || ktrfSystemUi == nullptr ||
      ktrfController->FlowState() !=
          Kotonoha::SchoolDaysFlowController::State::TitleLoad) {
    return false;
  }
  Kotonoha_KtrfError error{};
  if (!ktrfController->CancelTitleLoad(&error)) {
    SDL_LogError(0, "[SD-LOAD] cancel-to-title failed: %s", error.message);
    return false;
  }
  if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
  ktrfSystemUi->Close();
  ktrfSaveLoadMetadataPage = -1;
  return true;
}

bool ApplySchoolDaysFrontendHandoff() {
  const uint64_t now = SDL_GetTicks();
  if (ktrfFrontendHandoff == KtrfFrontendHandoff::None ||
      !ktrfFrontendTransition.NeedsHandoff(now)) {
    return true;
  }
  if (ktrfController == nullptr || ktrfSystemUi == nullptr) return false;

  Kotonoha_KtrfError error{};
  switch (ktrfFrontendHandoff) {
  case KtrfFrontendHandoff::TitleStart:
    ktrfMenuBackStartGameplay = nullptr;
    if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
    if (!ktrfController->StartNewGame(0, &error)) {
      SDL_LogError(0, "[SD-TITLE] faded new-game start failed: %s", error.message);
      return false;
    }
    ResetSchoolDaysBackLog();
    SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);
    StopSchoolDaysTitleBgm();
    ktrfSystemUi->Close();
    ktrfMenuBar.reset();
    break;
  case KtrfFrontendHandoff::TitleLoad:
    if (!ktrfController->EnterTitleLoad(&error)) {
      SDL_LogError(0, "[SD-TITLE] faded LOAD transition failed: %s", error.message);
      return false;
    }
    if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
    ktrfSystemUi->Close();
    ktrfSaveLoadMetadataPage = -1;
    break;
  case KtrfFrontendHandoff::TitleConfig:
    if (!ktrfController->EnterTitleConfig(&error)) {
      SDL_LogError(0, "[SD-TITLE] faded Config transition failed: %s", error.message);
      return false;
    }
    if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
    ktrfConfigPage = Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
    ktrfSystemUi->Close();
    break;
  case KtrfFrontendHandoff::TitleReplay:
    if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
    ktrfTitleReplayActive = true;
    StopSchoolDaysTitleBgm();
    if (!StartSchoolDaysSystemBgm()) return false;
    ktrfSystemUi->Close();
    break;
  case KtrfFrontendHandoff::LoadToTitle:
    if (!CancelTitleLoadUi()) return false;
    break;
  case KtrfFrontendHandoff::ConfigToTitle:
    if (!CacheSchoolDaysRetailConfigState("Config close")) return false;
    if (!ktrfController->CloseTitleConfig(&error)) {
      SDL_LogError(0, "[SD-CONFIG] faded close transition failed: %s",
                   error.message);
      return false;
    }
    ktrfSystemUi->Close();
    break;
  case KtrfFrontendHandoff::ReplayToTitle:
    StopSchoolDaysTitleBgm();
    ktrfTitleReplayActive = false;
    ktrfSystemUi->Close();
    break;
  case KtrfFrontendHandoff::None:
    break;
  }

  ktrfFrontendHandoff = KtrfFrontendHandoff::None;
  ktrfFrontendTransition.CompleteHandoff(now);
  return true;
}

bool RenderSchoolDaysFrontendTransition() {
  const uint64_t now = SDL_GetTicks();
  if (!ktrfFrontendTransition.Active(now)) return true;

  int outputWidth = 0;
  int outputHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(global.render, &outputWidth, &outputHeight) ||
      outputWidth <= 0 || outputHeight <= 0) {
    return false;
  }

  int logicalWidth = 800;
  int logicalHeight = 600;
  using Variant = Kotonoha::SchoolDaysSaveFeelModel::Variant;
  switch (ktrfUiVariant) {
  case Variant::Standard: logicalWidth = 800; logicalHeight = 600; break;
  case Variant::Wide: logicalWidth = 800; logicalHeight = 450; break;
  case Variant::WideFull: logicalWidth = 1280; logicalHeight = 720; break;
  case Variant::WideNote: logicalWidth = 1024; logicalHeight = 576; break;
  }
  const float scale = std::min(static_cast<float>(outputWidth) / logicalWidth,
                               static_cast<float>(outputHeight) / logicalHeight);
  const SDL_FRect rect = {
      (outputWidth - logicalWidth * scale) * 0.5f,
      (outputHeight - logicalHeight * scale) * 0.5f,
      logicalWidth * scale, logicalHeight * scale};
  const uint8_t alpha = ktrfFrontendTransition.Alpha(now);
  const bool white = ktrfFrontendTransition.CurrentColor() ==
                     Kotonoha::SchoolDaysFrontendTransition::Color::White;
  const uint8_t component = white ? 255u : 0u;
  if (!SDL_SetRenderDrawBlendMode(global.render, SDL_BLENDMODE_BLEND) ||
      !SDL_SetRenderDrawColor(global.render, component, component, component, alpha) ||
      !SDL_RenderFillRect(global.render, &rect)) {
    return false;
  }
  return SDL_SetRenderDrawBlendMode(global.render, SDL_BLENDMODE_NONE);
}

bool ExecuteSaveLoadLoadSlot(int slot) {
  if (slot < 0 || slot >= 100 || ktrfController == nullptr ||
      ktrfSaveStorage == nullptr || ktrfSystemUi == nullptr) {
    return false;
  }
  const auto flowState = ktrfController->FlowState();
  if (flowState != Kotonoha::SchoolDaysFlowController::State::TitleLoad &&
      flowState != Kotonoha::SchoolDaysFlowController::State::GameplayLoad) {
    return false;
  }

  std::vector<uint8_t> bytes;
  std::string storageError;
  if (!ktrfSaveStorage->ReadSaveFile(slot, &bytes, &storageError)) {
    SDL_LogError(0, "[SD-LOAD] SaveFile read failed slot=%d: %s", slot,
                 storageError.c_str());
    return false;
  }

  Kotonoha_KtrfError error{};
  if (!ktrfController->LoadSaveFile(bytes, &error)) {
    SDL_LogError(0, "[SD-LOAD] SaveFile apply failed slot=%d: %s", slot,
                 error.message);
    return false;
  }
  // Loading a checkpoint must never synthesize an episode card.  The retail
  // card belongs to the natural callback_38 handoff only; loading 01/..A00 in
  // the original immediately presents the saved scene without Episode 2.
  ResetSchoolDaysEpisodeOverlay();
  // Only a successful persistence transaction actually replaces Gameplay.
  // Keep any existing narrative lease intact on parse/apply failure so closing
  // the still-open LOAD view can resume the same live scene safely.
  if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();

  // Native load clears the transient BackLog manager before rebuilding the
  // persisted SLog collections. Type-2 LogMessage rows are never serialized.
  ResetSchoolDaysBackLog();
  SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);

  // LOAD is a child of the retained Title frontend, so its BGM must keep the
  // same decoder/position while the SaveLoad view is open.  Only a successful
  // TitleLoad -> Gameplay transition leaves that owner and tears the media down.
  if (flowState == Kotonoha::SchoolDaysFlowController::State::TitleLoad) {
    StopSchoolDaysTitleBgm();
  }

  ktrfMenuBackStartGameplay = nullptr;
  ktrfSystemUi->Close();
  ktrfSaveLoadMetadataPage = -1;
  // Recreate MenuBar from the loaded feelings/state on the first Gameplay tick.
  ktrfMenuBar.reset();
  return true;
}

bool CloseGameplayConfigUi() {
  if (ktrfController == nullptr || ktrfSystemUi == nullptr ||
      ktrfController->FlowState() !=
          Kotonoha::SchoolDaysFlowController::State::GameplayConfig) {
    return false;
  }
  if (!CacheSchoolDaysRetailConfigState("Gameplay Config close")) return false;
  Kotonoha_KtrfError error{};
  if (!ktrfController->CloseGameplayModal(&error)) {
    SDL_LogError(0, "[SD-CONFIG] Gameplay Config close failed: %s",
                 error.message);
    return false;
  }
  if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
  ktrfSystemUi->Close();
  return true;
}

bool SubmitSaveFeelUiOutput(const Kotonoha::SchoolDaysSystemUi::Output &output) {
  if (!output.hasSaveFeelResult || ktrfController == nullptr) {
    return true;
  }

  const bool normalHandoffContinue =
      output.saveFeelResult == Kotonoha::SchoolDaysSaveFeelModel::Result::Skip &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::HandoffPrompt;
  Kotonoha::Gameplay *beforeGameplay = nullptr;
  std::string sourceSceneKey;
  bool preparedForUiContinue = false;
  if (normalHandoffContinue) {
    beforeGameplay = ktrfController->CurrentGameplay();
    sourceSceneKey = ktrfController->CurrentScene().sceneKey;
    if (global.render == nullptr || beforeGameplay == nullptr ||
        ktrfSceneTransaction == nullptr || ktrfSystemUi == nullptr) {
      SDL_LogError(0,
                   "[SD-SYSTEM-UI] SaveFeel continue lacks visual transaction state");
      return false;
    }

    // SysMenu owns its own presentation. Narrative leases/prepared frames must
    // never bleed through the prompt. Recompose the exact current SaveFeel
    // surface on a deterministic cleared backbuffer, then capture that UI as
    // the outgoing composition before the Gameplay swap.
    ktrfSceneTransaction->Reset();
    if (!SDL_SetRenderTarget(global.render, nullptr)) {
      SDL_LogError(0, "[SD-SYSTEM-UI] backbuffer select failed: %s",
                   SDL_GetError());
      return false;
    }
    SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
    if (!SDL_RenderClear(global.render)) {
      SDL_LogError(0, "[SD-SYSTEM-UI] pre-swap clear failed: %s",
                   SDL_GetError());
      return false;
    }
    std::string uiError;
    if (!ktrfSystemUi->Render(global.render, &uiError)) {
      SDL_LogError(0, "[SD-SYSTEM-UI] pre-swap SaveFeel render failed: %s",
                   uiError.c_str());
      return false;
    }
    if (!ktrfSceneTransaction->PrepareExplicit(
            global.render, beforeGameplay, "savefeel-continue")) {
      SDL_LogError(0,
                   "[SD-SYSTEM-UI] failed to prepare SaveFeel continuation frame");
      return false;
    }
    preparedForUiContinue = true;
  }

  Kotonoha::SchoolDaysFlowController::SaveFeelResult result{};
  switch (output.saveFeelResult) {
  case Kotonoha::SchoolDaysSaveFeelModel::Result::Skip:
    result = Kotonoha::SchoolDaysFlowController::SaveFeelResult::Skip;
    break;
  case Kotonoha::SchoolDaysSaveFeelModel::Result::OpenSave:
    result = Kotonoha::SchoolDaysFlowController::SaveFeelResult::OpenSave;
    break;
  }

  Kotonoha_KtrfError error{};
  if (!ktrfController->SubmitSaveFeelResult(result, &error)) {
    if (preparedForUiContinue && ktrfSceneTransaction != nullptr) {
      ktrfSceneTransaction->CancelPreparedFor(beforeGameplay,
                                              "savefeel-continue-failed");
    }
    SDL_LogError(0, "[SD-SYSTEM-UI] SaveFeel result failed: %s", error.message);
    return false;
  }

  if (ktrfController->FlowState() ==
      Kotonoha::SchoolDaysFlowController::State::Title) {
    // Terminal Script.Free released the physical Gameplay. Clear every
    // presentation/input latch that still names the old narrative owner before
    // Title can allocate another Gameplay at the same address.
    ktrfMenuBackStartGameplay = nullptr;
    if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
    ResetSchoolDaysBackLog();
    SDL_SetAtomicInt(&global.textLogCaptureEnabled, 0);
    ktrfMenuBar.reset();
  }

  if (normalHandoffContinue && ktrfSceneTransaction != nullptr) {
    Kotonoha::Gameplay *afterGameplay = ktrfController->CurrentGameplay();
    if (afterGameplay == beforeGameplay || afterGameplay == nullptr) {
      if (preparedForUiContinue) {
        ktrfSceneTransaction->CancelPreparedFor(beforeGameplay,
                                                "savefeel-continue-no-swap");
      }
      SDL_LogError(0,
                   "[SD-SYSTEM-UI] successful handoff continuation did not swap Gameplay");
      return false;
    }
    if (!ktrfSceneTransaction->CommitIfSwapped(
            beforeGameplay, afterGameplay, "savefeel-continue")) {
      SDL_LogError(0,
                   "[SD-SYSTEM-UI] handoff swap lacks a retained presentation frame");
      return false;
    }
    // Episode-card presentation is deliberately best-effort until the original
    // EP2..EP6 trigger/timing contract is runtime-authenticated.  Never let it
    // block the already-committed RouteProc handoff.
    (void)ArmSchoolDaysEpisodeOverlay(
        sourceSceneKey, ktrfController->CurrentScene().sceneKey, afterGameplay);
  }
  if (ktrfSystemUi != nullptr) {
    ktrfSystemUi->Close();
  }
  return true;
}

KtrfLaunchOptions ExtractKtrfLaunchOptions(int argc, char *argv[]) {
  KtrfLaunchOptions options;

  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }

    if (std::strcmp(argv[i], "--sd-ui-variant") == 0) {
      if (options.uiVariantSpecified || i + 1 >= argc || argv[i + 1] == nullptr) {
        options.valid = false;
        return options;
      }
      const char *value = argv[i + 1];
      if (std::strcmp(value, "standard") == 0) {
        options.uiVariant = Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
      } else if (std::strcmp(value, "wide") == 0) {
        options.uiVariant = Kotonoha::SchoolDaysSaveFeelModel::Variant::Wide;
      } else if (std::strcmp(value, "wide-full") == 0) {
        options.uiVariant = Kotonoha::SchoolDaysSaveFeelModel::Variant::WideFull;
      } else if (std::strcmp(value, "wide-note") == 0) {
        options.uiVariant = Kotonoha::SchoolDaysSaveFeelModel::Variant::WideNote;
      } else {
        options.valid = false;
        return options;
      }
      options.uiVariantSpecified = true;
      // Consume the School Days-only option before the generic Kotonoha
      // argument parser sees it.  The value itself has no '-' prefix and is
      // therefore ignored by the generic parser.
      argv[i][0] = '!';
      ++i;
      continue;
    }

    if (std::strcmp(argv[i], "--sd-assets-root") == 0) {
      if (options.assetsRootSpecified || i + 1 >= argc ||
          argv[i + 1] == nullptr || *argv[i + 1] == '\0') {
        options.valid = false;
        return options;
      }
      options.assetsRootSpecified = true;
      options.assetsRoot = argv[i + 1];
      // This path is deliberately distinct from the ORS root consumed by the
      // KTRF session.  SysMenu/Title/Save configuration and extracted System
      // resources live under the original game-data root, not the ORS tree.
      argv[i][0] = '!';
      ++i;
      continue;
    }

    if (std::strcmp(argv[i], "--sd-script-dir") == 0) {
      if (options.scriptDirSpecified || i + 1 >= argc ||
          argv[i + 1] == nullptr || *argv[i + 1] == '\0') {
        options.valid = false;
        return options;
      }
      options.scriptDirSpecified = true;
      options.scriptDir = argv[i + 1];
      argv[i][0] = '!';
      ++i;
      continue;
    }

    if (std::strcmp(argv[i], "--sd-gpk-root") == 0) {
      if (options.gpkRootSpecified || i + 1 >= argc || argv[i + 1] == nullptr ||
          *argv[i + 1] == '\0') {
        options.valid = false;
        return options;
      }
      options.gpkRootSpecified = true;
      options.gpkRoot = argv[i + 1];
      argv[i][0] = '!';
      ++i;
      continue;
    }

    if (std::strcmp(argv[i], "-K") != 0) continue;

    if (options.requested || i + 1 >= argc || argv[i + 1] == nullptr ||
        *argv[i + 1] == '\0') {
      options.valid = false;
      return options;
    }

    options.requested = true;
    options.routePath = argv[i + 1];
    // Legacy/extracted launches supply an ORS root as the second -K value.
    // A retail-GPK launch may omit it entirely; after the full argv sweep the
    // presence of --sd-gpk-root authenticates that mode.  Options begin with
    // '-' so they are never mistaken for a filesystem root here.
    const bool hasLegacyOrsRoot =
        i + 2 < argc && argv[i + 2] != nullptr && *argv[i + 2] != '\0' &&
        argv[i + 2][0] != '-';
    if (hasLegacyOrsRoot) options.orsRoot = argv[i + 2];

    // Keep KotonohaConfigLoad from treating the KTRF switch as a generic
    // option while leaving argv storage/ordering intact for SDL.
    argv[i][0] = '!';
    i += hasLegacyOrsRoot ? 2 : 1;
  }

  if (options.requested) {
    if (!options.assetsRootSpecified && !options.gpkRootSpecified) {
      options.valid = false;
      return options;
    }
    if (options.orsRoot.empty() && !options.gpkRootSpecified) {
      options.valid = false;
      return options;
    }
    for (int i = 1; i < argc; ++i) {
      if (argv[i] != nullptr && std::strcmp(argv[i], "-l") == 0) {
        options.valid = false;
        return options;
      }
    }
  }
  else if (options.uiVariantSpecified || options.assetsRootSpecified ||
           options.gpkRootSpecified || options.scriptDirSpecified) {
    // These School Days profiles are meaningful only for KTRF mode.
    options.valid = false;
  }

  return options;
}

KtrfLaunchOptions ktrfLaunchOptions;

// Which debug GUI and Gameplay the shared frame steps use after
// SchoolDaysRenderFrame (title frontends hide the bridge Gameplay from the
// debug GUI; the BackLog shows the frozen scene).
struct SchoolDaysFrameDebug {
  enum class Mode { Engine, EngineIfEnabled, Ktrf };
  Mode mode = Mode::EngineIfEnabled;
  Kotonoha::Gameplay *debugGameplay = nullptr;
  Kotonoha::Gameplay *mcpGameplay = nullptr;
};
SchoolDaysFrameDebug ktrfFrameDebug;

// School Days as a GameSession: it keeps its complete retail front end (title,
// SaveLoad, route map, config, menu bar, BackLog), so GameplayFlow forwards
// events and frames to it and only adds the shared splash / debug UI /
// present steps, like every other port.
class SchoolDaysSession final : public Kotonoha::GameSession {
public:
  explicit SchoolDaysSession(void *appstate) : appstate_(appstate) {}

  bool OwnsFrontend() const override { return true; }
  SDL_AppResult HandleEvent(SDL_Event *event, Kotonoha_Game *game) override;
  SDL_AppResult RenderFrame(Kotonoha::Kotonoha *app,
                            Kotonoha_Game *game) override;
  Kotonoha::Gameplay *RunningGameplay() override;
  bool RunDebugUi(Kotonoha::Kotonoha *app, Kotonoha_Game &game) override;

  // The retail story runs inside the KTRF controller / Gameplay instead.
  bool Start() override { return true; }
  void Step(int) override {}
  bool Choosing() const override { return false; }
  std::vector<std::string> Choices() const override { return {}; }
  bool HasMessage() const override { return false; }
  std::string Speaker() const override { return {}; }
  std::string Text() const override { return {}; }
  void Draw(SDL_Renderer *) override {}

private:
  void *appstate_;
};

std::unique_ptr<Kotonoha::GameplayFlow> ktrfFlow;

} // namespace

SDL_AppResult LocalArgs(int argc, char *argv[]) {
  ktrfLaunchOptions = ExtractKtrfLaunchOptions(argc, argv);
#if defined(KOTONOHA_MOBILE)
  // Mobile launches have no command line: use the route and GPKs the user
  // placed in the platform game-data folder (iOS Documents).
  if (!ktrfLaunchOptions.requested && ktrfLaunchOptions.valid) {
    const char *dataDir = KotonohaMobileGameDataDir();
    if (dataDir != nullptr) {
      const std::string root = dataDir;
      std::string route = root + "/school-days-hq.ktnroute";
      if (!std::filesystem::exists(route)) {
        route = "assets/school-days-hq.ktnroute";
      }
      std::string program = argc > 0 && argv[0] != nullptr ? argv[0] : "app";
      std::string rootArg = root;
      std::string gpkFlag = "--sd-gpk-root";
      std::string routeFlag = "-K";
      char *synthetic[] = {program.data(), routeFlag.data(), route.data(),
                           gpkFlag.data(), rootArg.data()};
      ktrfLaunchOptions = ExtractKtrfLaunchOptions(5, synthetic);
    }
  }
#else
  // Commercial desktop layout: when no development KTRF command line was
  // supplied, discover the route/Packs beside the executable. This keeps the
  // debug -K contract intact while allowing a shipped SchoolDays.exe to launch
  // exactly like the retail executable.
  if (!ktrfLaunchOptions.requested && ktrfLaunchOptions.valid && argc > 0 &&
      argv[0] != nullptr && *argv[0] != '\0') {
    std::error_code ec;
    std::filesystem::path executable = std::filesystem::absolute(argv[0], ec);
    if (!ec) {
      const std::filesystem::path root = executable.parent_path();
      std::filesystem::path route = root / "route" / "school-days-hq.ktnroute";
      if (!std::filesystem::exists(route, ec)) {
        ec.clear();
        route = root / "school-days-hq.ktnroute";
      }
      const std::filesystem::path packs = root / "Packs";
      if (std::filesystem::exists(route, ec) && !ec &&
          std::filesystem::is_directory(packs, ec) && !ec) {
        std::string program = executable.string();
        std::string routeFlag = "-K";
        std::string routeArg = route.string();
        std::string gpkFlag = "--sd-gpk-root";
        std::string packsArg = packs.string();
        char *synthetic[] = {program.data(), routeFlag.data(), routeArg.data(),
                             gpkFlag.data(), packsArg.data()};
        ktrfLaunchOptions = ExtractKtrfLaunchOptions(5, synthetic);
        if (ktrfLaunchOptions.requested && ktrfLaunchOptions.valid) {
          SDL_Log("[SD-LAUNCH] discovered packaged route/Packs beside executable");
        }
      }
    }
  }
#endif
  if (!ktrfLaunchOptions.valid) {
    SDL_LogError(0,
                 "Invalid KTRF launch arguments. Use -K <route.ktnroute> "
                 "[<ors-root>] [--sd-assets-root <extracted-game-data-root>] "
                 "[--sd-gpk-root <retail-game-root-or-Packs>] "
                 "[--sd-script-dir <loose-script-directory>] "
                 "[--sd-ui-variant standard|wide|wide-full|wide-note] "
                 "and do not combine it with -l.");
    return SDL_APP_FAILURE;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult LocalInit(void **appstate, int, char *[]) {
  Kotonoha::Kotonoha *engine = Kotonoha::GetApp(*appstate);
  Kotonoha::InstallSchoolDaysImageAssetProvider();

  ktrfSceneTransaction.reset();
  ktrfReplaySceneTransaction.reset();
  ktrfSystemUi.reset();
  ktrfSaveStorage.reset();
  ktrfSaveProfile = {};
  ktrfController.reset();
  ktrfUiVariantSpecified = false;
  ktrfUiVariant = Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard;
  ktrfRetailDisplayState = {};
  ktrfRetailDisplayBackendState = {};
  ktrfModernDisplayPanel = {};
  ktrfFrontendTransition.Reset();
  ktrfFrontendHandoff = KtrfFrontendHandoff::None;
  ktrfTitleHasPresented = false;
  ResetSchoolDaysEpisodeOverlay();
  ktrfReplayHostModal = KtrfReplayHostModal::None;
  ktrfAssetsRoot.clear();

  const KtrfLaunchOptions &ktrfOptions = ktrfLaunchOptions;

  if (ktrfOptions.requested) {
    const char *basePath = SDL_GetBasePath();
    if (basePath == nullptr || *basePath == '\0') {
      SDL_LogError(0, "[SD-CONFIG] SDL_GetBasePath failed: %s", SDL_GetError());
      return SDL_APP_FAILURE;
    }

    std::string configError;
    ktrfConfigStorage = std::make_unique<Kotonoha::SchoolDaysConfigStorage>();
    Kotonoha::SchoolDaysConfigStorage::LoadResult configLoad;
    if (!ktrfConfigStorage->Initialize(basePath, &configError) ||
        !ktrfConfigStorage->Load(&configLoad, &configError)) {
      SDL_LogError(0, "[SD-CONFIG] Config.DAT startup failed: %s",
                   configError.c_str());
      ktrfConfigStorage.reset();
      return SDL_APP_FAILURE;
    }
    ktrfRetailConfigState = configLoad.state;
    if (!EnsureSchoolDaysSubtitleStyles()) {
      ktrfConfigStorage.reset();
      return SDL_APP_FAILURE;
    }
    if (!ktrfOptions.scriptDirSpecified) {
      // Default: Script/<rest> is served from ./assets/<rest> when present.
      std::string ignored;
      Kotonoha::SetSchoolDaysScriptDirectory("assets", &ignored);
    }
    if (ktrfOptions.scriptDirSpecified) {
      std::string dirError;
      if (!Kotonoha::SetSchoolDaysScriptDirectory(ktrfOptions.scriptDir,
                                                 &dirError)) {
        SDL_LogError(0, "[SD-SCRIPTS] script directory failed: %s",
                     dirError.c_str());
        ktrfConfigStorage.reset();
        return SDL_APP_FAILURE;
      }
      SDL_Log("[SD-SCRIPTS] using script directory %s",
              ktrfOptions.scriptDir.c_str());
    }
    if (ktrfOptions.gpkRootSpecified) {
      std::string gpkError;
      if (!Kotonoha::MountSchoolDaysRetailGpks(ktrfOptions.gpkRoot, &gpkError)) {
        SDL_LogError(0, "[SD-GPK] retail mount failed: %s", gpkError.c_str());
        ktrfConfigStorage.reset();
        return SDL_APP_FAILURE;
      }
      SDL_Log("[SD-GPK] mounted retail packs entries=%llu",
              static_cast<unsigned long long>(
                  Kotonoha::MountedSchoolDaysGpkEntryCount()));
    }
    if (ktrfOptions.gpkRootSpecified) {
      Kotonoha::InstallMountedSchoolDaysAssetResolver();

      // The retail logo lives in System.gpk; show it after the engine's
      // default assets/SPLASH.PNG.
      for (const char *logoPath :
           {"System/SYSTEM/LOGO.png", "System/SYSTEM/LOGO.PNG"}) {
        if (Kotonoha::LookupMountedSchoolDaysAsset(logoPath) !=
            Kotonoha::SchoolDaysGpkLookupStatus::Present) {
          continue;
        }
        if (engine->LoadSplashImages(
                {Kotonoha::MakeSchoolDaysGpkUri(logoPath)}, true)) {
          break;
        }
      }
    }

    const bool explicitUiVariant = ktrfOptions.uiVariantSpecified;
    ktrfUiVariantSpecified = ktrfOptions.uiVariantSpecified;
    ktrfUiVariant = ktrfOptions.uiVariant;
    if (!ktrfUiVariantSpecified) {
      int outputWidth = 0;
      int outputHeight = 0;
      Kotonoha::SchoolDaysDisplayPolicy::Decision decision;
      if (!SDL_GetCurrentRenderOutputSize(global.render, &outputWidth,
                                          &outputHeight) ||
          !Kotonoha::SchoolDaysDisplayPolicy::SelectAuto(
              outputWidth, outputHeight, &decision)) {
        SDL_LogError(0, "[SD-DISPLAY] automatic logical profile selection failed");
        return SDL_APP_FAILURE;
      }
      ktrfUiVariant = decision.variant;
      ktrfUiVariantSpecified = true;
      SDL_Log("[SD-DISPLAY] auto logical profile=%d output=%dx%d viewport=%dx%d coverage=%.3f",
              static_cast<int>(decision.variant), outputWidth, outputHeight,
              decision.viewportWidth, decision.viewportHeight,
              static_cast<double>(decision.coverage));
    }
    // Physical-output auto selection is a startup-only remaster seed.  From
    // here on the authenticated School Days logical/backend state owns the
    // resource family; resize/HiDPI changes only the presenter viewport.
    ktrfRetailDisplayBackendState = SchoolDaysRetailBackendSeed(ktrfUiVariant);
    if (!explicitUiVariant && configLoad.present) {
      // Config.DAT persists B/M/W but not the selected-device D table.  The
      // modern backend has no legacy device table, so seed D from persisted B
      // before applying W.  An explicit --sd-ui-variant remains a deliberate
      // modern launch override and therefore bypasses these persisted fields.
      if (configLoad.display.hasDisplayType) {
        ktrfRetailDisplayBackendState.baseDisplayType =
            configLoad.display.displayType;
        ktrfRetailDisplayBackendState.deviceDisplayType =
            configLoad.display.displayType;
      }
      if (configLoad.display.hasWindowMode)
        ktrfRetailDisplayBackendState.windowMode = configLoad.display.windowMode;
      if (configLoad.display.hasTypeMiniNote)
        ktrfRetailDisplayBackendState.typeMiniNote =
            configLoad.display.typeMiniNote;
    }
    if (!explicitUiVariant) {
      // In the remaster, physical display mode is persisted generically in
      // Kotonoha.ini.  Project that authoritative modern state back into the
      // original two-state WINDOW/FULL control on startup instead of inventing
      // a WindowMode writer for Config.DAT that has not been authenticated.
      Kotonoha::DisplayMode physicalMode = Kotonoha::DisplayMode::Windowed;
      if (Kotonoha::GetDisplayMode(global, &physicalMode)) {
        ktrfRetailDisplayBackendState.windowMode =
            physicalMode == Kotonoha::DisplayMode::Windowed ? 0 : 1;
      }
    }
    RefreshSchoolDaysRetailDisplayProjection();
    if (!explicitUiVariant && global.config.contentAspectPreference.load() != 0) {
      if (!ApplySchoolDaysModernAspectPreference()) {
        SDL_LogError(0, "[SD-DISPLAY] persisted aspect preference failed");
        return SDL_APP_FAILURE;
      }
    }
    // Keep an extracted root only as fallback.  GPK-only launches use the
    // supplied retail root as a non-empty School Days namespace marker; the
    // resolver consults the mounted archive first and never treats the retail
    // root itself as an extracted resource when an entry is present.
    ktrfAssetsRoot = ktrfOptions.assetsRootSpecified
                         ? ktrfOptions.assetsRoot
                         : ktrfOptions.gpkRoot;
    global.profileAssetsRoot = ktrfAssetsRoot.c_str();
    if (!ApplySchoolDaysRetailAudioAndUiState()) {
      SDL_LogError(0, "[SD-CONFIG] failed to project retail audio channels");
      ktrfConfigStorage.reset();
      return SDL_APP_FAILURE;
    }
    ktrfController =
        std::make_unique<Kotonoha::SchoolDaysKtrfAppController>();
    Kotonoha_KtrfError error{};
    // The C KTRF asset view still requires a non-empty root while constructing
    // its intermediate scene asset record.  In direct-GPK mode the value is
    // only a namespace placeholder: ResolveOrsPhysicalPath immediately replaces
    // it with Script.GPK/English/<SceneKey>.ENG.ORS before any I/O occurs.
    const std::string effectiveOrsRoot = !ktrfOptions.orsRoot.empty()
                                             ? ktrfOptions.orsRoot
                                             : ktrfOptions.gpkRoot;
    if (!ktrfController->Open(ktrfOptions.routePath.c_str(),
                              effectiveOrsRoot.c_str(), engine, &global,
                              &error)) {
      SDL_LogError(0, "Couldn't start School Days KTRF mode: %s",
                   error.message);
      ktrfController.reset();
      return SDL_APP_FAILURE;
    }
    global.textLogUserdata = nullptr;
    global.textLogAppend = &AppendSchoolDaysBackLogText;
    ResetSchoolDaysBackLog();
    SDL_SetAtomicInt(&global.textLogCaptureEnabled, 1);

    std::string saveError;
    if (!Kotonoha::SchoolDaysSaveProfile::Load(
            ktrfAssetsRoot, &ktrfSaveProfile, &saveError)) {
      SDL_LogError(0, "[SD-SAVE] FILMENGINE profile load failed: %s",
                   saveError.c_str());
      ktrfController.reset();
      return SDL_APP_FAILURE;
    }
    ktrfSaveStorage = std::make_unique<Kotonoha::SchoolDaysSaveStorage>();
    if (!ktrfSaveStorage->Initialize(basePath, ktrfSaveProfile, &saveError)) {
      SDL_LogError(0, "[SD-SAVE] storage init failed: %s", saveError.c_str());
      ktrfSaveStorage.reset();
      ktrfController.reset();
      return SDL_APP_FAILURE;
    }
    std::vector<uint8_t> globalFlagBytes;
    bool globalFlagPresent = false;
    if (!ktrfSaveStorage->LoadGlobalFlag(&globalFlagBytes, &globalFlagPresent,
                                         &saveError)) {
      SDL_LogError(0, "[SD-SAVE] GlobalFlag load failed: %s",
                   saveError.c_str());
      ktrfSaveStorage.reset();
      ktrfController.reset();
      return SDL_APP_FAILURE;
    }
    if (globalFlagPresent &&
        !ktrfController->LoadGlobalFlag(globalFlagBytes, &error)) {
      SDL_LogError(0, "[SD-SAVE] GlobalFlag parse/apply failed: %s",
                   error.message);
      ktrfSaveStorage.reset();
      ktrfController.reset();
      return SDL_APP_FAILURE;
    }
    // Retail frontend owns startup after GlobalFlag has been projected, so
    // Title_Clear/REVER/EndBG see persisted progress before any narrative frame
    // can run. The bridge's initial Gameplay remains hidden until INICIAR or
    // CARREGAR transfers outer ownership back to Gameplay.
    if (!ktrfController->EnterStartupTitle(&error)) {
      SDL_LogError(0, "[SD-TITLE] startup frontend transition failed: %s",
                   error.message);
      ktrfSaveStorage.reset();
      ktrfController.reset();
      return SDL_APP_FAILURE;
    }
    ktrfSceneTransaction =
        std::make_unique<Kotonoha::KtrfSceneTransaction>();
    ktrfReplaySceneTransaction =
        std::make_unique<Kotonoha::KtrfSceneTransaction>();
    ktrfSystemUi = std::make_unique<Kotonoha::SchoolDaysSystemUi>();
  }

  ktrfFlow = std::make_unique<Kotonoha::GameplayFlow>(
      Kotonoha::GameplayFlow::Config{"school-days", "School Days HQ"});
  if (!ktrfFlow->Attach(std::make_unique<SchoolDaysSession>(*appstate))) {
    SDL_LogError(0, "[SD] gameplay flow attach failed");
    return SDL_APP_FAILURE;
  }
  return SDL_APP_CONTINUE;
}

static SDL_AppResult SchoolDaysHandleEvent(void *appstate, SDL_Event *event) {

#ifdef KOTONOHA_DEV_CHECKPOINTS
  if (ktrfController != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
      event->key.key == SDLK_F5 && ktrfController->HasPendingHandoff()) {
    Kotonoha::Gameplay *beforeGameplay = ktrfController->CurrentGameplay();
    const std::string sourceSceneKey = ktrfController->CurrentScene().sceneKey;
    if (ktrfSceneTransaction != nullptr && global.render != nullptr &&
        beforeGameplay != nullptr) {
      SDL_SetRenderTarget(global.render, nullptr);
      ktrfSceneTransaction->PrepareExplicit(global.render, beforeGameplay,
                                            "episode-handoff");
    }

    Kotonoha_KtrfError error{};
    if (!ktrfController->ContinueHandoff(&error)) {
      if (error.message[0] != '\0') {
        SDL_LogError(0, "[KTRF-HANDOFF] continue failed: %s", error.message);
      }
    } else if (ktrfSceneTransaction != nullptr) {
      Kotonoha::Gameplay *afterGameplay = ktrfController->CurrentGameplay();
      if (ktrfSceneTransaction->CommitIfSwapped(
              beforeGameplay, afterGameplay, "episode-handoff")) {
        (void)ArmSchoolDaysEpisodeOverlay(
            sourceSceneKey, ktrfController->CurrentScene().sceneKey,
            afterGameplay);
      }
    }
    return SDL_APP_CONTINUE;
  }
#endif

  // The engine already handled the event (and SDL_EVENT_QUIT) by now.
  Kotonoha::Kotonoha *app = Kotonoha::GetApp(appstate);
  if (app == nullptr || event->type == SDL_EVENT_QUIT) {
    return SDL_APP_CONTINUE;
  }

  if (ktrfBackLog != nullptr && ktrfBackLog->IsOpen()) {
    // BackLog is a true modal over a frozen Gameplay. It owns all input while
    // visible; only authenticated mouse motion/left-click affect its model.
    if (event->type == SDL_EVENT_MOUSE_MOTION ||
        event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
      int x = 0, y = 0;
      if (BackLogPointerToLogical(global.render, event, &x, &y)) {
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
          ktrfBackLog->SetSelectionFromPointer(x, y);
        } else if (event->button.button == SDL_BUTTON_LEFT) {
          Kotonoha::SchoolDaysBackLogModel::Action action;
          if (ktrfBackLog->ActivatePointer(x, y, &action) &&
              action.kind ==
                  Kotonoha::SchoolDaysBackLogModel::ActionKind::Close) {
            CloseSchoolDaysBackLog();
          }
        }
      }
    }
    return SDL_APP_CONTINUE;
  }

  if (ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()) {
    if (!EnsureSchoolDaysMenuBar()) return SDL_APP_FAILURE;
    if (Kotonoha::Gameplay *playDataGameplay =
            ktrfPlayDataPlayback->CurrentGameplay()) {
      ktrfMenuBar->SetRuntimeState(ktrfRetailConfigState.autoMode,
                                   playDataGameplay->UserPaused(&global),
                                   ktrfSchoolDaysSpeedIndex,
                                   ktrfRetailConfigState.superSkip,
                                   true, false, false, false);
      // Native SceneView sets FILMEngine secondary +0x1E0 through vtbl+0x94(1)
      // only for PlayData. MenuBar reads it through +0x98 and selects record70.
      ktrfMenuBar->SetReplayIndicatorState(true);
      Kotonoha::SchoolDaysMenuBarPresenter::Action playDataMenuAction;
      const bool playDataMenuConsumed =
          ktrfMenuBar->HandleEvent(global.render, event, &playDataMenuAction);
      using PlayDataMenuAction =
          Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
      if (playDataMenuAction.kind == PlayDataMenuAction::ToggleAutoMode) {
        ktrfRetailConfigState.autoMode = !ktrfRetailConfigState.autoMode;
        playDataGameplay->SetAutoChoiceMode(ktrfRetailConfigState.autoMode);
        if (!CacheSchoolDaysRetailConfigState("PlayData AutoMode"))
          return SDL_APP_FAILURE;
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::TogglePause) {
        playDataGameplay->ToggleUserPaused(&global);
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::BackOrRestartScene) {
        playDataGameplay->RestartCurrentScene();
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::SkipToEnd) {
        (void)playDataGameplay->SkipToEndBoundary();
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::SetPlaybackSpeed) {
        if (playDataMenuAction.value < 0 || playDataMenuAction.value > 4 ||
            !Kotonoha::SchoolDaysPlaybackPolicy::Apply(playDataGameplay,
                playDataMenuAction.value)) {
          SDL_LogError(0, "[SD-REPLAY] invalid PlayData speed index %d",
                       playDataMenuAction.value);
          return SDL_APP_FAILURE;
        }
        ktrfSchoolDaysSpeedIndex = playDataMenuAction.value;
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind ==
          PlayDataMenuAction::SetReplayIndicatorOpacity) {
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::OpenSave) {
        playDataGameplay->Pause();
        ktrfReplayHostModal = KtrfReplayHostModal::Save;
        if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
        ktrfSaveLoadMetadataPage = -1;
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::OpenLoad) {
        playDataGameplay->Pause();
        ktrfReplayHostModal = KtrfReplayHostModal::Load;
        if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
        ktrfSaveLoadMetadataPage = -1;
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::OpenBackLog) {
        if (!OpenSchoolDaysBackLog(app)) {
          SDL_LogError(0, "[SD-PLAYDATA] BackLog open failed");
          return SDL_APP_FAILURE;
        }
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::OpenConfig) {
        playDataGameplay->Pause();
        ktrfReplayHostModal = KtrfReplayHostModal::Config;
        ktrfConfigPage = Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
        if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuAction.kind == PlayDataMenuAction::OpenMainMenuConfirm) {
        playDataGameplay->Pause();
        ktrfReplayHostModal = KtrfReplayHostModal::MainMenuConfirm;
        if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
        return SDL_APP_CONTINUE;
      }
      if (playDataMenuConsumed) return SDL_APP_CONTINUE;
    }
    if (Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(event->type)) {
      if (Kotonoha::Gameplay *playDataGameplay =
              ktrfPlayDataPlayback->CurrentGameplay()) {
        playDataGameplay->Events(event, &global);
      }
    }
    return SDL_APP_CONTINUE;
  }

  if (ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) {
    if (!EnsureSchoolDaysMenuBar()) return SDL_APP_FAILURE;
    if (Kotonoha::Gameplay *replayGameplay =
            ktrfReplayPlayback->CurrentGameplay()) {
      ktrfMenuBar->SetRuntimeState(ktrfRetailConfigState.autoMode,
                                   replayGameplay->UserPaused(&global),
                                   ktrfSchoolDaysSpeedIndex,
                                   ktrfRetailConfigState.superSkip,
                                   true, false, true, false);
      // Ordinary HScene launch never calls host vtbl+0x94(1); +0x98 therefore
      // remains zero and the native MenuBar uses record69 (gray).
      ktrfMenuBar->SetReplayIndicatorState(false);

      Kotonoha::SchoolDaysMenuBarPresenter::Action replayMenuAction;
      const bool replayMenuConsumed =
          ktrfMenuBar->HandleEvent(global.render, event, &replayMenuAction);
      using ReplayMenuAction =
          Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
      if (replayMenuAction.kind == ReplayMenuAction::ToggleAutoMode) {
        ktrfRetailConfigState.autoMode = !ktrfRetailConfigState.autoMode;
        replayGameplay->SetAutoChoiceMode(ktrfRetailConfigState.autoMode);
        if (!CacheSchoolDaysRetailConfigState("Replay AutoMode"))
          return SDL_APP_FAILURE;
        return SDL_APP_CONTINUE;
      }
      if (replayMenuAction.kind == ReplayMenuAction::TogglePause) {
        replayGameplay->ToggleUserPaused(&global);
        return SDL_APP_CONTINUE;
      }
      if (replayMenuAction.kind == ReplayMenuAction::BackOrRestartScene) {
        // Ordinary HScene replay is a fixed SceneView-owned sequence.  The
        // authenticated control remains available, but there is no RouteProc
        // owner to step backwards into; rewind the current physical scene.
        replayGameplay->RestartCurrentScene();
        return SDL_APP_CONTINUE;
      }
      if (replayMenuAction.kind == ReplayMenuAction::SkipToEnd) {
        (void)replayGameplay->SkipToEndBoundary();
        return SDL_APP_CONTINUE;
      }
      if (replayMenuAction.kind == ReplayMenuAction::SetPlaybackSpeed) {
        if (replayMenuAction.value < 0 || replayMenuAction.value > 4 ||
            !Kotonoha::SchoolDaysPlaybackPolicy::Apply(replayGameplay,
                replayMenuAction.value)) {
          SDL_LogError(0, "[SD-REPLAY] invalid replay speed index %d",
                       replayMenuAction.value);
          return SDL_APP_FAILURE;
        }
        ktrfSchoolDaysSpeedIndex = replayMenuAction.value;
        return SDL_APP_CONTINUE;
      }
      if (replayMenuAction.kind == ReplayMenuAction::OpenConfig) {
        replayGameplay->Pause();
        ktrfReplayHostModal = KtrfReplayHostModal::Config;
        ktrfConfigPage = Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
        if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
        return SDL_APP_CONTINUE;
      }
      if (replayMenuAction.kind == ReplayMenuAction::OpenMainMenuConfirm) {
        replayGameplay->Pause();
        ktrfReplayHostModal = KtrfReplayHostModal::MainMenuConfirm;
        if (ktrfSystemUi != nullptr) ktrfSystemUi->Close();
        return SDL_APP_CONTINUE;
      }
      // +104=1 during ordinary HScene playback makes Save/Load/BackLog and
      // NextChoice unavailable; +98=0 likewise disables replay-opacity
      // controls. Config/MainMenu remain host-level controls in retail, but
      // their modal ownership is not part of the temporary Gameplay session.
      // Consume them here rather than leaking a top-bar click into narrative.
      if (replayMenuConsumed) return SDL_APP_CONTINUE;
    }
    if (Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(event->type)) {
      if (Kotonoha::Gameplay *replayGameplay =
              ktrfReplayPlayback->CurrentGameplay()) {
        replayGameplay->Events(event, &global);
      }
    }
    // Temporary replay playback owns frontend input until its ORS sequence
    // finishes. Do not leak this event into the retained Title/SceneView UI.
    return SDL_APP_CONTINUE;
  }

  // Retail fades are modal presentation states.  Keep the outgoing/incoming
  // view stable until the opaque handoff or fade-in completes instead of
  // accepting another SysMenu pointer action mid-transition.
  if (ktrfFrontendTransition.Active(SDL_GetTicks()) &&
      (event->type == SDL_EVENT_MOUSE_MOTION ||
       event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
       event->type == SDL_EVENT_MOUSE_BUTTON_UP)) {
    return SDL_APP_CONTINUE;
  }

  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      ktrfController->FlowPromptVisible() && ktrfSystemUi->IsSaveFeelOpen()) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output) &&
        output.hasSaveFeelResult) {
      if (!SubmitSaveFeelUiOutput(output)) {
        return SDL_APP_FAILURE;
      }
      // The result may synchronously continue the handoff and swap Gameplay.
      // The mouse-down that dismissed SaveFeel belongs to SysMenu and must not
      // be delivered again to that newly activated narrative scene.
      return SDL_APP_CONTINUE;
    }
  }
  const bool playDataSaveLoad =
      ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen() &&
      (ktrfReplayHostModal == KtrfReplayHostModal::Save ||
       ktrfReplayHostModal == KtrfReplayHostModal::Load);
  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      (playDataSaveLoad || ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::SaveLoad ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleLoad ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::GameplaySave ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::GameplayLoad) &&
      ktrfSystemUi->IsSaveLoadOpen()) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output)) {
      (void)PlaySchoolDaysUiSfx(output.uiSfx);
      if (output.hasSaveLoadOutput) {
        using Action = Kotonoha::SchoolDaysSaveLoadModel::Action;
        if (output.saveLoadOutput.action == Action::Close) {
          if (playDataSaveLoad) {
            if (!ClosePlayDataSaveLoadUi()) return SDL_APP_FAILURE;
          } else if (ktrfController->FlowState() ==
              Kotonoha::SchoolDaysFlowController::State::TitleLoad) {
            (void)BeginSchoolDaysFrontendHandoff(
                KtrfFrontendHandoff::LoadToTitle,
                Kotonoha::SchoolDaysFrontendTransition::Profile::ChildToTitle);
          } else if (ktrfController->FlowState() ==
                         Kotonoha::SchoolDaysFlowController::State::GameplaySave ||
                     ktrfController->FlowState() ==
                         Kotonoha::SchoolDaysFlowController::State::GameplayLoad) {
            if (!CloseGameplaySaveLoadUi()) return SDL_APP_FAILURE;
          } else if (!CloseSaveLoadUi()) {
            return SDL_APP_FAILURE;
          }
        } else if (output.saveLoadOutput.action ==
                   Action::LoadSlotRequested) {
          const bool loaded = playDataSaveLoad
                                  ? ExecutePlayDataLoadSlot(output.saveLoadOutput.slot)
                                  : ExecuteSaveLoadLoadSlot(output.saveLoadOutput.slot);
          if (!loaded) {
            // Corrupt/missing user save data is recoverable UI input, not an
            // engine invariant failure. Keep LOAD open so another slot can be
            // selected or the modal can be closed normally.
            return SDL_APP_CONTINUE;
          }
        } else if (output.saveLoadOutput.action == Action::RouteMap) {
          if (playDataSaveLoad) {
            // The native PlayData LOAD reader contract is authenticated, but a
            // RouteMap handoff from that nested SaveLoad view is not. Consume
            // the request without leaking it into the retained narrative host.
            return SDL_APP_CONTINUE;
          }
          Kotonoha_KtrfError routeError{};
          const bool gameplayLoad =
              ktrfController->FlowState() ==
              Kotonoha::SchoolDaysFlowController::State::GameplayLoad;
          const bool entered = gameplayLoad
                                   ? ktrfController->EnterGameplayRouteMap(&routeError)
                                   : ktrfController->EnterTitleRouteMap(&routeError);
          if (!entered) {
            SDL_LogError(0, "[SD-ROUTEMAP] enter failed: %s", routeError.message);
            return SDL_APP_FAILURE;
          }
          ktrfSystemUi->Close();
          ktrfSaveLoadMetadataPage = -1;
          ktrfRouteMapProjectionMajor = -1;
          ktrfRouteMapProjectionSubpage = -1;
        } else if (output.saveLoadOutput.action ==
                   Action::CommentInputRequested) {
          Kotonoha::SchoolDaysSaveService::SlotMetadata metadata;
          if (!ReadSaveSlotMetadataForUi(output.saveLoadOutput.slot, &metadata)) {
            return SDL_APP_FAILURE;
          }
          std::u16string comment;
          std::string dialogError;
          Kotonoha::SchoolDaysFontData dialogFont;
          {
            std::filesystem::path fontLogical(ktrfSaveProfile.fontData);
            if (ktrfSaveProfile.useEnglish) {
              fontLogical.replace_filename(fontLogical.stem().string() + "_eng" +
                                           fontLogical.extension().string());
            }
            const auto fontAsset = Kotonoha::ResolveSchoolDaysAssetPath(
                ktrfAssetsRoot.c_str(), fontLogical.generic_string());
            std::string fontError;
            if (!fontAsset.present || fontAsset.collision ||
                !dialogFont.LoadFile(fontAsset.physical, &fontError)) {
              SDL_LogWarn(0, "[SD-SAVELOAD] comment dialog FONTDATA unavailable: %s",
                          fontError.c_str());
              dialogFont.Clear();
            }
          }
          const auto dialogResult = Kotonoha::SchoolDaysCommentDialog::Run(
              global.window, ktrfSaveProfile.useEnglish,
              metadata.occupied ? metadata.comment : std::u16string{},
              &comment, &dialogFont, &dialogError);
          if (dialogResult ==
              Kotonoha::SchoolDaysCommentDialog::Result::Confirmed) {
            if (!ktrfSystemUi->SubmitSaveLoadComment(comment)) {
              SDL_LogError(0, "[SD-SAVELOAD] comment callback was rejected");
              return SDL_APP_FAILURE;
            }
          } else if (dialogResult ==
                     Kotonoha::SchoolDaysCommentDialog::Result::Cancelled) {
            if (!ktrfSystemUi->CancelSaveLoadComment()) {
              SDL_LogError(0, "[SD-SAVELOAD] comment cancel was rejected");
              return SDL_APP_FAILURE;
            }
          } else {
            SDL_LogError(0, "[SD-SAVELOAD] comment dialog failed: %s",
                         dialogError.c_str());
            return SDL_APP_FAILURE;
          }
        }
      }
      // SaveLoad owns the pointer event even when it only changed page/hover.
      return SDL_APP_CONTINUE;
    }
  }
  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      (ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleRouteMap ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::GameplayRouteMap) &&
      ktrfSystemUi->IsRouteMapOpen()) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output)) {
      if (output.hasRouteMapAction) {
        using Kind = Kotonoha::SchoolDaysRouteMapModel::ActionKind;
        const auto& action = output.routeMapAction;
        if (action.kind == Kind::ReturnToLoad) {
          Kotonoha_KtrfError error{};
          const bool gameplayRoute =
              ktrfController->FlowState() ==
              Kotonoha::SchoolDaysFlowController::State::GameplayRouteMap;
          const bool returned =
              gameplayRoute
                  ? ktrfController->ReturnGameplayRouteMapToLoad(&error)
                  : ktrfController->ReturnTitleRouteMapToLoad(&error);
          if (!returned) {
            SDL_LogError(0, "[SD-ROUTEMAP] return to LOAD failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          ktrfSystemUi->Close();
          ktrfSaveLoadMetadataPage = -1;
          ktrfRouteMapProjectionMajor = -1;
          ktrfRouteMapProjectionSubpage = -1;
        } else if (action.kind == Kind::NodeRequested) {
          // Native result8 is an SP/checkpoint selector.  The controller resolves
          // SP%03d in live tag3, stages the checkpoint SceneKey Gameplay first,
          // then atomically restores/prunes persistence before publishing the swap.
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
          Kotonoha_KtrfError error{};
          if (!ktrfController->LoadRouteMapSelector(action.selector, &error)) {
            // Fail closed with RouteMap ownership intact.  A malformed/missing
            // checkpoint must not mutate live narrative state or force app exit.
            SDL_LogError(0, "[SD-ROUTEMAP] selector=%d restore failed: %s",
                         action.selector, error.message);
            return SDL_APP_CONTINUE;
          }
          ktrfSystemUi->Close();
          ktrfSaveLoadMetadataPage = -1;
          ktrfRouteMapProjectionMajor = -1;
          ktrfRouteMapProjectionSubpage = -1;
          // Recreate MenuBar from checkpoint-restored feelings/state.
          ktrfMenuBar.reset();
        }
      }
      return SDL_APP_CONTINUE;
    }
  }
  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      (ktrfReplayHostModal == KtrfReplayHostModal::MainMenuConfirm ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleExit ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::GameplayExit) &&
      ktrfSystemUi->IsExitOpen()) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output)) {
      if (output.hasExitActivation) {
        const bool confirm =
            output.exitActivation.outcome ==
            Kotonoha::SchoolDaysExitViewModel::Outcome::ConfirmExit;
        if (ktrfReplayHostModal == KtrfReplayHostModal::MainMenuConfirm) {
          if (confirm) {
            // Replay/PlayData are temporary Title-owned sessions. Top Menu
            // terminates only temporary playback and returns to Title.
            ClearTemporaryPlaybackPause();
            ktrfReplayPlayback.reset();
            ktrfPlayDataPlayback.reset();
            if (ktrfReplaySceneTransaction != nullptr)
              ktrfReplaySceneTransaction->Reset();
            ktrfReplayHostModal = KtrfReplayHostModal::None;
            ktrfTitleReplayActive = false;
            ktrfMenuBar.reset();
            global.config.next = false;
            global.config.back = false;
          } else {
            Kotonoha::Gameplay *replayGameplay =
                ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()
                    ? ktrfReplayPlayback->CurrentGameplay()
                    : (ktrfPlayDataPlayback != nullptr &&
                               ktrfPlayDataPlayback->IsOpen()
                           ? ktrfPlayDataPlayback->CurrentGameplay()
                           : nullptr);
            ktrfReplayHostModal = KtrfReplayHostModal::None;
            if (replayGameplay != nullptr && !replayGameplay->UserPaused(&global))
              replayGameplay->Resume();
          }
          ktrfSystemUi->Close();
          return SDL_APP_CONTINUE;
        }
        if (ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::GameplayExit) {
          bool returnToTitle = false;
          Kotonoha_KtrfError error{};
          if (!ktrfController->SubmitGameplayExit(confirm, &returnToTitle,
                                                   &error)) {
            SDL_LogError(0, "[SD-EXIT] Gameplay MainMenu result failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (returnToTitle) {
            // Native Script.Free flushes GlobalFlag after FLAG_LOGO=true and
            // gameplay teardown, before the frontend re-enters TitleView.
            if (!FlushSchoolDaysGlobalFlag("MainMenu confirm")) {
              return SDL_APP_FAILURE;
            }
            ResetSchoolDaysBackLog();
            SDL_SetAtomicInt(&global.textLogCaptureEnabled, 0);
            ktrfMenuBackStartGameplay = nullptr;
            ktrfMenuBar.reset();
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
          ktrfSystemUi->Close();
          return SDL_APP_CONTINUE;
        } else {
          bool exitApplication = false;
          Kotonoha_KtrfError error{};
          if (!ktrfController->SubmitTitleExit(confirm, &exitApplication, &error)) {
            SDL_LogError(0, "[SD-EXIT] result transition failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          ktrfSystemUi->Close();
          if (exitApplication) return SDL_APP_SUCCESS;
        }
      }
      return SDL_APP_CONTINUE;
    }
  }
  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Title &&
      ktrfSystemUi->IsTitleOpen()) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output)) {
      (void)PlaySchoolDaysUiSfx(output.uiSfx);
      if (output.hasTitleActivation) {
        using Result = Kotonoha::SchoolDaysTitleModel::Result;
        if (output.titleActivation.result == Result::Start &&
            output.titleActivation.startSelector == 0) {
          (void)BeginSchoolDaysFrontendHandoff(
              KtrfFrontendHandoff::TitleStart,
              Kotonoha::SchoolDaysFrontendTransition::Profile::TitleToStart);
        } else if (output.titleActivation.result == Result::Load) {
          (void)BeginSchoolDaysFrontendHandoff(
              KtrfFrontendHandoff::TitleLoad,
              Kotonoha::SchoolDaysFrontendTransition::Profile::TitleToChild);
        } else if (output.titleActivation.result == Result::Exit) {
          Kotonoha_KtrfError error{};
          if (!ktrfController->EnterTitleExit(&error)) {
            SDL_LogError(0, "[SD-TITLE] ExitView transition failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
          ktrfSystemUi->Close();
        } else if (output.titleActivation.result == Result::Config) {
          (void)BeginSchoolDaysFrontendHandoff(
              KtrfFrontendHandoff::TitleConfig,
              Kotonoha::SchoolDaysFrontendTransition::Profile::TitleToChild);
        } else if (output.titleActivation.result == Result::Replay) {
          (void)BeginSchoolDaysFrontendHandoff(
              KtrfFrontendHandoff::TitleReplay,
              Kotonoha::SchoolDaysFrontendTransition::Profile::TitleToChild);
        } else {
          // The remaining authenticated Title actions are typed by TitleModel
          // but their target presenters/semantics are integrated incrementally
          // in R4. Consume the pointer event without guessing a fallback.
          SDL_Log("[SD-TITLE] action result=%d selector=%d pending R4 target",
                  static_cast<int>(output.titleActivation.result),
                  output.titleActivation.startSelector);
        }
      }
      // TitleView owns this pointer event even when the target R4 subsystem is
      // intentionally not yet bound.
      return SDL_APP_CONTINUE;
    }
  }

  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Title &&
      ktrfTitleReplayActive && ktrfSystemUi->IsReplayOpen()) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output)) {
      (void)PlaySchoolDaysUiSfx(output.uiSfx);
      if (output.hasReplayAction) {
        using Kind = Kotonoha::SchoolDaysReplayModel::ActionKind;
        const auto &action = output.replayAction;
        if (action.kind == Kind::Close) {
          (void)BeginSchoolDaysFrontendHandoff(
              KtrfFrontendHandoff::ReplayToTitle,
              Kotonoha::SchoolDaysFrontendTransition::Profile::ChildToTitle);
        } else if (action.kind == Kind::SceneCheckRequested) {
          if (!RefreshSchoolDaysReplayAvailability()) return SDL_APP_FAILURE;
        } else if (action.kind == Kind::HSceneRequested) {
          if (ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) {
            SDL_LogError(0, "[SD-REPLAY] ordinary playback already active");
            return SDL_APP_FAILURE;
          }
          auto playback =
              std::make_unique<Kotonoha::SchoolDaysReplayPlaybackSession>();
          Kotonoha_KtrfError replayError{};
          if (!ktrfController->StartOrdinaryReplayPlayback(
                  action.index, playback.get(), &replayError)) {
            SDL_LogError(0, "[SD-REPLAY] ordinary playback start failed: %s",
                         replayError.message);
            return SDL_APP_FAILURE;
          }
          StopSchoolDaysTitleBgm();
          if (ktrfReplaySceneTransaction != nullptr)
            ktrfReplaySceneTransaction->Reset();
          ktrfSystemUi->Close();
          ktrfReplayPlayback = std::move(playback);
        } else if (action.kind == Kind::SceneCheckAccepted) {
          if (ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) {
            SDL_LogError(0, "[SD-REPLAY] special playback already active");
            return SDL_APP_FAILURE;
          }
          auto playback =
              std::make_unique<Kotonoha::SchoolDaysReplayPlaybackSession>();
          Kotonoha_KtrfError replayError{};
          if (!ktrfController->StartSpecialReplayPlayback(
                  action.index, action.choice, playback.get(), &replayError)) {
            SDL_LogError(0, "[SD-REPLAY] special playback start failed: %s",
                         replayError.message);
            return SDL_APP_FAILURE;
          }
          StopSchoolDaysTitleBgm();
          if (ktrfReplaySceneTransaction != nullptr)
            ktrfReplaySceneTransaction->Reset();
          ktrfSystemUi->Close();
          ktrfReplayPlayback = std::move(playback);
        } else if (action.kind == Kind::PlayDataRequested) {
          if ((ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) ||
              (ktrfPlayDataPlayback != nullptr &&
               ktrfPlayDataPlayback->IsOpen())) {
            SDL_LogError(0, "[SD-REPLAY] playback already active");
            return SDL_APP_FAILURE;
          }
          if (ktrfSaveStorage == nullptr) {
            SDL_LogError(0, "[SD-REPLAY] PlayData storage is unavailable");
            return SDL_APP_CONTINUE;
          }
          std::vector<uint8_t> saveBytes;
          std::string storageError;
          if (!ktrfSaveStorage->ReadSaveFile(action.index, &saveBytes,
                                             &storageError)) {
            SDL_LogError(0, "[SD-REPLAY] PlayData slot read failed: %s",
                         storageError.c_str());
            return SDL_APP_CONTINUE;
          }
          auto playback =
              std::make_unique<Kotonoha::SchoolDaysPlayDataSession>();
          Kotonoha_KtrfError replayError{};
          if (!ktrfController->StartPlayDataPlayback(
                  saveBytes, playback.get(), &replayError)) {
            SDL_LogError(0, "[SD-REPLAY] PlayData start failed: %s",
                         replayError.message);
            return SDL_APP_CONTINUE;
          }
          StopSchoolDaysTitleBgm();
          if (ktrfReplaySceneTransaction != nullptr)
            ktrfReplaySceneTransaction->Reset();
          ktrfSystemUi->Close();
          ktrfPlayDataPlayback = std::move(playback);
          // PlayData owns a fresh FILM gameplay/history beginning at A00.  Its
          // BackLog must not inherit messages from the retained narrative host.
          ResetSchoolDaysBackLog();
        }
      }
      return SDL_APP_CONTINUE;
    }
  }

  if (ktrfController != nullptr && ktrfSystemUi != nullptr &&
      (ktrfReplayHostModal == KtrfReplayHostModal::Config ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleConfig ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::GameplayConfig) &&
      ktrfSystemUi->IsConfigOpen()) {
    if (HandleSchoolDaysModernDisplayPanel(event)) return SDL_APP_CONTINUE;
    Kotonoha::SchoolDaysSystemUi::Output output;
    if (ktrfSystemUi->HandleEvent(global.render, event, &output)) {
      (void)PlaySchoolDaysUiSfx(output.uiSfx);
      if (output.hasConfigAction) {
        using Kind = Kotonoha::SchoolDaysConfigMenuModel::ActionKind;
        const auto &action = output.configAction;
        if (action.kind == Kind::Close) {
          if (ktrfReplayHostModal == KtrfReplayHostModal::Config) {
            if (!CacheSchoolDaysRetailConfigState("Replay Config close"))
              return SDL_APP_FAILURE;
            Kotonoha::Gameplay *replayGameplay =
                ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()
                    ? ktrfReplayPlayback->CurrentGameplay()
                    : (ktrfPlayDataPlayback != nullptr &&
                               ktrfPlayDataPlayback->IsOpen()
                           ? ktrfPlayDataPlayback->CurrentGameplay()
                           : nullptr);
            ktrfReplayHostModal = KtrfReplayHostModal::None;
            ktrfSystemUi->Close();
            if (replayGameplay != nullptr && !replayGameplay->UserPaused(&global))
              replayGameplay->Resume();
          } else if (ktrfController->FlowState() ==
              Kotonoha::SchoolDaysFlowController::State::GameplayConfig) {
            if (!CloseGameplayConfigUi()) return SDL_APP_FAILURE;
          } else {
            (void)BeginSchoolDaysFrontendHandoff(
                KtrfFrontendHandoff::ConfigToTitle,
                Kotonoha::SchoolDaysFrontendTransition::Profile::ChildToTitle);
          }
        } else if (action.kind == Kind::SelectPage) {
          // Page changes preserve outer Config ownership; the presenter is
          // reopened on the requested authenticated page with the same logical
          // display profile. State projection is refreshed in Iterate.
          if (action.page !=
              Kotonoha::SchoolDaysConfigMenuModel::Page::Display) {
            ktrfModernDisplayPanel.resolutionListOpen = false;
            ktrfModernDisplayPanel.hoveredResolution = -1;
            ktrfModernDisplayPanel.upscalerListOpen = false;
            ktrfModernDisplayPanel.hoveredUpscaler = -1;
          }
          ktrfConfigPage = action.page;
          ktrfSystemUi->Close();
        } else if (action.kind == Kind::SetMenVoice) {
          ktrfRetailConfigState.menVoice = action.value != 0;
          if (!ApplySchoolDaysRetailAudioAndUiState() ||
              !CacheSchoolDaysRetailConfigState("MenVoice"))
            return SDL_APP_FAILURE;
        } else if (action.kind == Kind::SetMute) {
          ktrfRetailConfigState.mute = action.value != 0;
          if (!ApplySchoolDaysRetailAudioAndUiState() ||
              !CacheSchoolDaysRetailConfigState("Mute"))
            return SDL_APP_FAILURE;
        } else if (action.kind == Kind::SetSkip) {
          const bool requestedSkip = action.value != 0;
          if (!requestedSkip) {
            Kotonoha::Gameplay *current =
                ktrfReplayHostModal == KtrfReplayHostModal::Config
                    ? (ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()
                           ? ktrfReplayPlayback->CurrentGameplay()
                           : (ktrfPlayDataPlayback != nullptr &&
                                      ktrfPlayDataPlayback->IsOpen()
                                  ? ktrfPlayDataPlayback->CurrentGameplay()
                                  : nullptr))
                    : ktrfController->CurrentGameplay();
            if (current != nullptr) {
              if (!Kotonoha::SchoolDaysPlaybackPolicy::Apply(current, 0)) {
                SDL_LogError(0, "[SD-CONFIG] Skip-off speed reset failed");
                return SDL_APP_FAILURE;
              }
            }
            ktrfSchoolDaysSpeedIndex = 0;
          }
          ktrfRetailConfigState.skip = requestedSkip;
          if (!CacheSchoolDaysRetailConfigState("Skip"))
            return SDL_APP_FAILURE;
          // The authenticated Skip-off side effect forces host speed index0.
          // Positive acceleration remains controlled only by MenuBar +0x8C;
          // never reuse generic config.skipFiles.
        } else if (action.kind == Kind::SetSuperSkip) {
          ktrfRetailConfigState.superSkip = action.value != 0;
          if (!CacheSchoolDaysRetailConfigState("SuperSkip"))
            return SDL_APP_FAILURE;
          // Native SuperSkip is an availability setting, not an autonomous
          // background accelerator. EXE message 0x800E gates the special
          // MenuBar-style skip command with this flag, then requests the same
          // authenticated mode-5 path implemented by SkipToNextChoice.
        } else if (action.kind == Kind::SetDrawMessage) {
          ktrfRetailConfigState.textView = action.value != 0;
          if (!ApplySchoolDaysRetailAudioAndUiState() ||
              !CacheSchoolDaysRetailConfigState("TextView"))
            return SDL_APP_FAILURE;
        } else if (action.kind == Kind::StepSoundBank ||
                   action.kind == Kind::SetSoundLevel) {
          if (action.soundBank < 0 || action.soundBank > 2) {
            SDL_LogError(0, "[SD-CONFIG] invalid visual sound bank %d",
                         action.soundBank);
            return SDL_APP_FAILURE;
          }
          using UiBank =
              Kotonoha::SchoolDaysRetailConfigPolicy::UiAudioBank;
          const auto bank = static_cast<UiBank>(action.soundBank);
          const bool changed =
              action.kind == Kind::StepSoundBank
                  ? Kotonoha::SchoolDaysRetailConfigPolicy::StepUiLevel(
                        &ktrfRetailConfigState, bank, action.choice)
                  : Kotonoha::SchoolDaysRetailConfigPolicy::SetUiLevel(
                        &ktrfRetailConfigState, bank, action.value);
          if (!changed || !ApplySchoolDaysRetailAudioAndUiState() ||
              !CacheSchoolDaysRetailConfigState(
                  action.kind == Kind::StepSoundBank ? "sound step"
                                                     : "sound level"))
            return SDL_APP_FAILURE;
        } else if (action.kind == Kind::RequestWide ||
                   action.kind == Kind::RequestFull) {
          const auto beforeVariant = ktrfUiVariant;
          const auto beforeBackend = ktrfRetailDisplayBackendState;
          const auto request =
              action.kind == Kind::RequestWide
                  ? Kotonoha::SchoolDaysDisplayPolicy::RetailConfigRequest::Wide
                  : Kotonoha::SchoolDaysDisplayPolicy::RetailConfigRequest::Full;
          if (Kotonoha::SchoolDaysDisplayPolicy::ApplyRetailConfigRequest(
                  request, action.value, SchoolDaysRetailConfigInputs(),
                  &ktrfRetailDisplayBackendState)) {
            RefreshSchoolDaysRetailDisplayProjection();
            if (action.kind == Kind::RequestFull &&
                !ApplySchoolDaysRetailWindowModeToPhysicalDisplay()) {
              // A rejected OS/window-manager transition must not leave the
              // original Config UI claiming a state that was not applied.
              ktrfRetailDisplayBackendState = beforeBackend;
              RefreshSchoolDaysRetailDisplayProjection();
              return SDL_APP_CONTINUE;
            }
            if (action.kind == Kind::RequestWide &&
                ktrfConfigStorage != nullptr) {
              std::string configError;
              if (!ktrfConfigStorage->CacheDisplayType(
                      ktrfRetailDisplayBackendState.baseDisplayType,
                      &configError)) {
                SDL_LogError(0, "[SD-CONFIG] DisplayType cache failed: %s",
                             configError.c_str());
                return SDL_APP_FAILURE;
              }
            }
            SDL_Log("[SD-CONFIG] logical display request kind=%d value=%d -> profile=%d e=%d w=%d note=%d",
                    static_cast<int>(action.kind), action.value,
                    static_cast<int>(ktrfUiVariant),
                    ktrfRetailDisplayState.effectiveDisplayType,
                    ktrfRetailDisplayState.windowMode,
                    ktrfRetailDisplayState.typeMiniNote);
            if (ktrfUiVariant != beforeVariant) {
              // Resource family changed. Reopen Config on the same page with
              // the new authored CMAP/base/CHIP set; TitleBGM ownership is the
              // enclosing Title/modal session and remains untouched.
              ktrfSystemUi->Close();
            }
          }
        } else {
          SDL_Log("[SD-CONFIG] typed action kind=%d value=%d bank=%d choice=%d deferred",
                  static_cast<int>(action.kind), action.value, action.soundBank,
                  action.choice);
        }
      }
      return SDL_APP_CONTINUE;
    }
  }

  // KTRF gameplays are owned by the routing bridge rather than app->gameplays.
  // Ruaneri's input refactor dispatches directly to the active Gameplay, so the
  // external KTRF scene must receive the event explicitly after global hotkeys.
  if (ktrfController != nullptr &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Gameplay &&
      !ktrfController->HasPendingHandoff()) {
    if (ktrfMenuBar != nullptr) {
      Kotonoha::SchoolDaysMenuBarPresenter::Action menuAction;
      const bool menuConsumed =
          ktrfMenuBar->HandleEvent(global.render, event, &menuAction);
      if (menuAction.kind !=
          Kotonoha::SchoolDaysMenuBarPresenter::ActionKind::None) {
        using MenuAction = Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
        Kotonoha_KtrfError error{};
        if (menuAction.kind == MenuAction::ToggleAutoMode) {
          ktrfRetailConfigState.autoMode = !ktrfRetailConfigState.autoMode;
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            current->SetAutoChoiceMode(ktrfRetailConfigState.autoMode);
          }
          if (!CacheSchoolDaysRetailConfigState("AutoMode"))
            return SDL_APP_FAILURE;
        } else if (menuAction.kind == MenuAction::TogglePause) {
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            current->ToggleUserPaused(&global);
          }
        } else if (menuAction.kind == MenuAction::BackOrRestartScene) {
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            // RestartCurrentScene() immediately restarts the Gameplay clock, so
            // testing for literal scene tick 0 gives the user only ~42 ms to
            // press Back again.  The retail navigation state is already tracked
            // explicitly by this Gameplay latch and is cleared by ordinary
            // gameplay input / transport actions below.  Use the latch itself:
            // first Back rewinds this physical scene, the next Back walks to the
            // authenticated predecessor without depending on frame timing.
            const bool atStart = ktrfMenuBackStartGameplay == current;
            if (atStart) {
              global.config.back = true;
            } else {
              ktrfMenuBackStartGameplay = nullptr;
              current->RestartCurrentScene();
              ktrfMenuBackStartGameplay = current;
              if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
            }
          }
        } else if (menuAction.kind == MenuAction::SetPlaybackSpeed) {
          if (menuAction.value < 0 || menuAction.value > 4) {
            SDL_LogError(0, "[SD-MENUBAR] invalid playback speed index %d",
                         menuAction.value);
            return SDL_APP_FAILURE;
          }
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            if (!Kotonoha::SchoolDaysPlaybackPolicy::Apply(current, menuAction.value)) {
              SDL_LogError(0, "[SD-MENUBAR] playback speed index %d failed",
                           menuAction.value);
              return SDL_APP_FAILURE;
            }
          }
          ktrfSchoolDaysSpeedIndex = menuAction.value;
        } else if (menuAction.kind == MenuAction::OpenBackLog) {
          if (!OpenSchoolDaysBackLog(app)) return SDL_APP_FAILURE;
        } else if (menuAction.kind == MenuAction::SkipToEnd) {
          ktrfMenuBackStartGameplay = nullptr;
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            if (current->SkipToEndBoundary() && ktrfSceneTransaction != nullptr)
              ktrfSceneTransaction->Reset();
          }
        } else if (menuAction.kind == MenuAction::SkipToNextChoice) {
          ktrfMenuBackStartGameplay = nullptr;
          if (!ktrfController->SkipToNextChoice(ktrfRetailConfigState.skip,
                                                &error)) {
            SDL_LogError(0, "[SD-MENUBAR] next-choice skip failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
        } else if (menuAction.kind == MenuAction::SetReplayIndicatorOpacity) {
          // The presenter already applies the authenticated local alpha/handle
          // state. Persistence for this SysMenu-only preference is not proven.
        } else if (menuAction.kind == MenuAction::OpenSave) {
          if (!ktrfController->EnterGameplaySave(&error)) {
            SDL_LogError(0, "[SD-MENUBAR] SAVE transition failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            current->Pause();
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
          ktrfSaveLoadMetadataPage = -1;
        } else if (menuAction.kind == MenuAction::OpenLoad) {
          if (!ktrfController->EnterGameplayLoad(&error)) {
            SDL_LogError(0, "[SD-MENUBAR] LOAD transition failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            current->Pause();
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
          ktrfSaveLoadMetadataPage = -1;
        } else if (menuAction.kind == MenuAction::OpenConfig) {
          if (!ktrfController->EnterGameplayConfig(&error)) {
            SDL_LogError(0, "[SD-MENUBAR] Config transition failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            current->Pause();
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
          ktrfConfigPage = Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
        } else if (menuAction.kind == MenuAction::OpenMainMenuConfirm) {
          Kotonoha_KtrfError error{};
          if (!ktrfController->EnterGameplayExit(&error)) {
            SDL_LogError(0, "[SD-MENUBAR] MainMenu confirmation enter failed: %s",
                         error.message);
            return SDL_APP_FAILURE;
          }
          if (Kotonoha::Gameplay* current = ktrfController->CurrentGameplay()) {
            current->Pause();
          }
          if (ktrfSceneTransaction != nullptr) ktrfSceneTransaction->Reset();
        }
        return SDL_APP_CONTINUE;
      }
      if (menuConsumed) return SDL_APP_CONTINUE;
    }
    if (Kotonoha::SchoolDaysInputPolicy::AcceptsGameplayEventType(event->type)) {
      if (Kotonoha::Gameplay *current = ktrfController->CurrentGameplay()) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          ktrfMenuBackStartGameplay = nullptr;
        }
        current->Events(event, &global);
        // School Days commits a chosen result immediately; only the visual
        // state2/state3 tail remains alive afterward.  Flush the semantic
        // result in the same input callback so a second queued MenuBar event
        // (Skip/Restart/SAVE/LOAD/Config) cannot observe pre-choice routing or
        // reset an accepted selection before the next frame.
        Kotonoha_KtrfError choiceError{};
        if (!ktrfController->SubmitCurrentChoiceIfReady(&choiceError)) {
          SDL_LogError(0, "[KTRF-APP] input-boundary choice commit failed: %s",
                       choiceError.message);
          return SDL_APP_FAILURE;
        }
      }
    }
  }

  return SDL_APP_CONTINUE;
}

// Renders the whole School Days frame (flow controller, frontends, BackLog,
// transitions) without presenting. The splash, debug UI,
// present are the shared GameplayFlow::RunFrame steps; this records which
// Gameplay and debug GUI they should use for the frame.
static SDL_AppResult SchoolDaysRenderFrame(Kotonoha::Kotonoha *app) {
  if (app == nullptr || global.render == nullptr) {
    return SDL_APP_FAILURE;
  }
  ktrfFrameDebug = {};

  Kotonoha::Gameplay *inRunning = nullptr;
  Kotonoha::Gameplay *beforeGameplay = nullptr;
  bool ktrfSceneAdvancePrepared = false;

  if (ktrfController == nullptr && app->IsSplashActive()) {
    SDL_SetRenderTarget(global.render, nullptr);
    SDL_SetRenderDrawColor(global.render, 0, 0, 0, 255);
    SDL_RenderClear(global.render);
    app->RenderSplash();
    if (global.config.enableDebugUi &&
        !Kotonoha::Debug::Run(app, nullptr, global)) {
      return SDL_APP_SUCCESS;
    }
    SDL_RenderPresent(global.render);
    return SDL_APP_CONTINUE;
  }

  if (!ApplySchoolDaysFrontendHandoff()) return SDL_APP_FAILURE;

  if (ktrfController != nullptr) {
    beforeGameplay = ktrfController->CurrentGameplay();
    if (beforeGameplay != nullptr &&
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::Gameplay) {
      // AutoMode is persisted independently from the physical Gameplay owner.
      // Re-project it every tick so a freshly swapped/loaded scene inherits the
      // current retail state before its Prompt can time out.
      beforeGameplay->SetAutoChoiceMode(ktrfRetailConfigState.autoMode);
      if (!Kotonoha::SchoolDaysPlaybackPolicy::Apply(beforeGameplay,
              ktrfSchoolDaysSpeedIndex)) {
        SDL_LogError(0, "[SD-MENUBAR] failed to project playback speed index %d",
                     ktrfSchoolDaysSpeedIndex);
        return SDL_APP_FAILURE;
      }
    }
  }

  // Observe the committed physical scene before its first Gameplay::Main tick,
  // so any PrintText emitted by the incoming script lands after native-style
  // ScriptChange prefix rewind rather than in the abandoned branch tail.
  ObserveSchoolDaysBackLogScriptChange();

  const bool ktrfBackLogOpen =
      ktrfBackLog != nullptr && ktrfBackLog->IsOpen();
  const bool narrativeBackLogCapture =
      ktrfController != nullptr && !ktrfBackLogOpen &&
      (ktrfReplayPlayback == nullptr || !ktrfReplayPlayback->IsOpen()) &&
      (ktrfPlayDataPlayback == nullptr || !ktrfPlayDataPlayback->IsOpen()) &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Gameplay;
  const bool playDataBackLogCapture =
      !ktrfBackLogOpen && ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen();
  SDL_SetAtomicInt(&global.textLogCaptureEnabled,
                   (narrativeBackLogCapture || playDataBackLogCapture) ? 1 : 0);

  if (ktrfBackLogOpen) {
    SDL_SetRenderTarget(global.render, nullptr);
    SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
    SDL_RenderClear(global.render);
    Kotonoha::Gameplay *backLogGameplay = nullptr;
    Kotonoha::KtrfSceneTransaction *backLogTransaction = nullptr;
    if (ktrfBackLogOwner == KtrfBackLogOwner::PlayData &&
        ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()) {
      backLogGameplay = ktrfPlayDataPlayback->CurrentGameplay();
      backLogTransaction = ktrfReplaySceneTransaction.get();
    } else if (ktrfBackLogOwner == KtrfBackLogOwner::Narrative) {
      backLogGameplay = beforeGameplay;
      backLogTransaction = ktrfSceneTransaction.get();
    }
    const bool frozenPresented =
        backLogTransaction != nullptr && backLogGameplay != nullptr &&
        (backLogTransaction->HasLease()
             ? (backLogTransaction->PresentLease(global.render), true)
             : backLogTransaction->PresentPrepared(global.render,
                                                    backLogGameplay));
    if (!frozenPresented) {
      SDL_LogError(0, "[SD-BACKLOG] frozen Gameplay frame is unavailable");
      return SDL_APP_FAILURE;
    }
    if (!RenderSchoolDaysBackLog(app)) {
      SDL_LogError(0, "[SD-BACKLOG] render failed");
      return SDL_APP_FAILURE;
    }
    ktrfFrameDebug.mode = SchoolDaysFrameDebug::Mode::EngineIfEnabled;
    ktrfFrameDebug.debugGameplay = beforeGameplay;
    ktrfFrameDebug.mcpGameplay = beforeGameplay;
    return SDL_APP_CONTINUE;
  }

  const bool ktrfGameplayFlow =
      ktrfController == nullptr ||
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Gameplay;
  if (ktrfController == nullptr &&
      (global.config.next || global.config.back) && app != nullptr) {
    app->CaptureLastPresentedFrameForTransition();
  }
  if (ktrfController != nullptr && ktrfSceneTransaction != nullptr &&
      !ktrfGameplayFlow) {
    // SysMenu/Title own the complete presentation while active. A narrative
    // lease is stale by definition once outer flow leaves Gameplay.
    ktrfSceneTransaction->Reset();
  }

  const bool ktrfBackRequested =
      ktrfController != nullptr && ktrfGameplayFlow &&
      !ktrfController->HasPendingHandoff() &&
      beforeGameplay != nullptr && global.config.back;
  if (ktrfBackRequested && ktrfSceneTransaction != nullptr &&
      !ktrfSceneTransaction->HasLease() &&
      !ktrfSceneTransaction->HasPreparedFrameFor(beforeGameplay)) {
    SDL_SetRenderTarget(global.render, nullptr);
    ktrfSceneTransaction->PrepareExplicit(global.render, beforeGameplay,
                                          "narrative-back");
  }

  if (ktrfController != nullptr && ktrfSceneTransaction != nullptr &&
      ktrfGameplayFlow &&
      !ktrfSceneTransaction->HasLease() &&
      !ktrfSceneTransaction->HasPreparedFrameFor(beforeGameplay) &&
      !ktrfController->HasPendingHandoff() && beforeGameplay != nullptr &&
      beforeGameplay->eventManager != nullptr &&
      (beforeGameplay->prompt == nullptr ||
       (!beforeGameplay->prompt->PresentationTailActive() &&
        !beforeGameplay->prompt->PresentationNeedsFinalFrame() &&
        !beforeGameplay->prompt->PresentationFinalFrameRendered())) &&
      beforeGameplay->eventManager->ReachedNext(beforeGameplay)) {
    SDL_SetRenderTarget(global.render, nullptr);
    ktrfSceneTransaction->PrepareAutomatic(global.render, beforeGameplay);
  }
  if (ktrfSceneTransaction != nullptr && beforeGameplay != nullptr &&
      ktrfGameplayFlow) {
    ktrfSceneAdvancePrepared =
        ktrfSceneTransaction->HasPreparedFrameFor(beforeGameplay) ||
        ktrfSceneTransaction->HasLease();
  }

  // Temporary SceneView playback needs the same outgoing-frame transaction as
  // narrative Gameplay.  In particular, a choice can publish its alpha-zero
  // terminal frame one render before CheckEnd becomes visible to the frontend.
  // Capture the already-presented composition before clearing the backbuffer so
  // a later Gameplay::Main early-return can never turn the carry frame black.
  Kotonoha::Gameplay *temporaryBeforeGameplay = nullptr;
  if (ktrfReplayHostModal == KtrfReplayHostModal::None) {
    if (ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) {
      temporaryBeforeGameplay = ktrfReplayPlayback->CurrentGameplay();
    } else if (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()) {
      temporaryBeforeGameplay = ktrfPlayDataPlayback->CurrentGameplay();
    }
  }
  if (ktrfReplaySceneTransaction != nullptr && temporaryBeforeGameplay != nullptr &&
      temporaryBeforeGameplay->eventManager != nullptr &&
      !ktrfReplaySceneTransaction->HasLease() &&
      !ktrfReplaySceneTransaction->HasPreparedFrameFor(temporaryBeforeGameplay) &&
      (temporaryBeforeGameplay->prompt == nullptr ||
       (!temporaryBeforeGameplay->prompt->PresentationTailActive() &&
        !temporaryBeforeGameplay->prompt->PresentationNeedsFinalFrame() &&
        !temporaryBeforeGameplay->prompt->PresentationFinalFrameRendered())) &&
      temporaryBeforeGameplay->eventManager->ReachedNext(temporaryBeforeGameplay)) {
    SDL_SetRenderTarget(global.render, nullptr);
    if (!ktrfReplaySceneTransaction->PrepareAutomatic(global.render,
                                                       temporaryBeforeGameplay)) {
      SDL_LogError(0, "[SD-REPLAY] failed to pre-retain outgoing temporary frame");
      return SDL_APP_FAILURE;
    }
  }

  SDL_SetRenderTarget(global.render, nullptr);
  SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
  SDL_RenderClear(global.render);

  if (ktrfSceneTransaction != nullptr && ktrfGameplayFlow) {
    if (ktrfSceneTransaction->HasLease()) {
      ktrfSceneTransaction->PresentLease(global.render);
    } else if (beforeGameplay != nullptr) {
      ktrfSceneTransaction->PresentPrepared(global.render, beforeGameplay);
    }
  }
  if (ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ktrfReplaySceneTransaction != nullptr &&
      ((ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) ||
       (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()))) {
    if (ktrfReplaySceneTransaction->HasLease()) {
      ktrfReplaySceneTransaction->PresentLease(global.render);
    } else if (temporaryBeforeGameplay != nullptr) {
      ktrfReplaySceneTransaction->PresentPrepared(global.render,
                                                  temporaryBeforeGameplay);
    }
  }

  const SDL_AppResult status = ktrfController != nullptr
                                   ? ktrfController->Main(
                                         &inRunning, ktrfSceneAdvancePrepared)
                                   : Kotonoha::EngineMasterMain(app, &inRunning);
  if (status != SDL_APP_CONTINUE) {
    if (ktrfBackRequested && ktrfSceneTransaction != nullptr &&
        beforeGameplay != nullptr) {
      ktrfSceneTransaction->CancelPreparedFor(beforeGameplay, "back-failed");
    }
    return status;
  }

  Kotonoha::Gameplay *afterControllerGameplay =
      ktrfController != nullptr ? ktrfController->CurrentGameplay() : nullptr;
  if (ktrfSceneTransaction != nullptr && beforeGameplay != nullptr &&
      afterControllerGameplay == beforeGameplay &&
      beforeGameplay->prompt != nullptr &&
      beforeGameplay->prompt->PresentationTailActive()) {
    // ReachedNext may become visible to the pre-frame transaction check one
    // iterate before Prompt::Update arms the native state2/state3 tail.  Do not
    // retain that stale pre-tail frame for the eventual scene swap; keep the
    // live Gameplay rendering until the 24/48-tick presentation completes.
    ktrfSceneTransaction->CancelPreparedFor(beforeGameplay,
                                            "school-days-choice-tail");
  }

  if (ktrfController != nullptr && ktrfSceneTransaction != nullptr &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Gameplay &&
      beforeGameplay != nullptr && afterControllerGameplay == beforeGameplay &&
      !ktrfSceneTransaction->HasLease() &&
      !ktrfSceneTransaction->HasPreparedFrameFor(beforeGameplay) &&
      beforeGameplay->eventManager != nullptr && beforeGameplay->prompt != nullptr &&
      beforeGameplay->prompt->PresentationFinalFrameRendered() &&
      beforeGameplay->eventManager->ReachedNext(beforeGameplay)) {
    // The alpha-zero 24/48-tick choice frame has just been composed into the
    // current backbuffer by Gameplay::Main. Capture it now, before
    // SDL_RenderPresent swaps buffers. Capturing at the start of the next
    // iterate can legally read an older backbuffer (and was retaining the
    // alpha-11 23/47-tick choice frame on double-buffered renderers).
    SDL_SetRenderTarget(global.render, nullptr);
    if (!ktrfSceneTransaction->PrepareAutomatic(global.render, beforeGameplay)) {
      SDL_LogError(
          0,
          "[SD-TRANSACTION] failed to capture final School Days choice frame");
      return SDL_APP_FAILURE;
    }
  }

  if (ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) {
    Kotonoha::Gameplay *replayGameplay = ktrfReplayPlayback->CurrentGameplay();
    if (replayGameplay == nullptr || replayGameplay->eventManager == nullptr) {
      SDL_LogError(0, "[SD-REPLAY] temporary playback has no active Gameplay");
      return SDL_APP_FAILURE;
    }
    if (!EnsureSchoolDaysMenuBar()) return SDL_APP_FAILURE;
    ktrfMenuBar->SetRuntimeState(ktrfRetailConfigState.autoMode,
                                 replayGameplay->UserPaused(&global),
                                 ktrfSchoolDaysSpeedIndex,
                                 ktrfRetailConfigState.superSkip,
                                 true, false, true, false);
    ktrfMenuBar->SetReplayIndicatorState(false);
    ktrfMenuBar->Update(static_cast<uint32_t>(SDL_GetTicks()), 0, 0);
    const SDL_AppResult replayStatus = replayGameplay->Main(&global);
    if (replayStatus == SDL_APP_FAILURE) return replayStatus;

    if (ktrfReplaySceneTransaction != nullptr && replayGameplay->prompt != nullptr &&
        replayGameplay->prompt->PresentationTailActive()) {
      ktrfReplaySceneTransaction->CancelPreparedFor(
          replayGameplay, "replay-choice-tail");
    }
    if (ktrfReplaySceneTransaction != nullptr && replayGameplay->prompt != nullptr &&
        !ktrfReplaySceneTransaction->HasLease() &&
        !ktrfReplaySceneTransaction->HasPreparedFrameFor(replayGameplay) &&
        replayGameplay->prompt->PresentationFinalFrameRendered() &&
        replayGameplay->eventManager->ReachedNext(replayGameplay)) {
      SDL_SetRenderTarget(global.render, nullptr);
      if (!ktrfReplaySceneTransaction->PrepareAutomatic(global.render,
                                                         replayGameplay)) {
        SDL_LogError(0,
                     "[SD-REPLAY] failed to retain final HScene choice frame");
        return SDL_APP_FAILURE;
      }
    }
    if (ktrfReplaySceneTransaction != nullptr)
      ktrfReplaySceneTransaction->ReleaseIfIncomingReady(replayGameplay);
    // SDL_APP_SUCCESS is the ordinary Gameplay end signal, not application
    // shutdown, while the frontend owns a temporary replay session.
    const int priorSelectResult = replayGameplay->promptId;
    if (replayGameplay->eventManager->CheckEnd(replayGameplay) &&
        (replayGameplay->prompt == nullptr ||
         !replayGameplay->prompt->PresentationTailActive())) {
      if (ktrfReplaySceneTransaction != nullptr &&
          !ktrfReplaySceneTransaction->HasLease() &&
          !ktrfReplaySceneTransaction->HasPreparedFrameFor(replayGameplay)) {
        SDL_LogError(0,
                     "[SD-REPLAY] HScene ended without a retained outgoing frame");
        return SDL_APP_FAILURE;
      }
      bool completed = false;
      Kotonoha_KtrfError replayError{};
      if (!ktrfReplayPlayback->AdvanceAfterSceneEnd(
              priorSelectResult, &completed, &replayError)) {
        SDL_LogError(0, "[SD-REPLAY] temporary playback advance failed: %s",
                     replayError.message);
        return SDL_APP_FAILURE;
      }
      global.config.next = false;
      if (completed) {
        ClearTemporaryPlaybackPause();
        if (ktrfReplaySceneTransaction != nullptr)
          ktrfReplaySceneTransaction->Reset();
        ktrfReplayPlayback.reset();
        inRunning = nullptr;
      } else {
        inRunning = ktrfReplayPlayback->CurrentGameplay();
        if (inRunning == nullptr) {
          SDL_LogError(0, "[SD-REPLAY] temporary playback swap lost Gameplay");
          return SDL_APP_FAILURE;
        }
        if (ktrfReplaySceneTransaction != nullptr)
          ktrfReplaySceneTransaction->CommitIfSwapped(
              replayGameplay, inRunning, "replay-current-scene");
      }
    } else {
      inRunning = replayGameplay;
      if (global.config.next) global.config.next = false;
    }
  }
  if (ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()) {
    Kotonoha::Gameplay *playDataGameplay =
        ktrfPlayDataPlayback->CurrentGameplay();
    if (playDataGameplay == nullptr || playDataGameplay->eventManager == nullptr) {
      SDL_LogError(0, "[SD-REPLAY] PlayData has no active Gameplay");
      return SDL_APP_FAILURE;
    }
    if (!EnsureSchoolDaysMenuBar()) return SDL_APP_FAILURE;
    ktrfMenuBar->SetRuntimeState(ktrfRetailConfigState.autoMode,
                                 playDataGameplay->UserPaused(&global),
                                 ktrfSchoolDaysSpeedIndex,
                                 ktrfRetailConfigState.superSkip,
                                 true, false, false, false);
    ktrfMenuBar->SetReplayIndicatorState(true);
    ktrfMenuBar->Update(static_cast<uint32_t>(SDL_GetTicks()), 0, 0);
    const SDL_AppResult playDataStatus = playDataGameplay->Main(&global);
    if (playDataStatus == SDL_APP_FAILURE) return playDataStatus;

    if (ktrfReplaySceneTransaction != nullptr && playDataGameplay->prompt != nullptr &&
        playDataGameplay->prompt->PresentationTailActive()) {
      ktrfReplaySceneTransaction->CancelPreparedFor(
          playDataGameplay, "playdata-choice-tail");
    }
    if (ktrfReplaySceneTransaction != nullptr && playDataGameplay->prompt != nullptr &&
        !ktrfReplaySceneTransaction->HasLease() &&
        !ktrfReplaySceneTransaction->HasPreparedFrameFor(playDataGameplay) &&
        playDataGameplay->prompt->PresentationFinalFrameRendered() &&
        playDataGameplay->eventManager->ReachedNext(playDataGameplay)) {
      SDL_SetRenderTarget(global.render, nullptr);
      if (!ktrfReplaySceneTransaction->PrepareAutomatic(global.render,
                                                         playDataGameplay)) {
        SDL_LogError(0,
                     "[SD-REPLAY] failed to retain final PlayData choice frame");
        return SDL_APP_FAILURE;
      }
    }
    if (ktrfReplaySceneTransaction != nullptr)
      ktrfReplaySceneTransaction->ReleaseIfIncomingReady(playDataGameplay);

    // Native PlayData resolves SetSELECT while the temporary FILM scene is
    // running. Preserve the same frame order here: first advance Gameplay time
    // (which materializes prompt timeout), then commit the private choice, and
    // only then inspect scene-end/routing state.
    bool choiceCommitted = false;
    Kotonoha_KtrfError choiceError{};
    if (!ktrfPlayDataPlayback->UpdateChoice(
            playDataGameplay->eventManager->SetSelectReached(),
            &playDataGameplay->promptId, &choiceCommitted, &choiceError)) {
      SDL_LogError(0, "[SD-REPLAY] PlayData choice restore/commit failed: %s",
                   choiceError.message);
      ClearTemporaryPlaybackPause();
      if (ktrfReplaySceneTransaction != nullptr)
        ktrfReplaySceneTransaction->Reset();
      ktrfPlayDataPlayback.reset();
      global.config.next = false;
      global.config.back = false;
      inRunning = nullptr;
      return SDL_APP_CONTINUE;
    }
    if (choiceCommitted) playDataGameplay->choiceCommitSubmitted = true;

    if (playDataGameplay->eventManager->CheckEnd(playDataGameplay) &&
        (playDataGameplay->prompt == nullptr ||
         !playDataGameplay->prompt->PresentationTailActive())) {
      if (ktrfReplaySceneTransaction != nullptr &&
          !ktrfReplaySceneTransaction->HasLease() &&
          !ktrfReplaySceneTransaction->HasPreparedFrameFor(playDataGameplay)) {
        SDL_LogError(0,
                     "[SD-REPLAY] PlayData ended without a retained outgoing frame");
        return SDL_APP_FAILURE;
      }
      bool terminal = false;
      Kotonoha_KtrfError replayError{};
      if (!ktrfPlayDataPlayback->AdvanceAfterSceneEnd(&terminal, &replayError)) {
        SDL_LogError(0, "[SD-REPLAY] PlayData private routing failed: %s",
                     replayError.message);
        ClearTemporaryPlaybackPause();
        if (ktrfReplaySceneTransaction != nullptr)
          ktrfReplaySceneTransaction->Reset();
        ktrfPlayDataPlayback.reset();
        global.config.next = false;
        global.config.back = false;
        inRunning = nullptr;
        return SDL_APP_CONTINUE;
      }
      global.config.next = false;
      if (terminal) {
        ClearTemporaryPlaybackPause();
        if (ktrfReplaySceneTransaction != nullptr)
          ktrfReplaySceneTransaction->Reset();
        ktrfPlayDataPlayback.reset();
        inRunning = nullptr;
      } else {
        inRunning = ktrfPlayDataPlayback->CurrentGameplay();
        if (inRunning == nullptr) {
          SDL_LogError(0, "[SD-REPLAY] PlayData scene swap lost Gameplay");
          return SDL_APP_FAILURE;
        }
        if (ktrfReplaySceneTransaction != nullptr)
          ktrfReplaySceneTransaction->CommitIfSwapped(
              playDataGameplay, inRunning, "playdata-current-scene");
      }
    } else {
      inRunning = playDataGameplay;
      if (global.config.next) global.config.next = false;
    }
  }

  if (ktrfController != nullptr && ktrfSceneTransaction != nullptr &&
      ktrfController->FlowState() !=
          Kotonoha::SchoolDaysFlowController::State::Gameplay) {
    // Main may have consumed callback_38 and entered SysMenu in this very
    // iteration after an automatic narrative frame was prepared above.
    ktrfSceneTransaction->Reset();
  }

  if (ktrfController != nullptr && ktrfSystemUi != nullptr) {
    if (ktrfController->FlowPromptVisible()) {
      if (!ktrfSystemUi->IsSaveFeelOpen()) {
        if (ktrfUiVariantSpecified) {
          std::string uiError;
          if (!ktrfSystemUi->OpenSaveFeel(global.render,
                                          ktrfAssetsRoot,
                                          ktrfUiVariant, &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] SaveFeel open failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
          }
        } else {
          SDL_LogError(0,
                       "[SD-SYSTEM-UI] missing required School Days display profile");
          return SDL_APP_FAILURE;
        }
      }

      if (ktrfSystemUi->IsSaveFeelOpen()) {
        int64_t feeling001 = 0;
        int64_t feeling002 = 0;
        Kotonoha_KtrfError stateError{};
        if (!ktrfController->ReadSessionInt("001", &feeling001, &stateError) ||
            !ktrfController->ReadSessionInt("002", &feeling002, &stateError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] feeling read failed: %s",
                       stateError.message);
          return SDL_APP_FAILURE;
        }

        Kotonoha::SchoolDaysSystemUi::Output output;
        ktrfSystemUi->Update(static_cast<uint32_t>(SDL_GetTicks()), feeling001,
                             feeling002, &output);
        if (output.hasSaveFeelResult && !SubmitSaveFeelUiOutput(output)) {
          return SDL_APP_FAILURE;
        }
      }
    } else if (ktrfSystemUi->IsSaveFeelOpen()) {
      ktrfSystemUi->Close();
    }

    const auto flowState = ktrfController->FlowState();
    const bool playDataHostSaveLoad =
        ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen() &&
        (ktrfReplayHostModal == KtrfReplayHostModal::Save ||
         ktrfReplayHostModal == KtrfReplayHostModal::Load);
    if (playDataHostSaveLoad ||
        flowState == Kotonoha::SchoolDaysFlowController::State::SaveLoad ||
        flowState == Kotonoha::SchoolDaysFlowController::State::TitleLoad ||
        flowState == Kotonoha::SchoolDaysFlowController::State::GameplaySave ||
        flowState == Kotonoha::SchoolDaysFlowController::State::GameplayLoad) {
      const bool playDataLoad =
          playDataHostSaveLoad &&
          ktrfReplayHostModal == KtrfReplayHostModal::Load;
      const bool controllerLoad =
          !playDataHostSaveLoad &&
          (flowState == Kotonoha::SchoolDaysFlowController::State::TitleLoad ||
           flowState == Kotonoha::SchoolDaysFlowController::State::GameplayLoad);
      const auto saveLoadMode =
          (playDataLoad || controllerLoad)
              ? Kotonoha::SchoolDaysSaveLoadModel::Mode::Load
              : Kotonoha::SchoolDaysSaveLoadModel::Mode::Save;
      if (!ktrfSystemUi->IsSaveLoadOpen()) {
        if (!ktrfUiVariantSpecified) {
          SDL_LogError(0,
                       "[SD-SYSTEM-UI] missing required School Days display profile");
          return SDL_APP_FAILURE;
        }
        std::string uiError;
        if (!ktrfSystemUi->OpenSaveLoad(global.render, ktrfAssetsRoot,
                                        ktrfUiVariant, saveLoadMode,
                                        ktrfSaveProfile.textInput,
                                        ktrfSaveProfile.useEnglish,
                                        ktrfSaveProfile.fontData, &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] SaveLoad open failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }
        ktrfSaveLoadMetadataPage = -1;
      }
      if (ktrfSaveLoadMetadataPage != ktrfSystemUi->SaveLoad().Page() &&
          !RefreshSaveLoadMetadataForUi()) {
        // A bad/openable slot or a temporary storage failure is recoverable at
        // the SaveLoad boundary.  Keep the modal open and retry on a later
        // frame rather than turning user data damage into process termination.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[SD-SAVELOAD] metadata refresh rejected; keeping view open");
      }

      Kotonoha::SchoolDaysSystemUi::Output output;
      ktrfSystemUi->Update(static_cast<uint32_t>(SDL_GetTicks()), 0, 0, &output);
      if (output.hasSaveLoadOutput &&
          output.saveLoadOutput.action ==
              Kotonoha::SchoolDaysSaveLoadModel::Action::SaveSlotRequested) {
        if (saveLoadMode != Kotonoha::SchoolDaysSaveLoadModel::Mode::Save) {
          SDL_LogError(0, "[SD-SAVELOAD] LOAD view emitted SAVE request");
          return SDL_APP_FAILURE;
        }
        const bool wrote = playDataHostSaveLoad
                               ? ExecutePlayDataSaveLoadWrite(output.saveLoadOutput)
                               : ExecuteSaveLoadWrite(output.saveLoadOutput);
        if (!wrote) {
          // The concrete I/O/persistence failure was already logged. Keep the
          // SAVE modal active and consume this frame so narrative input cannot
          // leak through a failed write attempt.
          return SDL_APP_CONTINUE;
        }
      }
    } else if (ktrfSystemUi->IsSaveLoadOpen()) {
      ktrfSystemUi->Close();
      ktrfSaveLoadMetadataPage = -1;
    }

    if (ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::TitleRouteMap ||
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::GameplayRouteMap) {
      if (!ktrfSystemUi->IsRouteMapOpen()) {
        Kotonoha::SchoolDaysKtrfAppController::RouteMapProjection projection;
        Kotonoha_KtrfError routeError{};
        if (!ktrfController->BuildInitialRouteMapProjection(&projection,
                                                            &routeError)) {
          SDL_LogError(0, "[SD-ROUTEMAP] initial projection failed: %s",
                       routeError.message);
          return SDL_APP_FAILURE;
        }
        std::string uiError;
        if (!ktrfSystemUi->OpenRouteMap(
                global.render, ktrfAssetsRoot, SchoolDaysRouteMapVariant(),
                projection.major, projection.subpage, projection.alternateHost,
                projection.completionPercent, ktrfSaveProfile.useEnglish,
                ktrfSaveProfile.fontData,
                &uiError)) {
          SDL_LogError(0, "[SD-ROUTEMAP] presenter open failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }
        if (!ApplySchoolDaysRouteMapProjection(projection)) {
          SDL_LogError(0, "[SD-ROUTEMAP] initial projection/model mismatch");
          return SDL_APP_FAILURE;
        }
      } else if (ktrfRouteMapProjectionMajor != ktrfSystemUi->RouteMap().Major() ||
                 ktrfRouteMapProjectionSubpage !=
                     ktrfSystemUi->RouteMap().Subpage()) {
        if (!RefreshSchoolDaysRouteMapProjection()) return SDL_APP_FAILURE;
      }
    } else if (ktrfSystemUi->IsRouteMapOpen()) {
      ktrfSystemUi->Close();
      ktrfRouteMapProjectionMajor = -1;
      ktrfRouteMapProjectionSubpage = -1;
    }

    if (ktrfController->FlowState() ==
        Kotonoha::SchoolDaysFlowController::State::Title) {
      if (ktrfTitleReplayActive) {
        const bool temporaryPlayback =
            (ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) ||
            (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen());
        if (!temporaryPlayback) {
          if (!StartSchoolDaysSystemBgm()) return SDL_APP_FAILURE;
          if (!ktrfSystemUi->IsReplayOpen()) {
            std::string uiError;
            if (!ktrfSystemUi->OpenReplay(
                    global.render, ktrfAssetsRoot, SchoolDaysReplayVariant(),
                    ktrfSaveProfile.textInput, ktrfSaveProfile.useEnglish,
                    ktrfSaveProfile.fontData, &uiError)) {
              SDL_LogError(0, "[SD-SYSTEM-UI] Replay open failed: %s",
                           uiError.c_str());
              return SDL_APP_FAILURE;
            }
          }
          if (!RefreshSchoolDaysReplayAvailability()) return SDL_APP_FAILURE;
        } else if (ktrfSystemUi->IsReplayOpen() || ktrfSystemUi->IsTitleOpen()) {
          // Replay Gameplay owns audio while the temporary FILM is active.
          StopSchoolDaysTitleBgm();
          ktrfSystemUi->Close();
        }
      } else if (!ktrfSystemUi->IsTitleOpen()) {
        Kotonoha::SchoolDaysEndBackgroundResolver::Profile profile;
        std::string uiError;
        if (!Kotonoha::SchoolDaysTitleProfile::Load(
                ktrfAssetsRoot, &profile, &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] Title profile load failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }

        Kotonoha::SchoolDaysEndBackgroundResolver::Result background;
        Kotonoha_KtrfError stateError{};
        if (!ktrfController->ResolveTitleBackground(profile, &background,
                                                    &stateError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] Title background resolve failed: %s",
                       stateError.message);
          return SDL_APP_FAILURE;
        }

        bool endClear = false;
        if (!ktrfController->TitleEndClearAvailable(&endClear, &stateError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] Title EndClear query failed: %s",
                       stateError.message);
          return SDL_APP_FAILURE;
        }

        Kotonoha::SchoolDaysTitleModel::HostState hostState;
        // Normal ctor/terminal->Title proves +E8=0 and +0x34=0.  +0xEC(1)
        // reads EndClear directly and controls REVER availability.  +0xEC(0)
        // first gates that same read on interface +0x1F0.  Startup 0x41F8D6
        // parses [EndBGView] with default 1, stores it at owner +0x26C and the
        // sole 0x420150 setter copies that exact value to main+0x21C ==
        // interface+0x1F0.  The Title_Clear visual gate is therefore exactly
        // EndBGView && EndClear.
        hostState.ac = false;
        hostState.alternate = false;
        hostState.endClearVisual = profile.endBgView && endClear;
        hostState.endClearReplay = endClear;
        if (!ktrfSystemUi->OpenTitle(global.render, ktrfAssetsRoot,
                                     ktrfUiVariant, hostState, background,
                                     profile.baseAsset,
                                     &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] Title open failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }
        if (!StartSchoolDaysTitleBgm(profile)) {
          SDL_LogError(0, "[SD-TITLE] TitleBGM startup failed");
          ktrfSystemUi->Close();
          return SDL_APP_FAILURE;
        }
        const uint64_t now = SDL_GetTicks();
        if (!ktrfFrontendTransition.Active(now)) {
          ktrfFrontendTransition.BeginIncoming(
              ktrfTitleHasPresented
                  ? Kotonoha::SchoolDaysFrontendTransition::Color::White
                  : Kotonoha::SchoolDaysFrontendTransition::Color::Black,
              ktrfTitleHasPresented ? 500u : 1000u, now);
        }
        ktrfTitleHasPresented = true;
      }
    } else if (ktrfSystemUi->IsTitleOpen()) {
      const auto state = ktrfController->FlowState();
      const bool titleModal =
          state == Kotonoha::SchoolDaysFlowController::State::TitleConfig ||
          state == Kotonoha::SchoolDaysFlowController::State::TitleExit ||
          state == Kotonoha::SchoolDaysFlowController::State::TitleLoad;
      if (!titleModal) StopSchoolDaysTitleBgm();
      ktrfSystemUi->Close();
    }

    if (ktrfController->FlowState() !=
            Kotonoha::SchoolDaysFlowController::State::Title &&
        ktrfTitleReplayActive) {
      ktrfTitleReplayActive = false;
      if (ktrfSystemUi->IsReplayOpen()) ktrfSystemUi->Close();
    }

    if (ktrfReplayHostModal == KtrfReplayHostModal::Config ||
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::TitleConfig ||
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::GameplayConfig) {
      if (!ktrfSystemUi->IsConfigOpen()) {
        const auto visualState = CurrentSchoolDaysConfigVisualState();
        std::string uiError;
        if (!ktrfSystemUi->OpenConfig(global.render, ktrfAssetsRoot,
                                      SchoolDaysConfigVariant(), ktrfConfigPage,
                                      visualState, &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] ConfigMenu open failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }
      } else {
        std::string uiError;
        if (!ktrfSystemUi->SetConfigVisualState(
                CurrentSchoolDaysConfigVisualState(), &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] ConfigMenu state refresh failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }
      }
    } else if (ktrfSystemUi->IsConfigOpen()) {
      ktrfSystemUi->Close();
    }

    if (ktrfReplayHostModal == KtrfReplayHostModal::MainMenuConfirm ||
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::TitleExit ||
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::GameplayExit) {
      if (!ktrfSystemUi->IsExitOpen()) {
        Kotonoha::SchoolDaysExitViewModel::Variant exitVariant =
            Kotonoha::SchoolDaysExitViewModel::Variant::Standard;
        switch (ktrfUiVariant) {
        case Kotonoha::SchoolDaysSaveFeelModel::Variant::Standard:
          exitVariant = Kotonoha::SchoolDaysExitViewModel::Variant::Standard;
          break;
        case Kotonoha::SchoolDaysSaveFeelModel::Variant::Wide:
          exitVariant = Kotonoha::SchoolDaysExitViewModel::Variant::Wide;
          break;
        case Kotonoha::SchoolDaysSaveFeelModel::Variant::WideFull:
          exitVariant = Kotonoha::SchoolDaysExitViewModel::Variant::WideFull;
          break;
        case Kotonoha::SchoolDaysSaveFeelModel::Variant::WideNote:
          exitVariant = Kotonoha::SchoolDaysExitViewModel::Variant::WideNote;
          break;
        }
        std::string uiError;
        const auto artwork =
            (ktrfReplayHostModal == KtrfReplayHostModal::MainMenuConfirm ||
             ktrfController->FlowState() ==
                    Kotonoha::SchoolDaysFlowController::State::GameplayExit)
                ? Kotonoha::SchoolDaysExitViewModel::BaseArtwork::Title
                : Kotonoha::SchoolDaysExitViewModel::BaseArtwork::Exit;
        if (!ktrfSystemUi->OpenExit(
                global.render, ktrfAssetsRoot, exitVariant,
                artwork,
                &uiError)) {
          SDL_LogError(0, "[SD-SYSTEM-UI] ExitView open failed: %s",
                       uiError.c_str());
          return SDL_APP_FAILURE;
        }
      }
    } else if (ktrfSystemUi->IsExitOpen()) {
      ktrfSystemUi->Close();
    }
  }

  if (ktrfController != nullptr &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Gameplay) {
    if (!EnsureSchoolDaysMenuBar()) return SDL_APP_FAILURE;
    const Kotonoha::Gameplay* menuGameplay = ktrfController->CurrentGameplay();
    const bool menuUserPaused =
        menuGameplay != nullptr ? menuGameplay->UserPaused(&global)
                                : global.config.paused.load();
    ktrfMenuBar->SetRuntimeState(ktrfRetailConfigState.autoMode,
                                 menuUserPaused, ktrfSchoolDaysSpeedIndex,
                                 ktrfRetailConfigState.superSkip,
                                 true, false, false, false);
    ktrfMenuBar->SetReplayIndicatorState(false);
    if (ktrfController->ConsumeTopMeterRefreshRequest()) {
      ktrfMenuBar->RequestFeelingRefresh();
    }
    int64_t feeling001 = 0;
    int64_t feeling002 = 0;
    if (ktrfMenuBar->FeelingLatch()) {
      Kotonoha_KtrfError stateError{};
      if (!ktrfController->ReadSessionInt("001", &feeling001, &stateError) ||
          !ktrfController->ReadSessionInt("002", &feeling002, &stateError) ||
          feeling001 < std::numeric_limits<int>::min() ||
          feeling001 > std::numeric_limits<int>::max() ||
          feeling002 < std::numeric_limits<int>::min() ||
          feeling002 > std::numeric_limits<int>::max()) {
        SDL_LogError(0, "[SD-MENUBAR] feeling update read failed: %s",
                     stateError.message);
        return SDL_APP_FAILURE;
      }
    }
    // Fade/reveal is independent from the feeling-meter refresh latch and must
    // advance every outer tick (native FUN_10024100/FUN_100255C0).
    ktrfMenuBar->Update(static_cast<uint32_t>(SDL_GetTicks()),
                        static_cast<int>(feeling001),
                        static_cast<int>(feeling002));
  }

  if (ktrfController != nullptr) {
    if (ktrfSceneTransaction != nullptr &&
        ktrfController->FlowState() ==
            Kotonoha::SchoolDaysFlowController::State::Gameplay) {
      Kotonoha::Gameplay *afterGameplay = ktrfController->CurrentGameplay();
      ktrfSceneTransaction->CommitIfSwapped(beforeGameplay, afterGameplay,
                                            "current-scene");
      if (beforeGameplay != afterGameplay) {
        ktrfMenuBackStartGameplay =
            ktrfBackRequested ? afterGameplay : nullptr;
      }
      if (ktrfBackRequested && beforeGameplay == afterGameplay) {
        ktrfSceneTransaction->CancelPreparedFor(beforeGameplay,
                                                "back-no-swap");
      }
      ktrfSceneTransaction->ReleaseIfIncomingReady(afterGameplay);
    }
  } else {
    app->CacheSceneTransitionFrameIfNeeded(inRunning);
    app->PresentSceneTransitionFrame(inRunning);
  }

  // Episode 1 owns this exact presentation inside its authored WMV. Episode
  // 2..6 are host overlays armed only by the natural episode handoff above.
  // Render before MenuBar so the caption remains part of the scene composition.
  if (!RenderSchoolDaysEpisodeOverlay(inRunning)) {
    SDL_LogWarn(0, "[SD-EPISODE] optional overlay render failed: %s",
                SDL_GetError());
    ResetSchoolDaysEpisodeOverlay();
  }

  const bool ktrfReplayMenuBarVisible =
      ktrfReplayHostModal == KtrfReplayHostModal::None &&
      ((ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) ||
       (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()));
  const bool ktrfGameplayMenuBarVisible =
      ktrfController != nullptr &&
      ktrfController->FlowState() ==
          Kotonoha::SchoolDaysFlowController::State::Gameplay;
  if (ktrfMenuBar != nullptr &&
      (ktrfGameplayMenuBarVisible || ktrfReplayMenuBarVisible)) {
    std::string menuError;
    if (!ktrfMenuBar->Render(global.render, &menuError)) {
      SDL_LogError(0, "[SD-MENUBAR] render failed: %s", menuError.c_str());
      return SDL_APP_FAILURE;
    }
  }

  if (ktrfSystemUi != nullptr &&
      (ktrfSystemUi->IsSaveFeelOpen() || ktrfSystemUi->IsSaveLoadOpen() ||
       ktrfSystemUi->IsTitleOpen() || ktrfSystemUi->IsConfigOpen() ||
       ktrfSystemUi->IsExitOpen() ||
       ktrfSystemUi->IsReplayOpen() || ktrfSystemUi->IsRouteMapOpen())) {
    std::string uiError;
    if (!ktrfSystemUi->Render(global.render, &uiError)) {
      SDL_LogError(0, "[SD-SYSTEM-UI] system view render failed: %s",
                   uiError.c_str());
      return SDL_APP_FAILURE;
    }
    if (ktrfSystemUi->IsConfigOpen() &&
        !RenderSchoolDaysModernDisplayPanel(app)) {
      SDL_LogError(0, "[SD-DISPLAY] modern Options panel render failed: %s",
                   SDL_GetError());
      return SDL_APP_FAILURE;
    }
  }

  if (!RenderSchoolDaysFrontendTransition()) {
    SDL_LogError(0, "[SD-TRANSITION] frontend fade render failed: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  TTF_Font *preferredFont = nullptr;

  auto it = app->fonts.find("ConcertOne-Regular");

  if (it != app->fonts.end()) {
    preferredFont = it->second;
  }

  if (global.config.showFps) {
    KotonohaFPSRender(global.window, global.render, preferredFont, nullptr);
  }

  if (global.config.showTimestamp && inRunning != nullptr) {
    void *timestampArgs[2] = {inRunning->tm, preferredFont};

    KotonohaTimestampRender(global.window, global.render, timestampArgs,
                            nullptr);
  }

  if (inRunning != nullptr && inRunning->ShouldShowLoadingOverlay()) {
    KotonohaLoadingRender(global.window, global.render, preferredFont, nullptr);
  }

  const bool titleFrontend = ktrfController != nullptr &&
      (ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::Title ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleLoad ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleRouteMap ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleConfig ||
       ktrfController->FlowState() ==
           Kotonoha::SchoolDaysFlowController::State::TitleExit);
  const bool temporaryPlayback =
      (ktrfReplayPlayback != nullptr && ktrfReplayPlayback->IsOpen()) ||
      (ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen());
  ktrfFrameDebug.mode = ktrfController != nullptr
                           ? SchoolDaysFrameDebug::Mode::Ktrf
                           : SchoolDaysFrameDebug::Mode::Engine;
  // Replay/HScene and PlayData are frontend-owned, but they still own a real
  // Gameplay.  Keep that physical Gameplay visible to the generic engine debug
  // tools instead of hiding it merely because the retained narrative
  // controller is in Title state.  School Days routing/state inspection remains
  // controller-owned and is handled separately by the game debug wrapper.
  ktrfFrameDebug.debugGameplay =
      titleFrontend && !temporaryPlayback ? nullptr : inRunning;
  ktrfFrameDebug.mcpGameplay = inRunning;
  return SDL_APP_CONTINUE;
}

bool SchoolDaysSession::RunDebugUi(Kotonoha::Kotonoha *app,
                                   Kotonoha_Game &game) {
  switch (ktrfFrameDebug.mode) {
  case SchoolDaysFrameDebug::Mode::Ktrf:
    {
      Kotonoha::SchoolDaysKtrfSession* temporarySession =
          ktrfPlayDataPlayback != nullptr && ktrfPlayDataPlayback->IsOpen()
              ? &ktrfPlayDataPlayback->Session()
              : nullptr;
    return Kotonoha_SchoolDaysKtrfDebugGuiRun(
        ktrfController.get(), app, ktrfFrameDebug.debugGameplay,
        temporarySession, ktrfSceneTransaction.get(), game);
    }
  case SchoolDaysFrameDebug::Mode::EngineIfEnabled:
    return !game.config.enableDebugUi ||
           Kotonoha::Debug::Run(app, ktrfFrameDebug.debugGameplay, game);
  case SchoolDaysFrameDebug::Mode::Engine:
    break;
  }
  return Kotonoha::Debug::Run(app, ktrfFrameDebug.debugGameplay, game);
}

Kotonoha::Gameplay *SchoolDaysSession::RunningGameplay() {
  return ktrfFrameDebug.mcpGameplay;
}

SDL_AppResult SchoolDaysSession::HandleEvent(SDL_Event *event,
                                             Kotonoha_Game *) {
  return SchoolDaysHandleEvent(appstate_, event);
}

SDL_AppResult SchoolDaysSession::RenderFrame(Kotonoha::Kotonoha *app,
                                             Kotonoha_Game *) {
  return SchoolDaysRenderFrame(app);
}

SDL_AppResult LocalEvent(void *, SDL_Event *event) {
  return ktrfFlow != nullptr ? ktrfFlow->HandleEvent(event, &global)
                             : SDL_APP_CONTINUE;
}

SDL_AppResult LocalIterate(void *appstate) {
  Kotonoha::Kotonoha *app = Kotonoha::GetApp(appstate);
  if (app == nullptr || ktrfFlow == nullptr) {
    return SDL_APP_FAILURE;
  }
  // School Days renders the whole frame itself; skip the engine's default.
  Kotonoha::MarkFrameHandled();
  return ktrfFlow->RunFrame(app, &global, nullptr);
}

void LocalQuit(void *, SDL_AppResult) {
  ktrfFlow.reset();

  // Temporary frontend Gameplay destruction needs the still-live controller/
  // engine factory.
  ktrfPlayDataPlayback.reset();
  ktrfReplayPlayback.reset();
  ktrfReplayHostModal = KtrfReplayHostModal::None;
  SDL_SetAtomicInt(&global.textLogCaptureEnabled, 0);
  global.textLogAppend = nullptr;
  global.textLogUserdata = nullptr;
  ResetSchoolDaysBackLog();
  StopSchoolDaysTitleBgm();
  if (ktrfConfigStorage != nullptr) {
    std::string configError;
    // Config actions cache their native-style mutations immediately. Shutdown
    // is only the physical flush boundary: synthesizing a CacheState here would
    // dirty an untouched/missing Config.DAT, including SDL_AppInit failures for
    // which SDL still invokes SDL_AppQuit.
    if (!ktrfConfigStorage->Flush(&configError)) {
      SDL_LogError(0, "[SD-CONFIG] shutdown Config.DAT flush failed: %s",
                   configError.c_str());
    }
  }
  // Native shutdown reaches the standalone GlobalFlag writer even when the
  // user did not perform a SaveLoad write after an ending.  Flush before the
  // controller/storage are destroyed so EndXX/EndNo/EndClear/AllClear and
  // terminal global writes survive a direct quit from TitleView.
  (void)FlushSchoolDaysGlobalFlag("shutdown");
  ktrfSceneTransaction.reset();
  ktrfReplaySceneTransaction.reset();
  ktrfMenuBar.reset();
  ktrfSystemUi.reset();
  ktrfConfigStorage.reset();
  ktrfSaveStorage.reset();
  ktrfSaveProfile = {};
  ktrfSaveLoadMetadataPage = -1;
  ktrfConfigPage = Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
  ktrfTitleReplayActive = false;
  ktrfFrontendTransition.Reset();
  ktrfFrontendHandoff = KtrfFrontendHandoff::None;
  ktrfTitleHasPresented = false;
  ResetSchoolDaysEpisodeOverlay();
  ktrfRouteMapProjectionMajor = -1;
  ktrfRouteMapProjectionSubpage = -1;
  ktrfRetailDisplayState = {};
  ktrfRetailDisplayBackendState = {};
  ktrfModernDisplayPanel = {};
  ktrfRetailConfigState =
      Kotonoha::SchoolDaysRetailConfigPolicy::Defaults();
  Kotonoha_SetMenuVoiceEnabled(&global, true);
  Kotonoha_SetMessageLayerEnabled(&global, true);
  global.profileAssetsRoot = nullptr;
  ktrfAssetsRoot.clear();
  ktrfController.reset();
  Kotonoha::UninstallMountedSchoolDaysAssetResolver();
  Kotonoha::UnmountSchoolDaysRetailGpks();
}


