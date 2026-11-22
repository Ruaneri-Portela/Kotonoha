#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysMobileProfile.hpp"
#include "SchoolDaysAssets.hpp"
#include "SchoolDaysOverlaySelection.hpp"

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/routing/SchoolDaysDesktopStartupProfile.hpp>
#include <Kotonoha/routing/SchoolDaysDisplayPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>
#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysTitleProfile.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>
#include <Kotonoha/routing/SchoolDaysUpscalingPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysGpkLanguageProfile.hpp>
#include <Kotonoha/mobile/AndroidDisplay.hpp>
#include <Kotonoha/SchoolDaysGpkArchive.hpp>
#include <Kotonoha/routing/SchoolDaysShaderPresetCatalog.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace SchoolDays {
namespace {
namespace fs = std::filesystem;

Kotonoha::EngineSettings defaultSchoolDaysLauncherSettings() {
  Kotonoha::EngineSettings defaults;
#if defined(_WIN32) && !defined(__ANDROID__)
  // Keep the launcher startup policy identical to main.cpp's Windows default.
  // Loaded settings.json still wins because the generic base loads it later.
  defaults.renderer = "opengl";
  defaults.mode = Kotonoha::WindowMode::Borderless;
  defaults.vsync = true;
  defaults.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
  defaults.upscaling.preset = Kotonoha::UpscalePreset::ArtCNN;
  defaults.upscaling.factor = 1.5f;
#elif defined(__ANDROID__) || defined(ANDROID)
  // Use the same mobile-low first-run profile as SDL_AppInit. Opening the
  // launcher must not save generic desktop defaults before the game starts.
  defaults.renderer = "opengles2";
  applyMobileDisplayDefaults(defaults);
#endif
  return defaults;
}

struct SavedLanguage {
  std::string name, jsonPath, lxhJsonPath;
};

SavedLanguage readLanguagePreference(const fs::path &path) {
  std::error_code ec;
  if (path.empty() || !fs::is_regular_file(path, ec) ||
      fs::file_size(path, ec) > 32768 || ec)
    return {};
  std::ifstream input(path);
  SavedLanguage saved;
  std::getline(input, saved.name);
  std::getline(input, saved.jsonPath);
  std::string thirdLine;
  std::getline(input, thirdLine);
  constexpr std::string_view key = "lxh-json=";
  if (thirdLine.rfind(key.data(), 0) == 0)
    saved.lxhJsonPath = thirdLine.substr(key.size());
  return saved;
}

bool regularTranslationFile(const std::string &path) {
  if (path.empty()) return false;
  const auto resolved = Kotonoha::FileResolver::findAsset(path).value_or(path);
  std::error_code ec;
  return fs::is_regular_file(resolved, ec) && !ec;
}

// Keep the original user-selected filename when moving the install's
// translations from a flat directory into translations/jsons. Never
// substitute an arbitrary similarly named external file or another locale.
std::string resolveInstalledJson(const std::string &settingsPath,
                                 const std::string &stored) {
  if (settingsPath.empty() || stored.empty()) return stored;
  const fs::path original(stored);
  if (regularTranslationFile(stored)) return stored;
  if (original.extension() != ".json" || original.filename().empty())
    return stored;
  const auto root = fs::path(settingsPath).parent_path();
  const auto translations = root / "translations";
  const auto candidate = original.lexically_normal();
  // Relocate absolute paths only if they point to this game's own old flat
  // translation directory (including its new jsons/ child).
  if (candidate.is_absolute()) {
    const auto relative = candidate.lexically_relative(translations);
    if (relative.empty() || relative == "." ||
        *relative.begin() == "..") return stored;
  }
  const auto filename = original.filename();
  // Shared discovery applies exact filename/case, checks ordinary files and
  // preserves the legacy flat fallback only when the organized file is absent.
  const auto managed = Kotonoha::SchoolDaysGpkLanguageProfile::JsonPath(
      settingsPath, filename.string());
  if (regularTranslationFile(managed.string()))
    return fs::absolute(managed).string();
  for (const auto &local : {translations / "jsons" / filename,
                            translations / filename,
                            root / filename}) {
    if (regularTranslationFile(local.string()))
      return fs::absolute(local).string();
  }
  return stored;
}

bool isGpkLanguage(SchoolDays::Launcher::Language language) {
  using Language = SchoolDays::Launcher::Language;
  return language == Language::EnglishGpk ||
         language == Language::BrazilianGpk ||
         language == Language::SpanishGpk ||
         language == Language::ChineseGpk ||
         language == Language::RussianGpk;
}

std::string gpkLocale(SchoolDays::Launcher::Language language) {
  using Language = SchoolDays::Launcher::Language;
  switch (language) {
  case Language::EnglishGpk: return "en";
  case Language::BrazilianGpk: return "pt-BR";
  case Language::SpanishGpk: return "es";
  case Language::ChineseGpk: return "zh-CN";
  case Language::RussianGpk: return "ru";
  default: return {};
  }
}

struct JsonPreset {
  SchoolDays::Launcher::Language language;
  const char *filename;
  const char *displayName;
  const char *savedName;
};

constexpr std::array<JsonPreset, 5> kJsonPresets{{
    {SchoolDays::Launcher::Language::EnglishJson, "en.json", "English", "english-json"},
    {SchoolDays::Launcher::Language::BrazilianJson, "pt-BR.json", "Português (Brasil)", "brazilian-json"},
    {SchoolDays::Launcher::Language::Spanish, "es.json", "Español", "spanish"},
    {SchoolDays::Launcher::Language::ChineseSimplified, "zh-CN.json", "Chinese - Simplified", "chinese"},
    {SchoolDays::Launcher::Language::RussianJson, "ru.json", "Russian", "russian-json"},
}};

const JsonPreset *jsonPresetForLanguage(SchoolDays::Launcher::Language language) {
  for (const auto &preset : kJsonPresets)
    if (preset.language == language) return &preset;
  return nullptr;
}

const JsonPreset *jsonPresetForFilename(const std::string &path) {
  const std::string filename = fs::path(path).filename().string();
  for (const auto &preset : kJsonPresets)
    if (filename == preset.filename) return &preset;
  return nullptr;
}

const JsonPreset *jsonPresetForSavedName(const std::string &name) {
  for (const auto &preset : kJsonPresets)
    if (name == preset.savedName) return &preset;
  return nullptr;
}

bool jsonPreferenceSelected(const std::string &name) {
  return jsonPresetForSavedName(name) || name == "custom";
}

// Runtime reads exactly this game-owned sidecar key, leaving settings.json
// exclusively responsible for physical monitor/output settings.
constexpr std::string_view kAspectKey = "ContentAspectPreference=";
constexpr const char *kShaderSidecarHeader = "SchoolDaysShaderPaths.v1";

constexpr const char *kTitleMusicId = "launcher-title-loop";

// Copy the selected retail TitleBGM once, then release the GPK mount before
// the launcher validates/saves Options. The MediaPlayer's FFmpeg decoder must
// never retain an AssetStream against an unmounted GPK archive.
class LauncherMusicBytes final : public Kotonoha::AssetProvider {
  const std::vector<Uint8> &bytes;

public:
  explicit LauncherMusicBytes(const std::vector<Uint8> &value) : bytes(value) {}
  std::string name() const override { return "School Days launcher music"; }
  bool containsAsset(const std::string &id) const override {
    return id == kTitleMusicId;
  }
  std::vector<Uint8> readAsset(const std::string &id) const override {
    if (!containsAsset(id))
      throw std::runtime_error("Unknown launcher music resource");
    return bytes;
  }
};

bool readLauncherMusicEnabled(const fs::path &path) {
  std::error_code ec;
  if (path.empty() || !fs::is_regular_file(path, ec) || ec ||
      fs::file_size(path, ec) > 16 || ec)
    return true;
  std::ifstream input(path);
  std::string value;
  std::getline(input, value);
  return value != "off";
}

std::filesystem::path shaderSidecarPath(const std::string &settingsPath) {
  return fs::path(settingsPath).parent_path() / "shader-paths.txt";
}

int readAspectPreference(const fs::path &root) {
  for (const auto &file : {root / "Kotonoha.ini", root / "RemasterDisplay.ini"}) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || ec) continue;
    if (fs::file_size(file, ec) > 131072u || ec)
      throw std::runtime_error("Oversized School Days aspect preference file");
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot read School Days aspect preference");
    std::string line;
    while (std::getline(input, line)) {
      if (line.rfind(kAspectKey.data(), 0) != 0) continue;
      try {
        return std::clamp(std::stoi(line.substr(kAspectKey.size())), 0, 2);
      } catch (const std::exception &) {
        return 0;
      }
    }
    // Match Runtime: fallback to RemasterDisplay.ini only if opening
    // Kotonoha.ini failed. An existing Kotonoha.ini owns the preference.
    return 0;
  }
  return 0;
}

void writeAspectPreference(const fs::path &root, int preference) {
  const auto path = root / "Kotonoha.ini";
  std::string contents;
  std::error_code ec;
  if (fs::is_regular_file(path, ec) && !ec) {
    if (fs::file_size(path, ec) > 131072u || ec)
      throw std::runtime_error("Oversized School Days aspect preference file");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot reopen School Days aspect file");
    contents.assign(std::istreambuf_iterator<char>(input), {});
  }
  const std::string replacement =
      std::string(kAspectKey) + std::to_string(std::clamp(preference, 0, 2));
  bool replaced = false;
  std::size_t cursor = 0;
  while (cursor < contents.size()) {
    if (contents.compare(cursor, kAspectKey.size(), kAspectKey) == 0) {
      const std::size_t end = contents.find_first_of("\r\n", cursor);
      contents.replace(cursor, end == std::string::npos ? contents.size() - cursor
                                                        : end - cursor, replacement);
      replaced = true;
      break;
    }
    const auto newline = contents.find('\n', cursor);
    if (newline == std::string::npos) break;
    cursor = newline + 1;
  }
  if (!replaced) {
    if (!contents.empty() && contents.back() != '\n') contents.push_back('\n');
    if (contents.empty()) contents = "[KotonohaConfig][Settings]\n";
    contents += replacement + "\n";
  }
  Kotonoha::writeTextFileAtomically(path.string(), contents);
}

int nearestRetailVolume(const Kotonoha::SchoolDaysRetailConfigPolicy::State &state,
                        Kotonoha::SchoolDaysRetailConfigPolicy::NativeAudioBank bank,
                        float volume) {
  using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
  int nearest = Policy::Level(state, bank);
  float error = std::numeric_limits<float>::max();
  for (int level = 0; level <= 10; ++level) {
    auto candidate = state;
    Policy::SetLevel(&candidate, bank, level);
    const float distance = std::abs(Policy::LinearGain(candidate, bank) - volume);
    if (distance < error) {
      nearest = level;
      error = distance;
    }
  }
  return nearest;
}

// The pack is permanent gameplay content, never a user preference. Retail GPK
// installs must supply all 21 unique LxH media resources; extracted base-only
// test fixtures remain valid without them. The seven ORS names also exist in
// the original retail script, so they cannot identify an installed LxH pack.
bool checkIntegratedLxhContent(const Assets &assets, bool *present,
                               std::string &error) {
  if (present == nullptr) return false;
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  (void)assets;
  error.clear();
  *present = false;
  return true;
#else
  std::size_t found = 0;
  std::size_t expected = 0;
  for (const auto &logical : Kotonoha::SchoolDaysLxhContent::PackEntries()) {
    if (logical.compare(0, 4, "LxH/") != 0) continue;
    ++expected;
    try {
      (void)assets.resolve(logical);
      ++found;
    } catch (const std::exception &) {
      // Report incomplete media below instead of allowing partial epilogues.
    }
  }
  if (found != expected && (found != 0 || assets.orsRoot() == assets.root())) {
    error = "Incomplete integrated School Days epilogues: " +
            std::to_string(found) + "/" + std::to_string(expected) +
            " media resources found. Install the complete Packs/LxH.GPK.";
    return false;
  }
  *present = found == expected;
  return true;
#endif
}
} // namespace

