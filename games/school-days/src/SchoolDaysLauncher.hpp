#pragma once

#include <Kotonoha/components/DisplayModeCatalog.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>
#include <Kotonoha/routing/SchoolDaysLauncherLocale.hpp>
#include <Kotonoha/routing/SchoolDaysLxhPreference.hpp>

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Kotonoha { class MediaPlayer; }
struct ImVec2;

namespace SchoolDays {

class Launcher final : public Kotonoha::GameLauncher {
public:
  // JSON and GPK are separate opt-in profiles. Keep CustomJson internally
  // for existing language.txt / explicit CLI profiles; it is not a UI choice.
  enum class Language {
    English, Spanish, ChineseSimplified, CustomJson,
    EnglishGpk, BrazilianGpk, ChineseGpk, SpanishGpk, RussianGpk,
    EnglishJson, BrazilianJson, RussianJson
  };

  static bool validateRoute(const std::string &path, std::string &info);
  static constexpr const char *RouteRememberFile = "route.txt";
  // The generic settings serializer stores a portable filename. For manually
  // selected Spanish JSONs outside its search roots, preserve the exact path.
  static std::string rememberedSpanishJsonPath(const std::string &settingsPath);
  static bool spanishPreferenceSelected(const std::string &settingsPath);
  // Locale profile is shared by --launcher and direct start. The first line
  // selects the preset, the second remembers an optional full base JSON path,
  // and the third remembers a manually selected LxH JSON (when applicable).
  static std::string rememberedLocaleJsonPath(const std::string &settingsPath);
  static bool nonNativeLanguagePreferenceSelected(const std::string &settingsPath);
  static std::string rememberedLxhJsonPath(const std::string &settingsPath,
                                           const std::string &baseJsonPath);
  // Empty for legacy profiles; retail-only also accepts "ru" GPK.
  // Throws on a corrupt native-pack profile instead of falling back to EN.
  static std::string rememberedGpkLocale(const std::string &settingsPath);
  // Used by direct --no-launcher startup after generic settings were loaded.
  static std::string resolveStoredTranslation(const std::string &settingsPath,
                                              const std::string &stored);
  // Generic EngineSettings serializes GLSL paths as portable basenames. Keep
  // full paths in a game-specific sidecar for this launcher and direct start.
  // The PRIME/main/runtime host owns calling these on non-launcher startup.
  static bool persistShaderPaths(const std::string &settingsPath,
                                 const std::string &shaderFiles,
                                 std::string *error = nullptr);
  static std::string resolveStoredShaderPaths(const std::string &settingsPath,
                                              const std::string &stored);

  Launcher(std::string settingsPath, std::string directory,
           std::string route = {});
  ~Launcher() override;
  void close();
  // Every launcher status banner (informational or error) is transient.
  // The underlying validation remains blocking until the user fixes it.
  static constexpr std::uint64_t StatusNoticeMilliseconds = 5000;
  static bool statusNoticeExpired(std::uint64_t shownAt,
                                  std::uint64_t now);
  // Config.DAT lives in Runtime's executable/--documents profile, not beside
  // settings.json. The owning main.cpp MUST supply that exact path explicitly:
  // never guess and overwrite an unrelated commercial save profile.
  void setRetailConfigRoot(std::filesystem::path documentsRoot);
  bool hasRetailConfigRoot() const { return retailConfigReady; }
  const Kotonoha::SchoolDaysRetailConfigPolicy::State &retailSettings() const {
    return retailDraft;
  }
  void setRetailSettings(const Kotonoha::SchoolDaysRetailConfigPolicy::State &value);
  int contentAspectPreference() const { return aspectPreference; }
  void setContentAspectPreference(int value);
  std::optional<bool> savedLxhPreference() const { return savedEnableLxh; }
  bool enableLxhPreference() const;
  void setEnableLxhPreference(bool enabled);
  bool effectiveLxhEnabled() const { return effectiveLxh; }
  const std::string &lxhStatusNotice() const { return lxhNotice; }
  const Kotonoha::SchoolDaysLxhPreference::Availability &lxhAvailability() const {
    return lxhPackAvailability;
  }