Launcher::Launcher(std::string settingsPath, std::string directory,
                   std::string route)
    : GameLauncher(
          "School Days HQ - Launcher", std::move(settingsPath),
          {"Game folder or Packs folder",
           PathKind::Folder,
           {},
           "dir.txt",
           "Use your own School Days HQ retail data (29 GPKs), or an extracted "
           "directory with Script/English, or an ISO containing these files. "
           "ISO data uses a temporary internal mount."},
          defaultSchoolDaysLauncherSettings()) {
  setAudioChannels({"BGM", "Voice", "SE", "Movie"});
  loadPath(std::move(directory));
  route =
      findGamePath("school-days",
                   {route, rememberedPath(getSettingsPath(), RouteRememberFile),
                    Assets::findDefaultRoute().string()},
                   validateRoute, {"school-days-hq.ktnroute"});
  routeOption =
      addPathOption({"Route file (.ktnroute)",
                     PathKind::File,
                     {{"KTRF routes", "ktnroute;ktrf"}, {"All files", "*"}},
                     RouteRememberFile,
                     "Select a compatible School Days HQ KTRF route."},
                    validateRoute, std::move(route));
  if (!optionPathValid(routeOption))
    setOptionPath(routeOption, {});

  // EngineSettings serializes asset references by filename for portability.
  // Resolve this game's JSON from the settings folder (or its translations/
  // directory) on reopening, without modifying the generic FileResolver.
  if (!getSettingsPath().empty() &&
      !settings().translationPath.empty() &&
      !regularTranslationFile(settings().translationPath)) {
    const auto folder = fs::path(getSettingsPath()).parent_path();
    const auto stored = fs::path(settings().translationPath);
    settings().translationPath =
        resolveInstalledJson(getSettingsPath(), settings().translationPath);
  }

  if (!getSettingsPath().empty()) {
    languagePreferenceFile =
        fs::path(getSettingsPath()).parent_path() / "language.txt";
    const auto savedLanguage = readLanguagePreference(languagePreferenceFile);
    if (jsonPreferenceSelected(savedLanguage.name)) {
      const auto *preset = jsonPresetForSavedName(savedLanguage.name);
      selectedLanguage = preset ? preset->language : Language::CustomJson;
      if (!savedLanguage.jsonPath.empty())
        settings().translationPath =
            resolveInstalledJson(getSettingsPath(), savedLanguage.jsonPath);
      selectedLocaleBaseJsonPath = settings().translationPath;
    } else if (!settings().translationPath.empty()) {
      const auto *preset = jsonPresetForFilename(settings().translationPath);
      selectedLanguage = preset ? preset->language : Language::CustomJson;
      selectedLocaleBaseJsonPath = settings().translationPath;
    }
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    if (!savedLanguage.lxhJsonPath.empty()) {
      selectedManualLxhJsonPath =
          resolveInstalledJson(getSettingsPath(), savedLanguage.lxhJsonPath);
      manualLxhBaseJsonPath = settings().translationPath;
    }
#endif
    // Explicit opt-in GPK profiles are separate from legacy JSON selections.
    // The existing chinese/zh-CN.json preference is kept untouched until the
    // player explicitly selects the GPK version in this launcher.
    const std::string nativeLocale = rememberedGpkLocale(getSettingsPath());
    if (!nativeLocale.empty()) {
      selectedLanguage = nativeLocale == "en" ? Language::EnglishGpk
          : nativeLocale == "pt-BR" ? Language::BrazilianGpk
          : nativeLocale == "es" ? Language::SpanishGpk
          : nativeLocale == "ru" ? Language::RussianGpk
                                  : Language::ChineseGpk;
      settings().translationPath.clear();
      selectedLocaleBaseJsonPath.clear();
      selectedManualLxhJsonPath.clear();
      manualLxhBaseJsonPath.clear();
    }
  } else if (!settings().translationPath.empty()) {
    const auto *preset = jsonPresetForFilename(settings().translationPath);
    selectedLanguage = preset ? preset->language : Language::CustomJson;
    selectedLocaleBaseJsonPath = settings().translationPath;
  }

  // The base launcher loads settings.json before video initialization. Do NOT
  // replace a stored manual choice with the native desktop on each startup.
  // A new/invalid profile is seeded once, after the SDL video driver is live.
  std::error_code ec;
  const bool savedFile = !getSettingsPath().empty() &&
                         fs::is_regular_file(getSettingsPath(), ec) && !ec;
  bool validSavedSettings = false;
  if (savedFile) {
    try {
      (void)Kotonoha::EngineSettings::load(getSettingsPath());
      validSavedSettings = true;
    } catch (const std::exception &) {
      // Base Launcher already reported the invalid JSON and chose defaults.
    }
  }
  initialDisplayPending = !validSavedSettings;
  initialDisplayBaseline = settings();
#if defined(__ANDROID__) || defined(ANDROID)
  // Android's GLES Surface is platform-managed, not a desktop display/window.
  // Even a relocated Windows JSON must not select an unavailable renderer.
  settings().renderer = "opengles2";
  settings().mode = Kotonoha::WindowMode::Borderless;
#endif
  if (!getSettingsPath().empty() &&
      !settings().upscaling.shaderFiles.empty())
    settings().upscaling.shaderFiles = resolveStoredShaderPaths(
        getSettingsPath(), settings().upscaling.shaderFiles);
  if (!getSettingsPath().empty()) {
    musicPreferenceFile = fs::path(getSettingsPath()).parent_path() /
                          "launcher-music.txt";
    musicEnabled = readLauncherMusicEnabled(musicPreferenceFile);
  }
  loadLxhPreference();
  refreshLxhAvailability();
}

Launcher::~Launcher() { close(); }

void Launcher::close() {
  titleMusic.reset();
  titleMusicBytes.clear();
  titleMusicSource.clear();
  musicAttempted = false;
  if (audioInitialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audioInitialized = false;
  }
  GameLauncher::close();
}

bool Launcher::statusNoticeExpired(std::uint64_t shownAt,
                                   std::uint64_t now) {
  return now >= shownAt && now - shownAt >= StatusNoticeMilliseconds;
}

void Launcher::toggleMusic() {
  musicEnabled = !musicEnabled;
  if (titleMusic && !musicEnabled) {
    try {
      titleMusic->setPaused(true);
    } catch (const std::exception &error) {
      titleMusic.reset();
      SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                  "Cannot pause launcher music: %s", error.what());
    }
  }
  if (musicEnabled && !titleMusic)
    musicAttempted = false; // retry a previously unavailable retail folder
  if (!musicPreferenceFile.empty()) {
    try {
      Kotonoha::writeTextFileAtomically(
          musicPreferenceFile.string(), musicEnabled ? "on\n" : "off\n");
    } catch (const std::exception &error) {
      SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                  "Cannot remember launcher music preference: %s", error.what());
    }
  }
}

void Launcher::drawMusicToggle(const ImVec2 &position) {
  ImGui::SetCursorScreenPos(position);
  // Use a dedicated high-contrast control. The old pale 119 px button in the
  // artwork's bright sky could look missing, even on a current build.
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
  ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.055f, 0.19f, 0.33f, 0.96f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.13f, 0.35f, 0.54f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.10f, 0.28f, 0.47f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  if (ImGui::Button(musicEnabled ? "Launcher music: ON##launcher" :
                                  "Launcher music: OFF##launcher",
                    {190.0f, 33.0f}))
    toggleMusic();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar();
}

void Launcher::updateTitleMusic() {
  if (!isOpen() || !musicEnabled || !pathValid() || path().empty()) {
    if (titleMusic) {
      try {
        titleMusic->setPaused(true);
      } catch (const std::exception &error) {
        titleMusic.reset();
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                    "Cannot pause launcher music: %s", error.what());
      }
    }
    return;
  }
  if (!musicAttempted || titleMusicSource != path()) {
    titleMusic.reset();
    titleMusicBytes.clear();
    titleMusicSource = path();
    musicAttempted = true;
    try {
      // The engine doesn't exist yet: the embedded launcher owns its audio
      // subsystem reference and releases it before entering gameplay.
      if (!audioInitialized) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
          throw std::runtime_error(SDL_GetError());
        audioInitialized = true;
      }
      {
        Assets assets(Kotonoha::gameDataDirectory(path()));
        Kotonoha::SchoolDaysEndBackgroundResolver::Profile profile;
        std::string error;
        if (!Kotonoha::SchoolDaysTitleProfile::Load(
                assets.root().string(), &profile, &error))
          throw std::runtime_error(error);
        titleMusicBytes = assets.read(
            assets.resolve(profile.titleBgmStem + "_loop.ogg"));
      }
      LauncherMusicBytes inMemory(titleMusicBytes);
      auto player = std::make_unique<Kotonoha::MediaPlayer>();
      player->setBufferSeconds(1.0);
      // Prepare paused, so a muted/zero-volume profile never leaks a short
      // audible burst between opening the stream and applying the gain.
      player->prepare(inMemory, kTitleMusicId, true, "BGM");
      titleMusic = std::move(player);
    } catch (const std::exception &error) {
      titleMusic.reset();
      titleMusicBytes.clear();
      SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                  "School Days launcher title BGM unavailable: %s", error.what());
    }
  }
  if (!titleMusic) return;
  try {
    // Decorative launcher audio belongs only to its ON/OFF preference.
    // Config.DAT Mute/BgmVolume and in-game master settings must not make a
    // visible "Launcher music: ON" control silently inaudible, or be changed
    // by that control. Use a comfortable fixed gain with a private mixer.
    constexpr float kLauncherMusicGain = 0.35f;
    titleMusic->getChannel()->setVolume(kLauncherMusicGain);
    titleMusic->setPaused(false);
    titleMusic->update(*launcherRenderer());
    if (titleMusic->finished()) {
      LauncherMusicBytes inMemory(titleMusicBytes);
      titleMusic->prepare(inMemory, kTitleMusicId, true, "BGM");
    }
  } catch (const std::exception &error) {
    titleMusic.reset();
    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                "School Days launcher title BGM stopped: %s", error.what());
  }
}

bool Launcher::initializeFirstRunDisplay(Kotonoha::EngineSettings &target,
                                         bool hasStoredSettings,
                                         int desktopWidth, int desktopHeight,
                                         float desktopHz) {
  return Kotonoha::SchoolDaysDesktopStartupProfile::Apply(
      target, hasStoredSettings, desktopWidth, desktopHeight, desktopHz);
}

bool Launcher::selectPhysicalResolution(
    Kotonoha::EngineSettings &target,
    const Kotonoha::DisplayModeChoice &choice,
    const SDL_DisplayMode *desktop) {
  if (!Kotonoha::DisplayModeCatalog::Select(target, choice, desktop)) return false;
  // A decorated/windowed app cannot change the monitor refresh. Persist
  // physical Hz only for fullscreen; do not promise a fictitious Hz switch.
  if (target.mode == Kotonoha::WindowMode::Windowed && desktop)
    target.refreshRate = desktop->refresh_rate;
  return true;
}

void Launcher::seedFirstRunDisplayIfPossible() {
#if defined(__ANDROID__) || defined(ANDROID)
  initialDisplayPending = false;
  return;
#endif
  if (!initialDisplayPending || !SDL_WasInit(SDL_INIT_VIDEO)) return;
  auto &draft = settings();
  // Another explicit caller may have seeded video before the first frame.
  // Never treat that as permission to overwrite their manual preference.
  if (draft.width != initialDisplayBaseline.width ||
      draft.height != initialDisplayBaseline.height ||
      draft.mode != initialDisplayBaseline.mode ||
      draft.display != initialDisplayBaseline.display ||
      draft.fullscreenWidth != initialDisplayBaseline.fullscreenWidth ||
      draft.fullscreenHeight != initialDisplayBaseline.fullscreenHeight ||
      draft.refreshRate != initialDisplayBaseline.refreshRate) {
    initialDisplayPending = false;
    return;
  }
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  if (!displays || count <= 0) {
    SDL_free(displays);
    return;
  }
  const int index = std::clamp(draft.display, 0, count - 1);
  const auto *desktop = SDL_GetDesktopDisplayMode(displays[index]);
  if (desktop &&
      initializeFirstRunDisplay(draft, false, desktop->w,
                                desktop->h, desktop->refresh_rate))
    initialDisplayPending = false;
  SDL_free(displays);
}

SDL_AppResult Launcher::iterate() {
  if (!isOpen()) open();
  seedFirstRunDisplayIfPossible();
  updateTitleMusic();
  return GameLauncher::iterate();
}

void Launcher::setRetailConfigRoot(std::filesystem::path documentsRoot) {
  if (documentsRoot.empty())
    throw std::invalid_argument("Retail Config.DAT needs an explicit documents root");
  if (retailConfigReady)
    throw std::logic_error("Retail Config.DAT root cannot be changed mid-launch");
  retailRoot = fs::absolute(documentsRoot).lexically_normal();
  std::string error;
  if (!retailStorage.Initialize(retailRoot, &error))
    throw std::runtime_error("School Days Config.DAT init: " + error);
  Kotonoha::SchoolDaysConfigStorage::LoadResult loaded;
  if (!retailStorage.Load(&loaded, &error))
    throw std::runtime_error("School Days Config.DAT load: " + error);
  retailFileExisted = loaded.present;
  if (retailFileExisted) {
    std::error_code ec;
    retailFileTimestamp = fs::last_write_time(retailStorage.Path(), ec);
    if (ec) throw std::runtime_error("Cannot stat retail Config.DAT timestamp");
    retailFileSize = fs::file_size(retailStorage.Path(), ec);
    if (ec) throw std::runtime_error("Cannot stat retail Config.DAT size");
  }
  retailDraft = loaded.state;
  aspectPreference = readAspectPreference(retailRoot);
  loadedAspectPreference = aspectPreference;
  retailConfigReady = true;
  loadLxhPreference();
  refreshLxhAvailability();
  // Runtime's retail mixer curve is authoritative for BGM/SE/Voice, NOT the
  // generic slider defaults in a previously saved settings.json. Movie and
  // master volume remain independent generic EngineSettings values.
  syncMixerFromRetail();
}

void Launcher::syncMixerFromRetail() {
  using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
  settings().masterMuted = retailDraft.mute;
  settings().channelVolumes["BGM"] =
      Policy::LinearGain(retailDraft, Policy::NativeAudioBank::Bgm);
  settings().channelVolumes["SE"] =
      Policy::LinearGain(retailDraft, Policy::NativeAudioBank::Se);
  settings().channelVolumes["Voice"] =
      Policy::LinearGain(retailDraft, Policy::NativeAudioBank::Voice);
}

void Launcher::syncRetailFromMixer() {
  if (!retailConfigReady) return;
  using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
  auto &draft = settings();
  // Generic Restore defaults resets the entire EngineSettings draft. Give
  // retail General/Sound the corresponding fresh defaults too.
  if (draft.channelVolumes.find("BGM") == draft.channelVolumes.end() &&
      draft.channelVolumes.find("SE") == draft.channelVolumes.end() &&
      draft.channelVolumes.find("Voice") == draft.channelVolumes.end()) {
    retailDraft = Policy::Defaults();
    syncMixerFromRetail();
  }
  for (const auto &[channel, bank] : {
           std::pair<const char *, Policy::NativeAudioBank>{
               "BGM", Policy::NativeAudioBank::Bgm},
           {"SE", Policy::NativeAudioBank::Se},
           {"Voice", Policy::NativeAudioBank::Voice}}) {
    if (const auto it = draft.channelVolumes.find(channel);
        it != draft.channelVolumes.end() && std::isfinite(it->second)) {
      Policy::SetLevel(&retailDraft, bank,
                       nearestRetailVolume(retailDraft, bank, it->second));
    }
  }
  retailDraft.mute = draft.masterMuted;
  // Write canonical retail gains to the same JSON that the Engine loads,
  // avoiding a startup mixer discrepancy after quantizing generic sliders.
  syncMixerFromRetail();
}

void Launcher::setRetailSettings(
    const Kotonoha::SchoolDaysRetailConfigPolicy::State &value) {
  if (!retailConfigReady)
    throw std::logic_error("Set retail Config.DAT root before editing Sound");
  if (!Kotonoha::SchoolDaysRetailConfigPolicy::Validate(value))
    throw std::invalid_argument("Invalid retail General/Sound options");
  retailDraft = value;
  syncMixerFromRetail();
}

void Launcher::setContentAspectPreference(int value) {
  if (!retailConfigReady)
    throw std::logic_error("Set retail Config.DAT root before editing aspect");
  if (value < 0 || value > 2)
    throw std::invalid_argument("Content aspect must be Auto, 4:3 or 16:9");
  aspectPreference = value;
}

void Launcher::checkRetailFileUnchanged() const {
  if (!retailConfigReady) return;
  const auto path = retailStorage.Path();
  std::error_code ec;
  const bool current = fs::exists(path, ec);
  if (ec || current != retailFileExisted)
    throw std::runtime_error(
        "Config.DAT changed while launcher was open; close and reopen");
  if (current) {
    if (!fs::is_regular_file(path, ec) || ec)
      throw std::runtime_error("Config.DAT is no longer a regular file");
    const auto timestamp = fs::last_write_time(path, ec);
    if (ec || !retailFileTimestamp || timestamp != *retailFileTimestamp)
      throw std::runtime_error(
          "Config.DAT was modified externally; refusing to overwrite");
    const auto size = fs::file_size(path, ec);
    if (ec || !retailFileSize || size != *retailFileSize)
      throw std::runtime_error(
          "Config.DAT size changed externally; refusing to overwrite");
  }
}

void Launcher::saveAspectPreference() {
  if (!retailConfigReady || aspectPreference == loadedAspectPreference) return;
  // The same Kotonoha.ini key is read/written by in-game Display Options;
  // never add a second aspect preference or place it in settings.json.
  if (readAspectPreference(retailRoot) != loadedAspectPreference)
    throw std::runtime_error(
        "Narrative aspect changed in another session; close and reopen launcher");
  writeAspectPreference(retailRoot, aspectPreference);
  loadedAspectPreference = aspectPreference;
}

std::filesystem::path Launcher::lxhPreferenceRoot() const {
  if (retailConfigReady && !retailRoot.empty())
    return retailRoot;
  if (!getSettingsPath().empty())
    return fs::path(getSettingsPath()).parent_path();
  return {};
}

void Launcher::loadLxhPreference() {
  const auto root = lxhPreferenceRoot();
  savedEnableLxh = Kotonoha::SchoolDaysLxhPreference::Read(root);
  if (savedEnableLxh.has_value()) {
    enableLxhDraft = *savedEnableLxh;
  } else {
    enableLxhDraft = lxhPackAvailability.packValid;
  }
  enableLxhModified = false;
}

void Launcher::refreshLxhAvailability() {
  const std::string currentPath = path();
  if (currentPath == probedLxhPath && !currentPath.empty())
    return;
  probedLxhPath = currentPath;
  if (currentPath.empty()) {
    lxhPackAvailability = {};
    lxhPackAvailability.reason = "Game folder is not selected";
  } else {
    try {
      lxhPackAvailability = Kotonoha::SchoolDaysLxhPreference::ProbePack(
          Kotonoha::gameDataDirectory(currentPath));
    } catch (const std::exception &error) {
      lxhPackAvailability = {};
      lxhPackAvailability.reason = error.what();
    }
  }
  // Never overwrite an explicit persisted or edited choice with auto-detection.
  if (!savedEnableLxh.has_value() && !enableLxhModified) {
    enableLxhDraft = lxhPackAvailability.packValid;
  }
  const auto resolved = resolveCurrentLxhState();
  effectiveLxh = resolved.effectiveEnabled;
  lxhNotice = resolved.statusReason;
}

void Launcher::saveLxhPreference() {
  if (!enableLxhModified)
    return;
  const auto root = lxhPreferenceRoot();
  if (root.empty())
    return;
  Kotonoha::SchoolDaysLxhPreference::Write(root, enableLxhDraft);
  savedEnableLxh = enableLxhDraft;
  enableLxhModified = false;
}

bool Launcher::enableLxhPreference() const {
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  return false;
#else
  if (enableLxhModified)
    return enableLxhDraft;
  if (savedEnableLxh.has_value())
    return *savedEnableLxh;
  return lxhPackAvailability.packValid;
#endif
}

void Launcher::setEnableLxhPreference(bool enabled) {
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  (void)enabled;
  enableLxhDraft = false;
  enableLxhModified = false;
#else
  enableLxhDraft = enabled;
  enableLxhModified = true;
  savedEnableLxh = enabled;
#endif
  const auto resolved = resolveCurrentLxhState();
  effectiveLxh = resolved.effectiveEnabled;
  lxhNotice = resolved.statusReason;
}

Kotonoha::SchoolDaysLxhPreference::Resolution
Launcher::resolveCurrentLxhState(bool *outTranslationReady,
                                 std::string *outTranslationReason) const {
  bool transReady = true;
  std::string transReason;
  const auto selected = language();
  if (isGpkLanguage(selected)) {
    const auto locale = gpkLocale(selected);
    if (!getSettingsPath().empty()) {
      const auto epilogue =
          Kotonoha::SchoolDaysGpkLanguageProfile::LxhPath(getSettingsPath(),
                                                          locale);
      std::error_code ec;
      if (!fs::is_regular_file(epilogue, ec) || ec) {
        transReady = false;
        transReason = "missing " + epilogue.filename().string() +
                      " in translations/scripts";
      }
    }
  } else {
    const auto &baseJson = currentSettings().translationPath;
    const auto locale = Kotonoha::SchoolDaysTranslationGate::LocaleId(baseJson);
    const auto lxh = lxhJsonPath();
    if (lxh.empty() && locale != "es") {
      transReady = false;
      transReason = "missing " + locale + "-lxh.json";
    }
  }
  if (outTranslationReady)
    *outTranslationReady = transReady;
  if (outTranslationReason)
    *outTranslationReason = transReason;
  const std::optional<bool> pref =
      enableLxhModified ? std::optional<bool>(enableLxhDraft) : savedEnableLxh;
  return Kotonoha::SchoolDaysLxhPreference::Resolve(
      pref, lxhPackAvailability, transReady, transReason);
}

std::string Launcher::rememberedSpanishJsonPath(
    const std::string &settingsPath) {
  if (settingsPath.empty()) return {};
  const auto file = fs::path(settingsPath).parent_path() / "language.txt";
  const auto saved = readLanguagePreference(file);
  return saved.name == "spanish"
             ? resolveInstalledJson(settingsPath, saved.jsonPath)
             : std::string{};
}

std::string Launcher::rememberedLocaleJsonPath(
    const std::string &settingsPath) {
  if (settingsPath.empty()) return {};
  const auto saved = readLanguagePreference(
      fs::path(settingsPath).parent_path() / "language.txt");
  return jsonPreferenceSelected(saved.name)
             ? resolveInstalledJson(settingsPath, saved.jsonPath) : std::string{};
}

bool Launcher::nonNativeLanguagePreferenceSelected(
    const std::string &settingsPath) {
  if (settingsPath.empty()) return false;
  const auto name = readLanguagePreference(
      fs::path(settingsPath).parent_path() / "language.txt").name;
  return jsonPreferenceSelected(name);
}

std::string Launcher::rememberedLxhJsonPath(
    const std::string &settingsPath, const std::string &baseJsonPath) {
  if (settingsPath.empty()) return {};
  const auto saved = readLanguagePreference(
      fs::path(settingsPath).parent_path() / "language.txt");
  if (saved.lxhJsonPath.empty()) return {};
  // A previously selected sidecar belongs only to its matching base JSON.
  // Switching the command-line/base dictionary must never silently attach
  // another language's epilogue. Empty base matches native English only.
  if (saved.jsonPath.empty() || baseJsonPath.empty())
    return saved.jsonPath.empty() && baseJsonPath.empty()
               ? resolveInstalledJson(settingsPath, saved.lxhJsonPath)
               : std::string{};
  const auto oldBase = fs::path(saved.jsonPath).lexically_normal();
  const auto chosen = fs::path(baseJsonPath).lexically_normal();
  const auto isInstalled = [&](const fs::path &file) {
    if (file.empty()) return false;
    if (!file.is_absolute()) return true; // Generic settings use basenames.
    const auto root = fs::path(settingsPath).parent_path() / "translations";
    const auto relative = file.lexically_relative(root);
    return !relative.empty() && relative != "." &&
           *relative.begin() != "..";
  };
  if (oldBase == chosen ||
      (oldBase.filename() == chosen.filename() &&
       isInstalled(oldBase) && isInstalled(chosen)))
    return resolveInstalledJson(settingsPath, saved.lxhJsonPath);
  return {};
}

bool Launcher::spanishPreferenceSelected(const std::string &settingsPath) {
  if (settingsPath.empty()) return false;
  return readLanguagePreference(
      fs::path(settingsPath).parent_path() / "language.txt").name == "spanish";
}

std::string Launcher::resolveStoredTranslation(
    const std::string &settingsPath, const std::string &stored) {
  if (stored.empty()) return stored;
  const auto resolved = resolveInstalledJson(settingsPath, stored);
  if (regularTranslationFile(resolved)) return resolved;
  const auto remembered = rememberedLocaleJsonPath(settingsPath);
  if (!remembered.empty() &&
      fs::path(remembered).filename() == fs::path(stored).filename() &&
      regularTranslationFile(remembered))
    return remembered;
  if (!settingsPath.empty()) {
    const auto folder = fs::path(settingsPath).parent_path();
    for (const auto &candidate : {folder / stored,
                                  folder / "translations" / stored,
                                  folder / "translations" / "jsons" /
                                      fs::path(stored).filename()})
      if (regularTranslationFile(candidate.string()))
        return fs::absolute(candidate).string();
  }
  return stored;
}

bool Launcher::persistShaderPaths(const std::string &settingsPath,
                                  const std::string &shaderFiles,
                                  std::string *error) {
  if (error) error->clear();
  if (settingsPath.empty()) return true;
  try {
    std::string text = std::string(kShaderSidecarHeader) + "\n";
    for (const auto &shader : Kotonoha::UpscaleShaderFiles(shaderFiles)) {
      if (shader.empty() || shader.find('\r') != std::string::npos ||
          shader.find('\n') != std::string::npos ||
          shader.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid custom GLSL path");
      text += shader + "\n";
      if (text.size() > 131072u)
        throw std::invalid_argument("Too many custom GLSL paths");
    }
    Kotonoha::writeTextFileAtomically(shaderSidecarPath(settingsPath).string(),
                                      text);
    return true;
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
}

std::string Launcher::resolveStoredShaderPaths(
    const std::string &settingsPath, const std::string &stored) {
  if (settingsPath.empty() || stored.empty()) return stored;
  const auto path = shaderSidecarPath(settingsPath);
  const auto fallback = [&](const char *reason) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "School Days custom GLSL sidecar ignored (%s); "
                "retaining safe portable settings.json references",
                reason);
    return stored;
  };
  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || ec) return stored;
  if (fs::file_size(path, ec) > 131072u || ec)
    return fallback("oversized/unreadable");
  std::ifstream input(path, std::ios::binary);
  if (!input) return fallback("cannot read");
  std::string header;
  std::getline(input, header);
  if (header != kShaderSidecarHeader)
    return fallback("unknown version");
  std::vector<std::string> originals;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.find('\r') != std::string::npos)
      return fallback("invalid path entry");
    originals.push_back(line);
  }
  const auto portable = Kotonoha::UpscaleShaderFiles(stored);
  if (originals.size() != portable.size())
    return fallback("entry count does not match settings.json");
  for (std::size_t index = 0; index < originals.size(); ++index) {
    const auto filename = fs::path(originals[index]).filename().string();
    if (filename != fs::path(portable[index]).filename().string())
      return fallback("basename differs from settings.json");
    // Do not replace a resolvable portable preset with a dead absolute path.
    if (fs::path(originals[index]).is_absolute()) {
      if (!fs::is_regular_file(originals[index], ec) || ec)
        return fallback("external shader is unavailable");
    }
  }
  return Kotonoha::UpscaleShaderFileList(originals);
}