  // Testable physical display contract shared with Display Options. Only a
  // fresh profile receives the desktop tuple; a persisted/manual tuple wins.
  static bool initializeFirstRunDisplay(Kotonoha::EngineSettings &target,
                                        bool hasStoredSettings,
                                        int desktopWidth, int desktopHeight,
                                        float desktopHz);
  static bool selectPhysicalResolution(
      Kotonoha::EngineSettings &target,
      const Kotonoha::DisplayModeChoice &choice,
      const SDL_DisplayMode *desktop);
  SDL_AppResult iterate();
  const std::string &routePath() const;
  void setRoutePath(std::string path);
  bool routeValid() const;
  const std::string &routeInfo() const;
  Language language() const;
  void setLanguage(Language selection);
  Kotonoha::SchoolDaysLauncherLocale::Language launcherUiLanguage() const {
    return uiLanguage;
  }
  void setLauncherUiLanguage(
      Kotonoha::SchoolDaysLauncherLocale::Language language);
  void setSpanishJsonPath(std::string path);
  void setChineseJsonPath(std::string path);
  void setCustomJsonPath(std::string path);
  void setLxhJsonPath(std::string path);
  const std::string &translationPath() const;
  std::string lxhJsonPath() const;
  std::string manualLxhJsonPath() const;
  // --scripts overrides the generic asset overlay in SchoolDays::Assets.
  void setScriptOverlayPath(std::string path);
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  // First-run setup; invoked only if no authentic Packs/Script.GPK exists.
  void promptForOriginalGameFiles();
#endif

protected:
  bool drawCustomContents() override;
  std::pair<int, int> preferredWindowSize() const override { return {950, 600}; }
  bool checkPath(const std::string &path, std::string &info) override;
  void drawGameOptions() override;
  bool validate(std::string &error) override;
  void save() override;

private:
  enum class Screen { Main, Options };
  struct OptionsSnapshot {
    Kotonoha::EngineSettings settings;
    Kotonoha::SchoolDaysRetailConfigPolicy::State retail;
    Language language = Language::English;
    std::string baseJson, manualLxhJson, manualLxhBase;
    int aspect = 0;
    std::optional<bool> savedEnableLxh;
    bool enableLxhDraft = false;
    bool enableLxhModified = false;
  };
  Screen screen = Screen::Main;
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  bool selectGameFilesTab = false;
#endif
  std::optional<OptionsSnapshot> optionSnapshot;
  std::optional<std::uint64_t> statusNoticeAt;
  std::string lastStatusNotice;
  Kotonoha::TexturePtr launcherArtwork;
  bool artworkAttempted = false;
  std::filesystem::path musicPreferenceFile;
  Kotonoha::SchoolDaysLauncherLocale::Language uiLanguage =
      Kotonoha::SchoolDaysLauncherLocale::Language::En;
  std::vector<Uint8> titleMusicBytes;
  std::unique_ptr<Kotonoha::MediaPlayer> titleMusic;
  std::string titleMusicSource;
  bool musicEnabled = true;
  bool musicAttempted = false;
  bool audioInitialized = false;
  std::size_t routeOption = 0;
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  // A fresh player install starts with the original English HQ script and
  // its matching LxH ORS pack. Never require a JSON translation for English.
  Language selectedLanguage = Language::EnglishGpk;
#else
  Language selectedLanguage = Language::English;
#endif
  std::filesystem::path languagePreferenceFile;
  std::string scriptOverlayPath, selectedLocaleBaseJsonPath;
  std::string selectedManualLxhJsonPath, manualLxhBaseJsonPath;
  bool initialDisplayPending = false;
  Kotonoha::EngineSettings initialDisplayBaseline;
  Kotonoha::SchoolDaysConfigStorage retailStorage;
  Kotonoha::SchoolDaysRetailConfigPolicy::State retailDraft;
  std::filesystem::path retailRoot;
  bool retailConfigReady = false;
  bool retailFileExisted = false;
  std::optional<std::filesystem::file_time_type> retailFileTimestamp;
  std::optional<std::uintmax_t> retailFileSize;
  int aspectPreference = 0;
  int loadedAspectPreference = 0;
  std::optional<bool> savedEnableLxh;
  bool enableLxhDraft = false;
  bool enableLxhModified = false;
  bool effectiveLxh = false;
  std::string lxhNotice;
  std::string probedLxhPath;
  Kotonoha::SchoolDaysLxhPreference::Availability lxhPackAvailability;

  std::filesystem::path lxhPreferenceRoot() const;
  void loadLxhPreference();
  void refreshLxhAvailability();
  void saveLxhPreference();
  Kotonoha::SchoolDaysLxhPreference::Resolution resolveCurrentLxhState(
      bool *outTranslationReady = nullptr,
      std::string *outTranslationReason = nullptr) const;
  std::filesystem::path effectiveOverlayRoot() const;
  std::string findSpanishJson() const;
  std::string findChineseJson() const;
  std::string findInstalledJson(Language selection) const;
  const Kotonoha::EngineSettings &currentSettings() const;
  void seedFirstRunDisplayIfPossible();
  void drawSharedDisplayOptions();
  void drawPhysicalDisplayOptions();
  void drawGraphicsOptions();
  void drawRetailGeneralSound();
  void drawRetailAudioOptions();
  void drawRetailGeneralOptions();
  void drawLanguageOptions();
  void drawMainScreen();
  void drawOptionsScreen();
  void openOptions();
  void cancelOptions();
  void resetOptions();
  void updateTitleMusic();
  void toggleMusic();
  void drawMusicToggle(const ImVec2 &position);
  void drawLauncherLanguageSelector(const ImVec2 &position);
  const char *tr(Kotonoha::SchoolDaysLauncherLocale::Key key) const;
  std::string trId(Kotonoha::SchoolDaysLauncherLocale::Key key,
                   const char *idSuffix) const;
  std::string localizedLxhStatus() const;
  void drawLocalizedPrimaryGamePath();
  void applyWindowTitle();
  bool loadLauncherArtwork();
  void syncMixerFromRetail();
  void syncRetailFromMixer();
  void checkRetailFileUnchanged() const;
  void saveAspectPreference();
};

} // namespace SchoolDays