bool Launcher::validateRoute(const std::string &file, std::string &info) {
  Kotonoha::SchoolDaysKtrfSession session;
  Kotonoha_KtrfError error{};
  if (!session.Open(file.c_str(), ".", &error)) {
    info = std::string("Invalid School Days route: ") + error.message;
    return false;
  }
  info = "School Days route is valid";
  return true;
}

bool Launcher::checkPath(const std::string &path, std::string &info) {
  return Assets::checkDirectory(path, info);
}

const std::string &Launcher::routePath() const {
  return optionPath(routeOption);
}

void Launcher::setRoutePath(std::string path) {
  setOptionPath(routeOption, std::move(path));
}

bool Launcher::routeValid() const { return optionPathValid(routeOption); }

const std::string &Launcher::routeInfo() const {
  return optionPathInfo(routeOption);
}

Launcher::Language Launcher::language() const {
  const auto &path = currentSettings().translationPath;
  if (isGpkLanguage(selectedLanguage) && path.empty())
    return selectedLanguage;
  if (selectedLanguage == Language::English && path.empty())
    return Language::English;
  if (jsonPresetForLanguage(selectedLanguage) &&
      path == selectedLocaleBaseJsonPath)
    return selectedLanguage;
  return Language::CustomJson;
}

void Launcher::setLanguage(Language selection) {
  if (selection == language()) return;
  // Manual LxH overrides must never leak from one base language to another.
  selectedManualLxhJsonPath.clear();
  manualLxhBaseJsonPath.clear();
  if (selection == Language::English || isGpkLanguage(selection)) {
    settings().translationPath.clear();
  } else if (jsonPresetForLanguage(selection)) {
    // Only match this explicit locale. Missing JSON must block Play; a
    // different installed language must never become the fallback.
    settings().translationPath = findInstalledJson(selection);
  }
  selectedLocaleBaseJsonPath = settings().translationPath;
  selectedLanguage = selection;
}

std::string Launcher::rememberedGpkLocale(const std::string &settingsPath) {
  return Kotonoha::SchoolDaysGpkLanguageProfile::Read(settingsPath);
}

void Launcher::setSpanishJsonPath(std::string path) {
  setCustomJsonPath(std::move(path));
  selectedLocaleBaseJsonPath = settings().translationPath;
  selectedLanguage = Language::Spanish;
}

void Launcher::setChineseJsonPath(std::string path) {
  setCustomJsonPath(std::move(path));
  selectedLocaleBaseJsonPath = settings().translationPath;
  selectedLanguage = Language::ChineseSimplified;
}

void Launcher::setCustomJsonPath(std::string path) {
  if (settings().translationPath != path) {
    selectedManualLxhJsonPath.clear();
    manualLxhBaseJsonPath.clear();
  }
  settings().translationPath = std::move(path);
  selectedLocaleBaseJsonPath = settings().translationPath;
  selectedLanguage = Language::CustomJson;
}

void Launcher::setLxhJsonPath(std::string path) {
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  (void)path;
#else
  selectedManualLxhJsonPath = std::move(path);
  manualLxhBaseJsonPath = settings().translationPath;
#endif
}

std::string Launcher::manualLxhJsonPath() const {
  return manualLxhBaseJsonPath == currentSettings().translationPath
             ? selectedManualLxhJsonPath : std::string{};
}

std::string Launcher::lxhJsonPath() const {
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  return {};
#else
  if (auto manual = manualLxhJsonPath(); !manual.empty()) return manual;
  try {
    const auto &base = translationPath();
    return Kotonoha::SchoolDaysTranslationGate::DlcJsonPath(
        Kotonoha::SchoolDaysTranslationGate::LocaleId(base), base);
  } catch (const std::exception &) {
    // Invalid base paths are explained by validate(), not an ImGui render.
    return {};
  }
#endif
}

const std::string &Launcher::translationPath() const {
  return currentSettings().translationPath;
}

const Kotonoha::EngineSettings &Launcher::currentSettings() const {
  return const_cast<Launcher *>(this)->settings();
}

void Launcher::setScriptOverlayPath(std::string path) {
  scriptOverlayPath = std::move(path);
}

std::filesystem::path Launcher::effectiveOverlayRoot() const {
  const fs::path requested = !scriptOverlayPath.empty()
                                 ? fs::path(scriptOverlayPath)
                                 : fs::path(currentSettings().assetOverlayPath);
  fs::path executableRoot;
#if defined(_WIN32) && !defined(__ANDROID__)
  if (const char *base = SDL_GetBasePath(); base && *base)
    executableRoot = base;
#endif
  return OverlaySelection::Select(executableRoot, requested);
}

std::string Launcher::findSpanishJson() const {
  // Only installed JSON files are offered; no language is inferred from a GPK
  // or from unrelated dictionaries the user selected in the generic tab.
  constexpr std::array<const char *, 9> names{
      "translations/jsons/es.json", "translations/jsons/es-ES.json",
      "translations/jsons/spanish.json", "translations/jsons/school-days-es.json",
      "translations/es.json", "translations/es-ES.json",
      "translations/spanish.json", "translations/school-days-es.json",
      "translation/es.json"};
  for (const char *name : names) {
    if (auto path = Kotonoha::FileResolver::findGameAsset("school-days", name);
        path && regularTranslationFile(path->string()))
      return path->string();
    if (!getSettingsPath().empty()) {
      const auto local = fs::path(getSettingsPath()).parent_path() / name;
      if (regularTranslationFile(local.string())) return local.string();
    }
  }
  return {};
}

std::string Launcher::findChineseJson() const {
  // Prefer Simplified Chinese. Preserve the installed file's real casing so
  // non-Windows filesystems can resolve zh-CN.json / zh-CN-lxh.json as a pair.
  constexpr std::array<const char *, 9> names{
      "translations/jsons/zh-CN.json", "translations/jsons/zh-cn.json",
      "translations/jsons/zh-Hans.json", "translations/jsons/chinese.json",
      "translations/zh-CN.json", "translations/zh-cn.json",
      "translations/zh-Hans.json", "translations/chinese.json",
      "translation/zh-CN.json"};
  for (const char *name : names) {
    if (auto path = Kotonoha::FileResolver::findGameAsset("school-days", name);
        path && regularTranslationFile(path->string()))
      return path->string();
    if (!getSettingsPath().empty()) {
      const auto local = fs::path(getSettingsPath()).parent_path() / name;
      if (regularTranslationFile(local.string())) return local.string();
    }
  }
  return {};
}

std::string Launcher::findInstalledJson(Language selection) const {
  const auto *preset = jsonPresetForLanguage(selection);
  if (!preset) return {};
  if (!getSettingsPath().empty()) {
    const auto path = Kotonoha::SchoolDaysGpkLanguageProfile::JsonPath(
        getSettingsPath(), preset->filename);
    if (regularTranslationFile(path.string()))
      return fs::absolute(path).string();
  }
  // Preserve previous Spanish/Chinese aliases only if the exact organized
  // profile is absent. Never prefer a global resolver over this installation.
  if (selection == Language::Spanish) return findSpanishJson();
  if (selection == Language::ChineseSimplified) return findChineseJson();
  return {};
}

void Launcher::drawSharedDisplayOptions() {
  drawPhysicalDisplayOptions();
  drawGraphicsOptions();
}

void Launcher::drawPhysicalDisplayOptions() {
  auto &draft = settings();
  ImGui::Spacing();
  ImGui::SeparatorText("Display Options (same settings as in-game)");
#if defined(__ANDROID__) || defined(ANDROID)
  ImGui::TextWrapped("Android uses a fullscreen Surface controlled by the "
                     "device. Windowed, exclusive fullscreen and monitor "
                     "switching do not apply on phones.");
  draft.mode = Kotonoha::WindowMode::Borderless;
  draft.display = 0;
  const auto modes = Kotonoha::AndroidEnumerateDisplayModes();
  const int currentId = Kotonoha::AndroidCurrentDisplayModeId();
  std::string current = "Device default";
  for (const auto &mode : modes) {
    const bool matches = draft.fullscreenWidth == mode.width &&
        draft.fullscreenHeight == mode.height &&
        std::abs(draft.refreshRate - mode.refreshRate) < 0.05f;
    if (matches || (draft.fullscreenWidth == 0 && mode.modeId == currentId)) {
      current = std::to_string(mode.width) + " x " +
          std::to_string(mode.height) + " @ " +
          std::to_string(static_cast<int>(std::lround(mode.refreshRate))) + " Hz";
      break;
    }
  }
  if (ImGui::BeginCombo("Native resolution / refresh##sd", current.c_str())) {
    if (ImGui::Selectable("Device default", draft.fullscreenWidth == 0)) {
      draft.fullscreenWidth = 0;
      draft.fullscreenHeight = 0;
      draft.refreshRate = 0;
    }
    for (const auto &mode : modes) {
      const std::string label = std::to_string(mode.width) + " x " +
          std::to_string(mode.height) + " @ " +
          std::to_string(static_cast<int>(std::lround(mode.refreshRate))) + " Hz";
      const bool selected = draft.fullscreenWidth == mode.width &&
          draft.fullscreenHeight == mode.height &&
          std::abs(draft.refreshRate - mode.refreshRate) < 0.05f;
      if (ImGui::Selectable(label.c_str(), selected)) {
        draft.fullscreenWidth = mode.width;
        draft.fullscreenHeight = mode.height;
        draft.refreshRate = mode.refreshRate;
      }
    }
    ImGui::EndCombo();
  }
  if (modes.empty()) ImGui::TextDisabled("The device did not publish display modes.");
  ImGui::Checkbox("VSync##sd", &draft.vsync);
  ImGui::TextDisabled("Render resolution is adjusted separately in Graphics.");
#else
  ImGui::TextDisabled(
      "The selected monitor, mode, refresh, upscaler and renderer are saved "
      "in settings.json and used by in-game Display Options.");

  int selectedMode = static_cast<int>(draft.mode);
  const char *windowModes[]{"Windowed", "Borderless fullscreen",
                            "Exclusive fullscreen"};
  if (ImGui::Combo("Window mode##sd", &selectedMode, windowModes, 3))
    draft.mode = static_cast<Kotonoha::WindowMode>(selectedMode);

  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  const SDL_DisplayID currentDisplay =
      displays && draft.display >= 0 && draft.display < count
          ? displays[draft.display] : SDL_DisplayID{};
  const SDL_DisplayMode *desktop =
      currentDisplay ? SDL_GetDesktopDisplayMode(currentDisplay) : nullptr;
  const char *monitorName =
      currentDisplay ? SDL_GetDisplayName(currentDisplay) : nullptr;
  const std::string currentMonitor =
      "Display " + std::to_string(draft.display + 1) + ": " +
      (monitorName ? monitorName : "Unavailable");
  if (ImGui::BeginCombo("Monitor##sd", currentMonitor.c_str())) {
    for (int index = 0; displays && index < count; ++index) {
      const char *name = SDL_GetDisplayName(displays[index]);
      const std::string label = std::to_string(index + 1) + ": " +
                                (name ? name : "Monitor");
      if (ImGui::Selectable(label.c_str(), index == draft.display)) {
        draft.display = index;
        // An explicit monitor switch selects *that monitor's* desktop
        // baseline, never the stale dimensions of the previous monitor.
        if (const auto *native = SDL_GetDesktopDisplayMode(displays[index])) {
          if (draft.mode == Kotonoha::WindowMode::Windowed) {
            draft.width = native->w;
            draft.height = native->h;
          }
          draft.fullscreenWidth = native->w;
          draft.fullscreenHeight = native->h;
          draft.refreshRate = native->refresh_rate;
        }
      }
    }
    ImGui::EndCombo();
  }
  // Refresh after an explicit monitor change inside the same frame.
  const SDL_DisplayID selectedDisplay =
      displays && draft.display >= 0 && draft.display < count
          ? displays[draft.display] : SDL_DisplayID{};
  const auto *selectedDesktop =
      selectedDisplay ? SDL_GetDesktopDisplayMode(selectedDisplay) : nullptr;
  if (selectedDesktop && draft.mode == Kotonoha::WindowMode::Borderless &&
      (draft.fullscreenWidth != selectedDesktop->w ||
       draft.fullscreenHeight != selectedDesktop->h)) {
    ImGui::TextDisabled(
        "Borderless uses the current desktop mode; choosing a different "
        "resolution below requests Exclusive fullscreen.");
  }
  int actualWidth = draft.width;
  int actualHeight = draft.height;
  float shownHz = draft.refreshRate;
  if (draft.mode == Kotonoha::WindowMode::Borderless && selectedDesktop) {
    actualWidth = selectedDesktop->w;
    actualHeight = selectedDesktop->h;
    shownHz = selectedDesktop->refresh_rate;
  } else if (draft.mode != Kotonoha::WindowMode::Windowed) {
    if (draft.fullscreenWidth > 0) actualWidth = draft.fullscreenWidth;
    if (draft.fullscreenHeight > 0) actualHeight = draft.fullscreenHeight;
  } else if (selectedDesktop) {
    shownHz = selectedDesktop->refresh_rate;
  }
  const std::string currentResolution =
      Kotonoha::DisplayModeCatalog::Label(
          {actualWidth, actualHeight, shownHz}) +
      (draft.mode == Kotonoha::WindowMode::Borderless ? " (desktop)"
          : draft.mode == Kotonoha::WindowMode::Windowed ? " (window)" : "");
  const auto catalog = Kotonoha::DisplayModeCatalog::Enumerate(selectedDisplay);
  if (ImGui::BeginCombo("Resolution / refresh##sd",
                        currentResolution.c_str())) {
    for (const auto &choice : catalog) {
      const bool selected = choice.width == actualWidth &&
                            choice.height == actualHeight &&
                            std::abs(choice.refreshRate - shownHz) < 0.02f;
      const std::string label = Kotonoha::DisplayModeCatalog::Label(choice);
      if (ImGui::Selectable(label.c_str(), selected))
        (void)selectPhysicalResolution(draft, choice, selectedDesktop);
    }
    ImGui::EndCombo();
  }
  if (catalog.empty())
    ImGui::TextDisabled("No display modes were returned for this monitor.");
  if (draft.mode == Kotonoha::WindowMode::Windowed) {
    int size[]{draft.width, draft.height};
    if (ImGui::InputInt2("Custom window size##sd", size)) {
      draft.width = std::clamp(size[0], 320, 16384);
      draft.height = std::clamp(size[1], 240, 16384);
    }
  }
  SDL_free(displays);

  ImGui::Checkbox("VSync##sd", &draft.vsync);
  if (draft.mode == Kotonoha::WindowMode::Windowed) {
    ImGui::SameLine();
    ImGui::Checkbox("Resizable window##sd", &draft.resizable);
  }
#endif
}

void Launcher::drawGraphicsOptions() {
  auto &draft = settings();
  ImGui::Spacing();
  ImGui::SeparatorText("Renderer and upscaling");
  ImGui::SliderFloat("Render scale##sd", &draft.renderScale, 0.25f, 1.0f,
                     "%.2fx");
  const std::string renderer =
      draft.renderer.empty() ? "Automatic" : draft.renderer;
#if defined(__ANDROID__) || defined(ANDROID)
  // SDL/Android exposes the device's GLES Surface; desktop renderer backends
  // and an arbitrary Windows D3D/Metal renderer are not valid choices here.
  draft.renderer = "opengles2";
  ImGui::TextDisabled("Renderer: OpenGL ES (device-native)");
#else
  if (ImGui::BeginCombo("Renderer##sd", renderer.c_str())) {
    if (ImGui::Selectable("Automatic", draft.renderer.empty()))
      Kotonoha::SchoolDaysUpscalingPolicy::SelectRenderer(draft, {});
    for (int index = 0; index < SDL_GetNumRenderDrivers(); ++index) {
      const char *driver = SDL_GetRenderDriver(index);
      if (driver && ImGui::Selectable(driver, draft.renderer == driver))
        Kotonoha::SchoolDaysUpscalingPolicy::SelectRenderer(draft, driver);
    }
    ImGui::EndCombo();
  }
#endif
  if (ImGui::BeginCombo("Upscaler##sd",
                        Kotonoha::UpscaleBackendName(draft.upscaling.backend))) {
    for (const auto backend : {
             Kotonoha::UpscaleBackend::Auto,
             Kotonoha::UpscaleBackend::None,
             Kotonoha::UpscaleBackend::Linear,
             Kotonoha::UpscaleBackend::MPV,
             Kotonoha::UpscaleBackend::MetalFX}) {
#if !defined(__APPLE__)
      if (backend == Kotonoha::UpscaleBackend::MetalFX) continue;
#endif
      if (ImGui::Selectable(Kotonoha::UpscaleBackendName(backend),
                            draft.upscaling.backend == backend)) {
        draft.upscaling.backend = backend;
        if (backend == Kotonoha::UpscaleBackend::MPV)
          draft.renderer = Kotonoha::isMobilePlatform() ? "opengles2" : "opengl";
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SliderFloat("Upscale factor##sd", &draft.upscaling.factor,
                     1.0f, 4.0f, "%.2fx");
  if (draft.upscaling.factor <= 1.0f)
    ImGui::TextDisabled("Upscaling is OFF at 1.0x, regardless of preset.");
  const char *presetLabel = draft.upscaling.shaderFiles.empty()
                                ? Kotonoha::UpscalePresetName(draft.upscaling.preset)
                                : "Custom GLSL (external file)";
  if (ImGui::BeginCombo("Shader preset##sd", presetLabel)) {
    namespace Catalog = Kotonoha::SchoolDaysShaderPresetCatalog;
    for (const auto tier : Catalog::Tiers) {
      ImGui::SetNextItemOpen(Catalog::Group(draft.upscaling.preset) == tier,
                             ImGuiCond_Once);
      if (!ImGui::TreeNode(Catalog::Name(tier))) continue;
      for (const auto preset : Kotonoha::UpscalePresets()) {
        if (Catalog::Group(preset) != tier) continue;
        if (ImGui::Selectable(Kotonoha::UpscalePresetName(preset),
                              draft.upscaling.shaderFiles.empty() &&
                                  draft.upscaling.preset == preset)) {
          // A preset replaces any custom override; otherwise the selected
          // builtin would be shown but the external GLSL would keep running.
          draft.upscaling.shaderFiles.clear();
          Kotonoha::SchoolDaysUpscalingPolicy::SelectPreset(draft, preset);
#if defined(__ANDROID__) || defined(ANDROID)
          draft.renderer = "opengles2";
#endif
        }
      }
      ImGui::TreePop();
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("Choosing a shader preset selects OpenGL + MPV and >=2.0x.");
  if (!draft.renderer.empty() &&
      !Kotonoha::SchoolDaysUpscalingPolicy::IsOpenGLRenderer(draft.renderer)) {
    ImGui::TextWrapped("%s uses Linear with Auto: Anime4K/ArtCNN GLSL "
                       "presets are NOT active. Select a shader preset to "
                       "switch to OpenGL + MPV, or keep this renderer for "
                       "Linear scaling.", draft.renderer.c_str());
  } else if (draft.renderer.empty()) {
    ImGui::TextWrapped("Automatic renderer: the active upscaler depends on "
                       "the driver SDL chooses. GLSL requires OpenGL + MPV.");
  }
  if (ImGui::InputText("Custom GLSL paths##sd", &draft.upscaling.shaderFiles) &&
      !draft.upscaling.shaderFiles.empty()) {
#if defined(__ANDROID__) || defined(ANDROID)
    draft.renderer = "opengles2";
#else
    draft.renderer = "opengl";
#endif
    draft.upscaling.backend = Kotonoha::UpscaleBackend::MPV;
    draft.upscaling.factor = std::max(2.0f, draft.upscaling.factor);
  }
  ImGui::TextDisabled("Enter a .glsl / .hook / .frag FILE, not its folder.");
  ImGui::TextDisabled("FSRCNNX x2 needs >1.4x; x3 >2.3x; x4 >3.3x.");
  for (const auto &file : Kotonoha::UpscaleShaderFiles(draft.upscaling.shaderFiles)) {
    if (!Kotonoha::SchoolDaysShaderPresetCatalog::SupportedShaderFile(file)) {
      ImGui::TextColored({0.72f, 0.18f, 0.14f, 1.0f},
                         "A folder name is not a shader. Select the .glsl FILE.");
      break;
    }
  }
  if (!draft.upscaling.shaderFiles.empty() &&
      !Kotonoha::SchoolDaysUpscalingPolicy::UsesShaders(draft))
    ImGui::TextWrapped("Custom GLSL paths are saved but inactive with this "
                       "renderer/backend or 1.0x factor.");
}

void Launcher::drawRetailGeneralSound() {
  drawRetailAudioOptions();
  drawRetailGeneralOptions();
}

void Launcher::drawRetailAudioOptions() {
  ImGui::Spacing();
  ImGui::SeparatorText("Audio (retail Config.DAT)");
  if (!retailConfigReady) {
    ImGui::TextDisabled(
        "The game host has not provided a Config.DAT profile; retail options "
        "are unavailable to avoid modifying another player's saves.");
    return;
  }
  using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
  // Both the Advanced tab and this retail page edit the same engine draft.
  // Reproject changed generic sliders into the authenticated 0..10 levels.
  const bool genericRestoredDefaults =
      settings().channelVolumes.find("BGM") == settings().channelVolumes.end() &&
      settings().channelVolumes.find("SE") == settings().channelVolumes.end() &&
      settings().channelVolumes.find("Voice") == settings().channelVolumes.end();
  syncRetailFromMixer();
  if (genericRestoredDefaults) aspectPreference = 0;
  const auto drawVolume = [&](const char *label, Policy::NativeAudioBank bank) {
    int level = Policy::Level(retailDraft, bank);
    if (ImGui::SliderInt(label, &level, 0, 10)) {
      Policy::SetLevel(&retailDraft, bank, level);
      syncMixerFromRetail();
    }
  };
  drawVolume("BGM volume##sd", Policy::NativeAudioBank::Bgm);
  drawVolume("SE volume##sd", Policy::NativeAudioBank::Se);
  drawVolume("Voice volume##sd", Policy::NativeAudioBank::Voice);
  ImGui::Checkbox("Male voices##sd", &retailDraft.menVoice);
  if (ImGui::Checkbox("Mute game audio##sd", &retailDraft.mute))
    syncMixerFromRetail();
  ImGui::SliderFloat("Master volume##sd", &settings().masterVolume,
                     0.0f, 1.0f, "%.2f");
  auto [movie, inserted] =
      settings().channelVolumes.try_emplace("Movie", 1.0f);
  (void)inserted;
  ImGui::SliderFloat("Movie volume##sd", &movie->second, 0.0f, 1.0f,
                     "%.2f");
  ImGui::TextDisabled(
      "Retail MasterVolume is intentionally left untouched: its original "
      "load-back semantics are not authenticated.");
}

void Launcher::drawRetailGeneralOptions() {
  ImGui::Spacing();
  ImGui::SeparatorText("General options");
  if (!retailConfigReady) {
    ImGui::TextDisabled("The retail Config.DAT profile is unavailable.");
    return;
  }
  using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
  ImGui::Checkbox("Show text##sd", &retailDraft.textView);
  ImGui::Checkbox("Skip unread scenes##sd", &retailDraft.skip);
  ImGui::Checkbox("SuperSkip / next choice##sd", &retailDraft.superSkip);
  ImGui::Checkbox("Auto draw##sd", &retailDraft.autoDraw);
  ImGui::Checkbox("Auto mode##sd", &retailDraft.autoMode);
  ImGui::Checkbox("Swap controller A/B##sd", &settings().swapControllerAB);
  ImGui::Checkbox("Forced subtitles##sd", &settings().enableForcedSubtitles);

  refreshLxhAvailability();
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  ImGui::Spacing();
  ImGui::SeparatorText("Optional expansion");
  bool wantLxh = enableLxhPreference();
  if (ImGui::Checkbox("Enable LxH endings##sd", &wantLxh))
    setEnableLxhPreference(wantLxh);
  ImGui::TextWrapped(
      "Enables the seven additional epilogues when the required files are "
      "available.");
  const auto lxhResolved = resolveCurrentLxhState();
  ImGui::TextDisabled("Status: %s", lxhResolved.statusReason.c_str());
#endif

  constexpr const char *aspectLabels[]{"Automatic", "4:3", "16:9"};
  int aspect = aspectPreference;
  if (ImGui::Combo("Narrative aspect##sd", &aspect, aspectLabels, 3))
    setContentAspectPreference(aspect);
  ImGui::TextDisabled(
      "Narrative aspect changes the authored story layout only; monitor "
      "resolution remains in settings.json.");
  if (ImGui::Button("Retail General/Sound defaults##sd")) {
    retailDraft = Policy::Defaults();
    syncMixerFromRetail();
    aspectPreference = 0;
  }
}

void Launcher::drawGameOptions() {
  seedFirstRunDisplayIfPossible();
  GameLauncher::drawGameOptions();
  drawSharedDisplayOptions();
  drawRetailGeneralSound();
  drawLanguageOptions();
}

void Launcher::drawLanguageOptions() {
  ImGui::Spacing();
  ImGui::SeparatorText("School Days HQ language");
  const auto beforeSelection = language();
  std::string customLabel = fs::path(settings().translationPath).stem().string();
  const char *current = customLabel.empty() ? "Select a language" : customLabel.c_str();
  switch (beforeSelection) {
  case Language::English:
  case Language::EnglishGpk: current = "English"; break;
  case Language::BrazilianGpk: current = "Português (Brasil)"; break;
  case Language::SpanishGpk: current = "Español"; break;
  case Language::ChineseGpk: current = "Chinese - Simplified"; break;
  case Language::RussianGpk: current = "Russian"; break;
  case Language::CustomJson: break; // Historic/CLI profile, not selectable.
  default:
    if (const auto *preset = jsonPresetForLanguage(beforeSelection))
      current = preset->displayName;
    break;
  }
  if (ImGui::BeginCombo("Language", current)) {
    ImGui::SeparatorText("GPK");
    if (ImGui::Selectable("English##gpk",
                          beforeSelection == Language::EnglishGpk))
      setLanguage(Language::EnglishGpk);
    if (ImGui::Selectable("Português (Brasil)##gpk",
                          beforeSelection == Language::BrazilianGpk))
      setLanguage(Language::BrazilianGpk);
    // The original Spanish NSIS patch modifies controls and media paths.
    // Enable this option only after certified, retail-compatible GPKs exist.
    refreshLxhAvailability();
    const bool lxhWanted = enableLxhPreference() && lxhPackAvailability.packValid;
    std::error_code spanishError;
    const bool spanishPackReady = !getSettingsPath().empty() &&
        fs::is_regular_file(Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(
            getSettingsPath(), "es"), spanishError);
    ImGui::BeginDisabled(!spanishPackReady);
    if (ImGui::Selectable("Español##gpk",
                          beforeSelection == Language::SpanishGpk))
      setLanguage(Language::SpanishGpk);
    ImGui::EndDisabled();
    if (!spanishPackReady)
      ImGui::TextDisabled("Spanish GPK requires a certified Script - Spanish.GPK archive.");
    // The original legacy Chinese GPK is incompatible. Use only the
    // retail-aligned UTF-8 derivative; full structural parity is verified
    // again by the mount before playback.
    std::error_code chineseError;
    const bool chinesePackReady = !getSettingsPath().empty() &&
        fs::is_regular_file(Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(
            getSettingsPath(), "zh-CN"), chineseError);
    ImGui::BeginDisabled(!chinesePackReady);
    if (ImGui::Selectable("Chinese - Simplified##gpk",
                          beforeSelection == Language::ChineseGpk))
      setLanguage(Language::ChineseGpk);
    ImGui::EndDisabled();
    if (!chinesePackReady)
      ImGui::TextDisabled("Chinese GPK requires Script - chinese-compatible.GPK.");
    std::error_code russianError;
    const bool russianPackReady = !getSettingsPath().empty() &&
        fs::is_regular_file(Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(
            getSettingsPath(), "ru"), russianError);
    ImGui::BeginDisabled(!russianPackReady);
    if (ImGui::Selectable("Russian##gpk",
                          beforeSelection == Language::RussianGpk))
      setLanguage(Language::RussianGpk);
    ImGui::EndDisabled();
    if (!russianPackReady)
      ImGui::TextDisabled("Russian GPK requires its certified Script - Russian.GPK archive.");
    (void)lxhWanted;
    ImGui::SeparatorText("JSON");
    for (const auto &preset : kJsonPresets) {
      const bool installed = !getSettingsPath().empty() &&
          regularTranslationFile(Kotonoha::SchoolDaysGpkLanguageProfile::
              JsonPath(getSettingsPath(), preset.filename).string());
      ImGui::BeginDisabled(!installed);
      const std::string item = std::string(preset.displayName) + "##json-" +
                               preset.filename;
      if (ImGui::Selectable(item.c_str(), beforeSelection == preset.language))
        setLanguage(preset.language);
      ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  const auto selected = language();
  ImGui::TextDisabled("Format: %s", isGpkLanguage(selected) ||
      selected == Language::English ? "GPK" : "JSON");
  if (isGpkLanguage(selected)) {
    const auto locale = gpkLocale(selected);
    if (!getSettingsPath().empty()) {
      const auto script = Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(
          getSettingsPath(), locale);
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
      const auto epilogue = Kotonoha::SchoolDaysGpkLanguageProfile::LxhPath(
          getSettingsPath(), locale);
#endif
      if (locale == "en")
        ImGui::TextDisabled("HQ: original Packs/Script.GPK (never duplicated).");
      else
        ImGui::TextWrapped("Base HQ GPK: %s", script.string().c_str());
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
      ImGui::TextWrapped("Seven LxH endings GPK: %s", epilogue.string().c_str());
#endif
      std::error_code ec;
      if (locale != "en" && !fs::is_regular_file(script, ec))
        ImGui::TextWrapped(
            "Missing original HQ language script in translations/scripts.");
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
      if (enableLxhPreference() && !fs::is_regular_file(epilogue, ec))
        ImGui::TextWrapped(
            "Optional LxH script %s not found in translations/scripts. "
            "Base School Days HQ will play in %s without LxH endings.",
            epilogue.filename().string().c_str(), locale.c_str());
#endif
    }
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    ImGui::TextDisabled("Retail-only edition: original 29 GPKs; no LxH epilogues or translations.");
#else
    ImGui::TextDisabled("Original 29 retail GPKs and optional LxH media stay untouched.");
    ImGui::TextDisabled("LxH status: %s", resolveCurrentLxhState().statusReason.c_str());
#endif
    ImGui::TextDisabled("Language texts are read from scripts, not converted to JSON.");
    return;
  }
  if (selected == Language::English) {
    ImGui::TextDisabled("English uses the original School Days HQ dialogue.");
  } else {
    if (regularTranslationFile(settings().translationPath)) {
      ImGui::TextWrapped("Base HQ JSON: %s",
                         settings().translationPath.c_str());
      ImGui::TextWrapped(
          "Play checks dialogue, choices and reviewed source-identical "
          "phrases. Missing entries do not fall back to English.");
    } else {
      ImGui::TextWrapped(
          "Base language JSON missing. Choose the matching HQ JSON before Play.");
    }
    ImGui::TextDisabled("The JSON list contains installed language profiles only.");
  }

 #if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
  ImGui::Spacing();
  ImGui::SeparatorText("LxH epilogue translation (7 endings)");
  const auto manual = manualLxhJsonPath();
  const auto lxh = lxhJsonPath();
  ImGui::TextWrapped("LxH JSON (%s): %s",
                     manual.empty() ? "auto-matched to base" : "manual override",
                     lxh.empty() ? "Not found" : lxh.c_str());
#if defined(__ANDROID__) || defined(ANDROID)
  if (ImGui::BeginCombo("Installed LxH JSON##sd",
                        lxh.empty() ? "Select imported LxH JSON" :
                        fs::path(lxh).filename().string().c_str())) {
    if (!getSettingsPath().empty())
      for (const auto &file : Kotonoha::SchoolDaysGpkLanguageProfile::
               DiscoverJsonFiles(getSettingsPath(), true)) {
        const auto name = file.filename().string();
        if (name.size() < 9 || name.compare(name.size() - 9, 9, "-lxh.json") != 0)
          continue;
        if (ImGui::Selectable(name.c_str(), manual == file.string()))
          setLxhJsonPath(file.string());
      }
    ImGui::EndCombo();
  }
#else
  if (ImGui::Button("Choose LxH JSON...")) {
    const auto initial = lxh.empty()
        ? (getSettingsPath().empty() ? std::string{}
             : (fs::path(getSettingsPath()).parent_path() / "translations/jsons").string())
        : fs::path(lxh).parent_path().string();
    browseFile({{"Translation JSON (*.json)", "json"}, {"All files (*)", "*"}},
               initial, [this](const std::string &path) { setLxhJsonPath(path); });
  }
#endif
  if (!manual.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Use matching LxH automatically")) setLxhJsonPath({});
  }
  ImGui::TextWrapped(
      "The LxH videos remain in Packs/LxH.GPK. Only subtitle JSON changes; "
      "the base and LxH JSON are validated together before Play.");
#endif
}

bool Launcher::loadLauncherArtwork() {
  if (artworkAttempted) return launcherArtwork != nullptr;
  artworkAttempted = true;
  // This is the user's School Days HQ artwork, not an external launcher
  // mockup. The installed copy is deliberately kept separate from retail GPKs.
  constexpr const char *filename = "SchoolDaysHQ-Launcher.png";
  std::vector<fs::path> choices;
  if (const char *base = SDL_GetBasePath(); base && *base) {
    choices.emplace_back(fs::path(base) / "assets" / filename);
    choices.emplace_back(fs::path(base) / "launcher-art" / filename);
  }
  if (const auto found = Kotonoha::FileResolver::findGameAsset(
          "school-days", std::string("launcher-art/") + filename))
    choices.push_back(*found);
  choices.emplace_back(fs::path("games/school-days/game-assets/launcher-art") /
                       filename);
  // Also recognize the older source layout for developer checkouts.
  choices.emplace_back(fs::path("games/school-days/launcher-art") / filename);
  for (const auto &candidate : choices) {
    std::error_code ec;
    if (!fs::is_regular_file(candidate, ec) || ec) continue;
    try {
      const auto length = fs::file_size(candidate, ec);
      if (ec || length == 0 || length > 16 * 1024 * 1024u)
        throw std::runtime_error("invalid artwork size");
      std::ifstream input(candidate, std::ios::binary);
      if (!input) throw std::runtime_error("cannot read artwork");
      std::vector<Uint8> bytes((std::istreambuf_iterator<char>(input)), {});
      auto surface = Kotonoha::MediaDecoder::decodePicture(bytes);
      if (!surface) throw std::runtime_error("cannot decode artwork");
      launcherArtwork.reset(SDL_CreateTextureFromSurface(launcherRenderer(),
                                                         surface.get()));
      if (!launcherArtwork) throw std::runtime_error(SDL_GetError());
      SDL_SetTextureScaleMode(launcherArtwork.get(), SDL_SCALEMODE_LINEAR);
      return true;
    } catch (const std::exception &error) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "School Days launcher artwork %s: %s",
                  candidate.string().c_str(), error.what());
    }
  }
  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
              "School Days HQ launcher artwork is missing; restore assets/%s",
              filename);
  return false;
}

void Launcher::openOptions() {
  if (screen == Screen::Options) return;
  optionSnapshot = OptionsSnapshot{settings(), retailDraft, selectedLanguage,
                                   selectedLocaleBaseJsonPath,
                                   selectedManualLxhJsonPath,
                                   manualLxhBaseJsonPath, aspectPreference,
                                   savedEnableLxh, enableLxhDraft,
                                   enableLxhModified};
  screen = Screen::Options;
#if !defined(__ANDROID__) && !defined(ANDROID)
  if (launcherWindow()) SDL_SetWindowSize(launcherWindow(), 950, 700);
#endif
}

#if defined(KOTONOHA_DISTRIBUTION_BUILD)
void Launcher::promptForOriginalGameFiles() {
  selectGameFilesTab = true;
  openOptions();
}
#endif

void Launcher::cancelOptions() {
  if (optionSnapshot) {
    settings() = optionSnapshot->settings;
    retailDraft = optionSnapshot->retail;
    selectedLanguage = optionSnapshot->language;
    selectedLocaleBaseJsonPath = optionSnapshot->baseJson;
    selectedManualLxhJsonPath = optionSnapshot->manualLxhJson;
    manualLxhBaseJsonPath = optionSnapshot->manualLxhBase;
    aspectPreference = optionSnapshot->aspect;
    savedEnableLxh = optionSnapshot->savedEnableLxh;
    enableLxhDraft = optionSnapshot->enableLxhDraft;
    enableLxhModified = optionSnapshot->enableLxhModified;
  }
  optionSnapshot.reset();
  screen = Screen::Main;
#if !defined(__ANDROID__) && !defined(ANDROID)
  if (launcherWindow()) SDL_SetWindowSize(launcherWindow(), 950, 600);
#endif
}

void Launcher::resetOptions() {
  // Reset is a draft operation until OK is clicked, matching conventional
  // Windows setup dialogs. Cancel can still restore the original snapshot.
  resetEngineDefaults();
  retailDraft = Kotonoha::SchoolDaysRetailConfigPolicy::Defaults();
  syncMixerFromRetail();
  aspectPreference = 0;
  setLanguage(Language::English);
  selectedManualLxhJsonPath.clear();
  manualLxhBaseJsonPath.clear();
  setStatus("Default options are staged. Select OK to save them.", false);
}

void Launcher::drawMainScreen() {
  const ImVec2 start = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(start,
                      {start.x + size.x, start.y + size.y},
                      IM_COL32(229, 243, 247, 255));
  if (loadLauncherArtwork()) {
    draw->AddImage(ImTextureRef(launcherArtwork.get()), start,
                   {start.x + size.x, start.y + size.y});
  } else {
    draw->AddText({start.x + 50.0f, start.y + 110.0f},
                  IM_COL32(15, 48, 74, 255), "SCHOOL DAYS HQ");
    draw->AddText({start.x + 50.0f, start.y + 140.0f},
                  IM_COL32(150, 35, 41, 255),
                  "Missing assets/SchoolDaysHQ-Launcher.png");
  }

  // The supplied artwork already contains its original School Days HQ logo.
  // Place the three actions immediately above it, in the clear sky area.
  const float buttonWidth = size.x * 0.253f;
  const float buttonHeight = std::max(35.0f, size.y * 0.068f);
  const float left = start.x + size.x * 0.642f;
  const float firstY = start.y + size.y * 0.49f;
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.19f, 0.33f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                         ImVec4(0.86f, 0.95f, 1.0f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                         ImVec4(0.75f, 0.87f, 0.96f, 0.90f));
  const auto button = [&](const char *label, int row) {
    ImGui::SetCursorScreenPos(
        {left, firstY + static_cast<float>(row) * (buttonHeight + 5.0f)});
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.32f);
    const bool clicked = ImGui::Button(label, {buttonWidth, buttonHeight});
    ImGui::PopFont();
    return clicked;
  };
  if (button("PLAY##school-days-main", 0)) {
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
    if (!pathValid()) promptForOriginalGameFiles();
    else (void)launch();
#else
    (void)launch();
#endif
  }
  if (button("OPTIONS##school-days-main", 1)) openOptions();
  if (button("QUIT##school-days-main", 2)) quit();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);

  // Clearly visible in both windows; no dependency on Audio tab or hovering.
  drawMusicToggle({start.x + size.x - 214.0f, start.y + 20.0f});

  if (!getStatus().empty()) {
    // Play failures must remain visible, rather than obscured by the artwork.
    const ImVec2 a{start.x + size.x * 0.43f, start.y + size.y * 0.17f};
    const ImVec2 b{start.x + size.x * 0.95f, start.y + size.y * 0.31f};
    draw->AddRectFilled(a, b, IM_COL32(255, 255, 255, 225), 5.0f);
    ImGui::SetCursorScreenPos({a.x + 12.0f, a.y + 9.0f});
    ImGui::PushTextWrapPos(b.x - 12.0f);
    ImGui::TextColored({0.62f, 0.12f, 0.14f, 1.0f}, "%s",
                       getStatus().c_str());
    ImGui::PopTextWrapPos();
  }
}

void Launcher::drawOptionsScreen() {
  const ImVec2 top = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float bannerHeight = 175.0f;
  draw->AddRectFilled(top, {top.x + size.x, top.y + size.y},
                      IM_COL32(248, 249, 251, 255));
  if (loadLauncherArtwork()) {
    // Crop ONLY the user's Kotonoha/Sekai key visual: show their faces in the
    // setup header without including the large logo twice.
    draw->AddImage(ImTextureRef(launcherArtwork.get()), top,
                   {top.x + size.x, top.y + bannerHeight},
                   {0.0f, 0.065f}, {1.0f, 0.357f});
  }
  draw->AddRectFilledMultiColor(
      {top.x + size.x * 0.56f, top.y},
      {top.x + size.x, top.y + bannerHeight},
      IM_COL32(255, 255, 255, 0), IM_COL32(247, 250, 253, 243),
      IM_COL32(247, 250, 253, 243), IM_COL32(255, 255, 255, 0));
  draw->AddRectFilled({top.x, top.y + bannerHeight - 2.0f},
                      {top.x + size.x, top.y + bannerHeight},
                      IM_COL32(24, 81, 126, 255));
  ImGui::SetCursorScreenPos({top.x + size.x * 0.68f, top.y + 61.0f});
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.19f, 0.33f, 1.0f));
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
  ImGui::TextUnformatted("School Days HQ");
  ImGui::PopFont();
  ImGui::SetCursorScreenPos({top.x + size.x * 0.68f, top.y + 98.0f});
  ImGui::TextUnformatted("SETUP / OPTIONS");
  ImGui::PopStyleColor();

  // Win32-style setup treatment, rendered inside the already embedded ImGui
  // backend. No second executable, UI process, GUI toolkit or background app.
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.11f, 0.16f, 0.22f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled,
                         ImVec4(0.39f, 0.45f, 0.51f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                         ImVec4(0.974f, 0.978f, 0.982f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg,
                         ImVec4(0.99f, 0.995f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                         ImVec4(0.91f, 0.95f, 0.985f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button,
                         ImVec4(0.91f, 0.93f, 0.95f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                         ImVec4(0.81f, 0.90f, 0.97f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                         ImVec4(0.72f, 0.84f, 0.96f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Header,
                         ImVec4(0.82f, 0.90f, 0.96f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                         ImVec4(0.73f, 0.86f, 0.95f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Tab,
                         ImVec4(0.90f, 0.93f, 0.96f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TabHovered,
                         ImVec4(0.81f, 0.90f, 0.97f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TabSelected,
                         ImVec4(0.98f, 0.99f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border,
                         ImVec4(0.76f, 0.79f, 0.83f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                         ImVec4(0.11f, 0.39f, 0.62f, 1.0f));

  ImGui::SetCursorScreenPos({top.x + 22.0f, top.y + bannerHeight + 14.0f});
  ImGui::BeginChild("##school-days-options-body",
                    {std::max(100.0f, size.x - 44.0f),
                     std::max(130.0f, size.y - bannerHeight - 101.0f)},
                    ImGuiChildFlags_Borders);
  if (ImGui::BeginTabBar("##school-days-options-tabs")) {
    if (ImGui::BeginTabItem("Display")) {
      drawPhysicalDisplayOptions();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Graphics")) {
      drawGraphicsOptions();
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
      if (ImGui::CollapsingHeader("Additional engine upscaling settings"))
        drawUpscaling();
#endif
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio")) {
      ImGui::SeparatorText("Launcher-only music");
      bool launcherMusic = musicEnabled;
      if (ImGui::Checkbox("Play music while launcher is open##sd",
                          &launcherMusic))
        toggleMusic();
      ImGui::TextDisabled("Does not change the Title music in the game.");
      drawRetailAudioOptions();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("General")) {
      drawRetailGeneralOptions();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Language")) {
      drawLanguageOptions();
      ImGui::EndTabItem();
    }
#if defined(__ANDROID__) || defined(ANDROID)
    if (ImGui::BeginTabItem("Game Files")) {
      ImGui::SeparatorText("Imported School Days HQ data");
      ImGui::TextWrapped("Original 29 HQ GPKs and LxH.GPK are imported using "
                         "the Android file picker, before this launcher opens. "
                         "They remain outside the APK and are not modified.");
      ImGui::TextWrapped("Active Packs folder: %s", path().c_str());
      ImGui::TextWrapped("Routing profile: %s", routePath().c_str());
      ImGui::TextDisabled("To import a new language, close the app and use "
                          "Import language GPK or JSON on its Android setup screen.");
      ImGui::TextDisabled("A content:// URI is not a disk path and cannot be "
                          "entered into the desktop Folder/ISO field.");
      ImGui::EndTabItem();
    }
#elif defined(KOTONOHA_DISTRIBUTION_BUILD)
    if (ImGui::BeginTabItem("Game Files", nullptr,
                            selectGameFilesTab ? ImGuiTabItemFlags_SetSelected
                                               : ImGuiTabItemFlags_None)) {
      ImGui::SeparatorText("School Days HQ original game files");
      ImGui::TextWrapped(
          "Select the folder of your legitimate School Days HQ installation "
          "(or its Packs folder). Original game assets are not included.");
      if (!pathValid())
        ImGui::TextWrapped(
            "IMPORTANT: Do NOT select this new distribution folder or add "
            "\\Packs to its path. That folder contains no retail packs. "
            "Click Folder... and select the ORIGINAL School Days HQ game "
            "installation containing the 29 original retail GPKs "
            "(optional LxH.GPK is supported when present). "
            "No save files are required.");
      drawPrimaryGamePath();
      ImGui::EndTabItem();
    }
    selectGameFilesTab = false;
#else
    if (ImGui::BeginTabItem("Advanced")) {
      ImGui::SeparatorText("Data files and routing");
      GameLauncher::drawGameOptions();
      ImGui::SeparatorText("Asset overlay");
      ImGui::InputText("Overlay folder##sd", &settings().assetOverlayPath);
      if (ImGui::Button("Choose overlay folder..."))
        browseFolder(settings().assetOverlayPath, [this](const std::string &value) {
          settings().assetOverlayPath = value;
        });
      ImGui::SameLine();
      if (ImGui::Button("Automatic overlay"))
        settings().assetOverlayPath.clear();
      // School Days owns translation selection in the Language tab. Do not
      // expose the generic arbitrary-JSON chooser in the Advanced tab.
      if (ImGui::CollapsingHeader("Advanced engine settings"))
        drawAdvanced();
      ImGui::EndTabItem();
    }
#endif
    ImGui::EndTabBar();
  }
  ImGui::EndChild();

  const float footerY = top.y + size.y - 65.0f;
  ImGui::SetCursorScreenPos({top.x + 25.0f, footerY});
  if (ImGui::Button("Reset##school-days-options", {112.0f, 31.0f}))
    resetOptions();
  drawMusicToggle({top.x + 155.0f, footerY});
  ImGui::SetCursorScreenPos({top.x + size.x - 261.0f, footerY});
  if (ImGui::Button("OK##school-days-options", {112.0f, 31.0f})) {
    if (apply()) {
      optionSnapshot.reset();
      screen = Screen::Main;
#if !defined(__ANDROID__) && !defined(ANDROID)
      if (launcherWindow()) SDL_SetWindowSize(launcherWindow(), 950, 600);
#endif
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel##school-days-options", {112.0f, 31.0f}))
    cancelOptions();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelOptions();
  if (!getStatus().empty()) {
    ImGui::SetCursorScreenPos({top.x + 25.0f, footerY + 37.0f});
    ImGui::PushTextWrapPos(top.x + size.x - 30.0f);
    ImGui::TextColored({0.50f, 0.21f, 0.20f, 1.0f}, "%s",
                       getStatus().c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::PopStyleColor(15);
  ImGui::PopStyleVar(3);
}

bool Launcher::drawCustomContents() {
  if (!launcherWindow() || !launcherRenderer()) return false;
  // The banner is a notification, not a modal window: show each new status
  // for five seconds, including failed validation and reset notifications.
  // Dismissing the message does not bypass apply()/launch() validation.
  const std::string currentStatus = getStatus();
  if (currentStatus != lastStatusNotice) {
    lastStatusNotice = currentStatus;
    statusNoticeAt = currentStatus.empty()
                         ? std::nullopt
                         : std::optional<std::uint64_t>(SDL_GetTicks());
  }
  if (statusNoticeAt &&
      statusNoticeExpired(*statusNoticeAt, SDL_GetTicks())) {
    setStatus("", false);
    statusNoticeAt.reset();
    lastStatusNotice.clear();
  }
  if (screen == Screen::Main)
    drawMainScreen();
  else
    drawOptionsScreen();
  return true;
}

bool Launcher::validate(std::string &error) {
  seedFirstRunDisplayIfPossible();
  if (!GameLauncher::validate(error)) return false;
  // A folder such as "FSRCNNX" is not a valid shader. Previously it was
  // persisted without complaint and the MPV loader silently fell back to a
  // normal copy, making it look as if the user's shader did not work.
  for (const auto &file : Kotonoha::UpscaleShaderFiles(settings().upscaling.shaderFiles)) {
    if (!Kotonoha::SchoolDaysShaderPresetCatalog::SupportedShaderFile(file)) {
      error = "Custom GLSL must be the full path to a .glsl/.hook/.frag file, "
              "not a folder (e.g. FSRCNNX_x2_16-0-4-1.glsl).";
      return false;
    }
    try {
      const fs::path shaderPath = fs::u8path(file);
      std::error_code ec;
      const auto resolved = Kotonoha::FileResolver::findAsset(shaderPath);
      if (!fs::is_regular_file(shaderPath, ec) &&
          (!resolved || !fs::is_regular_file(*resolved, ec))) {
        error = "Custom GLSL file not found: " + file;
        return false;
      }
    } catch (const std::exception &) {
      error = "Custom GLSL path is invalid; select an existing shader file.";
      return false;
    }
  }
  if (Kotonoha::SchoolDaysUpscalingPolicy::IncompatibleMpvSelection(settings())) {
    error = "MPV/Anime4K/ArtCNN requires the OpenGL renderer. Select "
            "OpenGL or change Upscaler to Auto/Linear for Direct3D12.";
    return false;
  }
  if (retailConfigReady) {
    try {
      checkRetailFileUnchanged();
      if (aspectPreference != loadedAspectPreference &&
          readAspectPreference(retailRoot) != loadedAspectPreference)
        throw std::runtime_error(
            "Narrative aspect changed in another session; close and reopen launcher");
      syncRetailFromMixer();
    } catch (const std::exception &exception) {
      error = exception.what();
      return false;
    }
    // Cache only: this path performs no disk I/O. The host writes Config.DAT
    // after Play passes every locale gate, in save().
    std::string failure;
    if (!retailStorage.CacheState(retailDraft, &failure)) {
      error = "Cannot stage retail General/Sound settings: " + failure;
      return false;
    }
    if (!retailStorage.CacheWindowMode(
            settings().mode == Kotonoha::WindowMode::Windowed ? 0 : 1,
            &failure)) {
      error = "Cannot stage retail WindowMode: " + failure;
      return false;
    }
    if (aspectPreference != loadedAspectPreference && aspectPreference != 0) {
      const auto authored = Kotonoha::SchoolDaysDisplayPolicy::
          CanonicalRetailStateForVariant(
              aspectPreference == 1
                  ? Kotonoha::SchoolDaysDisplayPolicy::Variant::Standard
                  : Kotonoha::SchoolDaysDisplayPolicy::Variant::WideFull);
      if (!retailStorage.CacheDisplayType(authored.effectiveDisplayType,
                                          &failure) ||
          !retailStorage.CacheTypeMiniNote(authored.typeMiniNote, &failure)) {
        error = "Cannot stage authored aspect in Config.DAT: " + failure;
        return false;
      }
    }
  }
  refreshLxhAvailability();
  const bool wantLxh = enableLxhPreference();
  effectiveLxh = false;
  lxhNotice.clear();
  const auto &translationFile = settings().translationPath;
  if (isGpkLanguage(language())) {
    if (getSettingsPath().empty()) {
      error = "Native GPK languages need a persistent School Days settings path.";
      return false;
    }
    if (!translationFile.empty()) {
      error = "Native GPK mode cannot be combined with a base JSON dictionary.";
      return false;
    }
    const auto locale = gpkLocale(language());
    const auto scriptPath =
        Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(getSettingsPath(), locale);
    Kotonoha::SchoolDaysGpkArchive base;
    std::string reason;
    if (locale != "en") {
      if (!base.Open(scriptPath, &reason)) {
        error = "Base " + locale + " Script.GPK cannot be opened: " + reason;
        return false;
      }
      if (base.EntryCount() < 1857 ||
          base.Lookup("English/00/00-00-A00.ENG.ORS") !=
              Kotonoha::SchoolDaysGpkLookupStatus::Present) {
        error = "Selected translated Script.GPK is not compatible with the "
                "School Days HQ retail ORS names. A converted pack is needed.";
        return false;
      }
    }
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    if (!wantLxh) {
      effectiveLxh = false;
      lxhNotice = "LxH endings disabled (playing original School Days HQ).";
      return true;
    }
    if (!lxhPackAvailability.packValid) {
      effectiveLxh = false;
      lxhNotice =
          "LxH endings unavailable (" + lxhPackAvailability.reason +
          "); playing base School Days HQ.";
      return true;
    }
    const auto lxhPath =
        Kotonoha::SchoolDaysGpkLanguageProfile::LxhPath(getSettingsPath(), locale);
    Kotonoha::SchoolDaysGpkArchive epilogue;
    bool epilogueValid = epilogue.Open(lxhPath, &reason);
    if (epilogueValid && epilogue.EntryCount() != 7) {
      epilogueValid = false;
      reason = "LxH language GPK must contain exactly the seven extended ORS";
    }
    if (epilogueValid) {
      for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
        const std::string logical =
            "Script/English/05/" + std::string(ending) + ".ENG.ORS";
        if (epilogue.Lookup(logical) !=
            Kotonoha::SchoolDaysGpkLookupStatus::Present) {
          epilogueValid = false;
          reason = "missing translated ending " + logical;
          break;
        }
      }
    }
    if (!epilogueValid) {
      // Never fall back silently to another language. Disable effective LxH
      // and allow base School Days HQ in the chosen language.
      effectiveLxh = false;
      lxhNotice =
          "LxH endings unavailable for " + locale + " (" +
          lxhPath.filename().string() + ": " + reason +
          "); playing base School Days HQ without LxH.";
      return true;
    }
    try {
      SchoolDays::Assets sourceAssets(
          Kotonoha::gameDataDirectory(path()), effectiveOverlayRoot().string(),
          true, {}, Kotonoha::SchoolDaysLxhMountMode::Required);
      bool mediaInstalled = false;
      if (!checkIntegratedLxhContent(sourceAssets, &mediaInstalled, error) ||
          !mediaInstalled) {
        effectiveLxh = false;
        lxhNotice = error.empty()
                        ? "LxH endings unavailable (missing LxH media); playing base School Days HQ."
                        : error;
        error.clear();
        return true;
      }
      effectiveLxh = true;
      lxhNotice = "LxH endings enabled (7 epilogues ready).";
    } catch (const std::exception &e) {
      effectiveLxh = false;
      lxhNotice = std::string("LxH endings unavailable (") + e.what() +
                  "); playing base School Days HQ.";
      return true;
    }
#endif
    return true;
  }
  if (language() != Language::English &&
      !regularTranslationFile(translationFile)) {
    error = "Selected non-English locale has no valid base JSON. Choose a "
            "translation file in School Days HQ language before Play.";
    return false;
  }
  if (wantLxh && !manualLxhJsonPath().empty() &&
      !regularTranslationFile(manualLxhJsonPath())) {
    error = "Manually selected LxH JSON is missing. Choose a valid file or "
            "restore automatic matching.";
    return false;
  }
  try {
    const bool nativeHq = translationFile.empty();
    Kotonoha::Translation dictionary, reviewed;
    if (!nativeHq) {
      dictionary.load(translationFile);
      reviewed = Kotonoha::SchoolDaysTranslationGate::LoadReviewedIdentities(
          translationFile);
      if (language() == Language::Spanish ||
          language() == Language::ChineseSimplified) {
        bool hasTranslation = false;
        for (const auto &[node, entries] : dictionary.getSections()) {
          for (const auto &[original, translated] : entries)
            if (!translated.empty() && translated != original) {
              hasTranslation = true;
              break;
            }
          if (hasTranslation) break;
        }
        if (!hasTranslation) {
          error = "Selected language JSON contains no translated entries.";
          return false;
        }
      }
    }
    const auto locale = Kotonoha::SchoolDaysTranslationGate::LocaleId(
        translationFile);
    const bool installedBaseJson =
        !nativeHq &&
        (locale == "en" ||
         Kotonoha::SchoolDaysGpkLanguageProfile::IsInstalledLocaleJson(
             getSettingsPath(), translationFile));

    const auto validateBaseOnly = [&]() -> bool {
      SchoolDays::Assets baseAssets(
          Kotonoha::gameDataDirectory(path()), effectiveOverlayRoot().string(),
          true, {}, Kotonoha::SchoolDaysLxhMountMode::Disabled);
      if (nativeHq)
        return true;
      const auto baseSources =
          Kotonoha::SchoolDaysTranslationGate::CollectDetailed(baseAssets,
                                                               routePath());
      const auto baseAudit = Kotonoha::SchoolDaysTranslationGate::CheckLayered(
          baseSources, dictionary, reviewed, {}, {}, false, installedBaseJson);
      if (!baseAudit.certifiedForNonNativeDisplay()) {
        error = "Selected School Days locale INCOMPLETE: " +
                baseAudit.summary() +
                ". English fallback is forbidden. Review missing and "
                "source-identical dialogue/choices before Play.";
        return false;
      }
      return true;
    };

    // Probe whether this installation uses packed retail GPKs (where LxH.GPK
    // is independently toggleable) or an extracted loose developer directory.
    SchoolDays::Assets sourceAssets(
        Kotonoha::gameDataDirectory(path()), effectiveOverlayRoot().string(),
        true, {},
        wantLxh ? Kotonoha::SchoolDaysLxhMountMode::Optional
                : Kotonoha::SchoolDaysLxhMountMode::Disabled);
    const bool packedInstall = sourceAssets.usesPackedResources();

    if (packedInstall && !wantLxh) {
      if (!nativeHq) {
        const auto baseSources =
            Kotonoha::SchoolDaysTranslationGate::CollectDetailed(sourceAssets,
                                                                 routePath());
        const auto baseAudit = Kotonoha::SchoolDaysTranslationGate::CheckLayered(
            baseSources, dictionary, reviewed, {}, {}, false, installedBaseJson);
        if (!baseAudit.certifiedForNonNativeDisplay()) {
          error = "Selected School Days locale INCOMPLETE: " +
                  baseAudit.summary() +
                  ". English fallback is forbidden. Review missing and "
                  "source-identical dialogue/choices before Play.";
          return false;
        }
      }
      effectiveLxh = false;
      lxhNotice = "LxH endings disabled (playing original School Days HQ).";
      return true;
    }

    bool epiloguesPresent = false;
    std::string lxhMediaError;
    const bool mediaOk =
        checkIntegratedLxhContent(sourceAssets, &epiloguesPresent, lxhMediaError);
    if (!mediaOk) {
      if (!packedInstall) {
        error = lxhMediaError;
        return false;
      }
      if (!validateBaseOnly())
        return false;
      effectiveLxh = false;
      lxhNotice = !lxhMediaError.empty()
                      ? lxhMediaError
                      : "LxH endings unavailable; playing base School Days HQ.";
      return true;
    }
    if (!epiloguesPresent) {
      if (!validateBaseOnly())
        return false;
      effectiveLxh = false;
      lxhNotice = wantLxh
                      ? (!lxhPackAvailability.reason.empty()
                             ? ("LxH endings unavailable (" +
                                lxhPackAvailability.reason +
                                "); playing base School Days HQ.")
                             : "LxH endings unavailable; playing base School Days HQ.")
                      : "LxH endings disabled (playing original School Days HQ).";
      return true;
    }

    Kotonoha::Translation dlc, reviewedDlc;
    const std::string dlcFile = lxhJsonPath();
    if (dlcFile.empty() && locale != "es") {
      if (packedInstall) {
        if (!validateBaseOnly())
          return false;
        effectiveLxh = false;
        lxhNotice = "LxH endings unavailable for " + locale + " (missing " +
                    locale +
                    "-lxh.json); playing base School Days HQ without LxH.";
        return true;
      }
      if (!nativeHq) {
        Kotonoha::SchoolDaysTranslationGate::RequireLxhSidecar(locale, dlcFile);
      }
    }
    if (!dlcFile.empty()) {
      dlc.load(dlcFile);
      reviewedDlc =
          Kotonoha::SchoolDaysTranslationGate::LoadReviewedIdentities(dlcFile);
    }
    const auto sources = Kotonoha::SchoolDaysTranslationGate::CollectDetailed(
        sourceAssets, routePath());
    const auto audit = Kotonoha::SchoolDaysTranslationGate::CheckLayered(
        sources, dictionary, reviewed, dlc, reviewedDlc, nativeHq,
        installedBaseJson);
    if (locale == "es" && audit.overriddenBaseSlots != 0) {
      error = "Spanish LxH JSON conflicts with the installed HQ V4b (" +
              std::to_string(audit.overriddenBaseSlots) +
              " changed dialogue(s)). Preserve the already reviewed Spanish lines.";
      return false;
    }
    if (!audit.certifiedForNonNativeDisplay()) {
      if (packedInstall) {
        if (!validateBaseOnly())
          return false;
        effectiveLxh = false;
        lxhNotice = "LxH endings unavailable for " + locale +
                    " (incomplete LxH translation: " + audit.summary() +
                    "); playing base School Days HQ without LxH.";
        return true;
      }
      error = "Selected School Days locale INCOMPLETE: " + audit.summary() +
              ". LxH requires complete dialogue for locale " + locale +
              (dlcFile.empty() ? " (epilogue JSON missing)."
                               : " (epilogue JSON: " + dlcFile + ").");
      return false;
    }
    effectiveLxh = true;
    lxhNotice = "LxH endings enabled (7 epilogues ready).";
  } catch (const std::exception &exception) {
    error = std::string("School Days locale validation failed: ") +
            exception.what();
    return false;
  }
  return true;
}

void Launcher::save() {
  if (retailConfigReady) {
    checkRetailFileUnchanged();
    std::error_code ec;
    fs::create_directories(retailRoot, ec);
    if (ec)
      throw std::runtime_error("Cannot create retail profile: " + ec.message());
    std::string failure;
    if (!retailStorage.Flush(&failure))
      throw std::runtime_error("Cannot save retail Config.DAT: " + failure);
    saveAspectPreference();
  }
  saveLxhPreference();
  GameLauncher::save();
  if (!getSettingsPath().empty()) {
    std::string error;
    if (!persistShaderPaths(getSettingsPath(),
                            settings().upscaling.shaderFiles, &error))
      throw std::runtime_error("Cannot persist custom GLSL paths: " + error);
  }
  if (getSettingsPath().empty()) return;
  const auto nativeProfile =
      Kotonoha::SchoolDaysGpkLanguageProfile::Sidecar(getSettingsPath());
  if (isGpkLanguage(language())) {
    const std::string locale = gpkLocale(language());
    Kotonoha::writeTextFileAtomically(
        nativeProfile.string(),
        std::string(Kotonoha::SchoolDaysGpkLanguageProfile::Version) +
            "\n" + locale + "\n");
  } else {
    // Switching back to the original script or an existing JSON translation
    // explicitly disables GPK overrides without deleting the user's packs.
    std::error_code ec;
    (void)fs::remove(nativeProfile, ec);
    if (ec)
      throw std::runtime_error("Cannot disable old GPK language profile: " +
                               ec.message());
  }
  if (!languagePreferenceFile.empty()) {
    const auto *jsonPreset = jsonPresetForLanguage(language());
    const char *value = jsonPreset ? jsonPreset->savedName
                        : language() == Language::Spanish ? "spanish"
                        : language() == Language::ChineseSimplified ? "chinese"
                        : language() == Language::English ? "english"
                        : language() == Language::EnglishGpk ? "english-gpk"
                        : language() == Language::BrazilianGpk ? "brazilian-gpk"
                        : language() == Language::SpanishGpk ? "spanish-gpk"
                        : language() == Language::ChineseGpk ? "chinese-gpk"
                        : language() == Language::RussianGpk ? "russian-gpk"
                        : "custom";
    std::string payload = std::string(value) + "\n";
    if (language() != Language::English && !isGpkLanguage(language()))
      payload += settings().translationPath + "\n";
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    if (!isGpkLanguage(language()) && !manualLxhJsonPath().empty()) {
      if (language() == Language::English) payload += "\n";
      payload += "lxh-json=" + manualLxhJsonPath() + "\n";
    }
#endif
    Kotonoha::writeTextFileAtomically(languagePreferenceFile.string(),
                                      payload);
  }
}

} // namespace SchoolDays
