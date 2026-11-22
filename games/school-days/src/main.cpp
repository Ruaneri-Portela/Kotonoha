#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include "SchoolDaysAssets.hpp"
#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysMobileProfile.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/components/ForcedSubtitleRenderer.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/mobile/StartupStage.hpp>
#include <Kotonoha/routing/SchoolDaysGpkLanguageProfile.hpp>
#include <Kotonoha/routing/SchoolDaysLxhPreference.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>
#ifdef __ANDROID__
#include <Kotonoha/mobile/DocumentMount.hpp>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {
#if defined(__ANDROID__) || defined(ANDROID)
// Per-game Android frame pacing. This does not change script/video time (24fps)
// or any Windows render loop, and is intentionally separate from device Hz.
int ReadAndroidFrameLimit(const std::filesystem::path &documents) {
  std::ifstream input(documents / "Kotonoha.ini");
  std::string line;
  while (std::getline(input, line)) {
    constexpr const char *key = "AndroidFrameLimit=";
    if (line.rfind(key, 0) != 0) continue;
    try {
      const auto parsed = std::stoi(line.substr(std::strlen(key)));
      return parsed == 30 || parsed == 60 ? parsed : 0;
    } catch (...) { return 0; }
  }
  return 0;
}
#endif
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
// No QA startup trace file or developer logger exists in a shipped binary.
#define DevStartupLog(...) ((void)0)
#else
void DevStartupLog(const char *phase, const std::string &detail = {}) noexcept {
#if defined(KOTONOHA_DEV_CONTROL) && defined(_WIN32)
  try {
    const char *base = SDL_GetBasePath();
    if (!base) return;
    static bool first = true;
    std::ofstream log(std::filesystem::path(base) / "dev-startup.log",
                      first ? std::ios::trunc : std::ios::app);
    first = false;
    if (!log) return;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    log << milliseconds << ' ' << phase;
    if (!detail.empty()) log << ' ' << detail;
    log << '\n';
  } catch (...) {
    // Diagnostics must never prevent the game from starting.
  }
#else
  (void)phase;
  (void)detail;
#endif
}
#endif

Kotonoha::EngineOptions WithAssets(Kotonoha::EngineOptions options,
                                  const std::shared_ptr<SchoolDays::Assets> &assets) {
  options.assets = assets;
  return options;
}

struct Application {
  std::string requestedTranslation;
  std::string translationSettingsPath;
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;
  Application(std::shared_ptr<SchoolDays::Assets> assets, const std::string &route,
              Kotonoha::EngineOptions options, const std::string &documents,
              const std::string &lxhJsonOverride = {},
              const std::string &nativeGpkLocale = {},
              const std::string &installedTranslationRoot = {})
      : requestedTranslation(options.translationPath),
        translationSettingsPath(options.settingsPath),
        runtime(assets, route, documents, options.settingsPath),
        engine(runtime, WithAssets(std::move(options), assets)) {
    Kotonoha::Mobile::setStartupStage(Kotonoha::Mobile::StartupStage::Language);
    if (!nativeGpkLocale.empty()) {
      // The native translation already lives in the mounted Script.GPK and
      // seven LxH ORS. Never translate those strings again through JSON, and
      // never blend two languages after a stale generic settings restore.
      if (!engine.GetSettings().translationPath.empty() ||
          !engine.GetTranslation().getSections().empty())
        throw std::runtime_error(
            "GPK locale conflicts with a stored JSON translation. "
            "Select one language in the launcher.");
      engine.GetForcedSubtitles().setLocale(nativeGpkLocale);
      runtime.setLxhTranslation({}, nativeGpkLocale, true);
      return;
    }
    const auto &dictionary = engine.GetTranslation();
    const char *environmentTranslation = std::getenv("KOTONOHA_TRANSLATION");
    const bool nonNativeSelected = !requestedTranslation.empty() ||
        !engine.GetSettings().translationPath.empty() ||
        !dictionary.getSections().empty() ||
        (environmentTranslation && *environmentTranslation);
    const auto translationFile = !requestedTranslation.empty()
        ? requestedTranslation
        : !engine.GetSettings().translationPath.empty()
              ? engine.GetSettings().translationPath
              : environmentTranslation && *environmentTranslation
                  ? std::string(environmentTranslation) : std::string{};
    Kotonoha::Translation dlc;
    Kotonoha::Translation reviewedDlc;
    std::string dlcFile;
    const auto locale = Kotonoha::SchoolDaysTranslationGate::LocaleId(
        translationFile);
    // Packaged installs are permanently LxH-integrated and were validated
    // at mount. Only extracted loose developer fixtures may contain base HQ
    // ORS without the seven grafts; this is not a user-selectable DLC mode.
    const auto sources =
        Kotonoha::SchoolDaysTranslationGate::CollectDetailed(*assets, route);
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    const bool integratedContent = false;
#else
    const bool integratedContent =
        assets->usesPackedResources()
            ? assets->lxhMounted()
            : std::any_of(sources.begin(), sources.end(),
                          [](const auto &slot) { return slot.lxh; });
#endif
    if (integratedContent) {
      dlcFile = lxhJsonOverride.empty()
          ? Kotonoha::SchoolDaysTranslationGate::DlcJsonPath(locale, translationFile)
          : lxhJsonOverride;
      Kotonoha::SchoolDaysTranslationGate::RequireLxhSidecar(locale, dlcFile);
      if (!dlcFile.empty()) {
        dlc.load(dlcFile);
        reviewedDlc =
            Kotonoha::SchoolDaysTranslationGate::LoadReviewedIdentities(dlcFile);
      }
    }
    if (nonNativeSelected || integratedContent) {
      const auto reviewed = nonNativeSelected
          ? Kotonoha::SchoolDaysTranslationGate::LoadReviewedIdentities(
                translationFile)
          : Kotonoha::Translation{};
      const bool installedLocaleJson =
          Kotonoha::SchoolDaysGpkLanguageProfile::IsInstalledLocaleJson(
              translationSettingsPath, translationFile) ||
          (!installedTranslationRoot.empty() &&
           Kotonoha::SchoolDaysGpkLanguageProfile::IsInstalledLocaleJson(
               (std::filesystem::path(installedTranslationRoot) /
                "settings.json")
                   .string(),
               translationFile));
      const auto audit = Kotonoha::SchoolDaysTranslationGate::CheckLayered(
          sources, dictionary, reviewed, dlc, reviewedDlc, !nonNativeSelected,
          nonNativeSelected && (locale == "en" || installedLocaleJson));
      // The installed V4b Spanish dictionary already has all 273 LxH slots,
      // including nine independently corrected translations. A sidecar may
      // supply new missing slots, but must never silently replace these.
      if (locale == "es" && audit.overriddenBaseSlots != 0)
        throw std::runtime_error(
            "Spanish LxH JSON conflicts with the installed HQ V4b " +
            std::to_string(audit.overriddenBaseSlots) +
            " time(s). Preserve the already reviewed Spanish lines.");
      if (!audit.certifiedForNonNativeDisplay())
        throw std::runtime_error(
            "Selected School Days translation is INCOMPLETE: " + audit.summary() +
            (integratedContent
                 ? ". The integrated LxH pack requires complete dialogue "
                   "for the selected locale and forbids silent English fallback " +
                       (dlcFile.empty() ? "(LxH JSON missing)." :
                                          "(LxH JSON: " + dlcFile + ").")
                 : ". English fallback is forbidden in non-native language "
                   "even for an extracted developer fixture."));
    }
    // The engine selects ASS in the same locale as the validated JSON. Native
    // English without a selected translation retains the original root ASS.
    engine.GetForcedSubtitles().setLocale(nonNativeSelected ? locale : "");
    runtime.setLxhTranslation(std::move(dlc), locale);
  }
  ~Application() { runtime.shutdown(); }
};

struct App {
  std::unique_ptr<SchoolDays::Launcher> launcher;
  std::unique_ptr<Application> application;
  Kotonoha::EngineOptions options;
  std::string scripts, route, startNode, dump, documents, lxhJsonOverride, cliGpkLocale;
  std::shared_ptr<const Kotonoha::Filesystem::IMount> documentPacks;
  std::optional<bool> cliLxhOverride;
  std::optional<bool> launcherEffectiveLxh;
  long frames = -1;
#if defined(__ANDROID__) || defined(ANDROID)
  int frameLimit = 0;
#endif
  void start(const std::string &directory) {
#if defined(__ANDROID__) || defined(ANDROID)
    frameLimit = ReadAndroidFrameLimit(
        Kotonoha::preferredDocumentsDirectory("school-days"));
#endif
    DevStartupLog("assets-open", directory);
    const bool explicitlySelectedJson = !options.translationPath.empty() ||
        (std::getenv("KOTONOHA_TRANSLATION") &&
         *std::getenv("KOTONOHA_TRANSLATION"));
    // The generic engine serializes custom GLSL references as portable names.
    // School Days also retains the player's original, explicitly selected
    // external shader paths in its local launcher sidecar. Reapply only paths
    // that still match the persisted shader names and exist on this computer.
    std::string resolvedShaderPaths;
    // Ruaneri's generic settings serializer stores portable basenames. The
    // School Days launcher keeps full paths for community translations and
    // optional custom display artwork. Restore those paths on
    // ordinary no-launcher restarts too, without touching generic engine code.
    // Explicit --translation, --asset-overlay and --no-asset-overlay win.
    std::string gpkLocale = cliGpkLocale;
    if (!gpkLocale.empty()) {
      if (!Kotonoha::SchoolDaysGpkLanguageProfile::Supported(gpkLocale))
        throw std::runtime_error("Unsupported School Days GPK locale: " +
                                 gpkLocale);
      options.translationPath.clear();
    } else if (!explicitlySelectedJson && options.persistSettings &&
               !options.settingsPath.empty()) {
      gpkLocale = SchoolDays::Launcher::rememberedGpkLocale(
          options.settingsPath);
    }
    if (options.persistSettings && !options.settingsPath.empty() &&
        std::filesystem::is_regular_file(options.settingsPath)) {
      try {
        auto saved = Kotonoha::EngineSettings::load(options.settingsPath);
        resolvedShaderPaths = SchoolDays::Launcher::resolveStoredShaderPaths(
            options.settingsPath, saved.upscaling.shaderFiles);
        if (!cliGpkLocale.empty() && !saved.translationPath.empty()) {
          // A fresh GPK launch from the Android launcher must not inherit a
          // stale JSON translationPath persisted by an earlier session.
          saved.translationPath.clear();
          saved.save(options.settingsPath);
        } else if (gpkLocale.empty() && options.translationPath.empty() &&
                   !saved.translationPath.empty()) {
          options.translationPath = SchoolDays::Launcher::resolveStoredTranslation(
              options.settingsPath, saved.translationPath);
        }
        if (!options.assetOverlaySpecified && options.assetOverlayPath.empty() &&
            !saved.assetOverlayPath.empty()) {
          options.assetOverlayPath = saved.assetOverlayPath;
          options.settings.assetOverlayPath = options.assetOverlayPath;
        }
      } catch (const std::exception &) {
        // Engine owns validation/reporting for invalid user settings.
      }
    }
    if (gpkLocale.empty() && options.persistSettings &&
        !options.settingsPath.empty() &&
        SchoolDays::Launcher::nonNativeLanguagePreferenceSelected(
            options.settingsPath)) {
      // A relocated/nonstandard settings.json may have lost the JSON's full
      // path (or the generic basename entirely), but the dedicated launcher
      // still remembers the selected Spanish/Chinese/custom locale. Do not
      // silently return to English while a non-native language is selected.
      if (options.translationPath.empty())
        options.translationPath =
            SchoolDays::Launcher::rememberedLocaleJsonPath(options.settingsPath);
      if (options.translationPath.empty())
        throw std::runtime_error(
            "Non-English language is selected but its HQ JSON is missing; "
            "refusing English fallback. Select a valid base translation.");
    }
#if !defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    if (gpkLocale.empty() && lxhJsonOverride.empty() && options.persistSettings &&
        !options.settingsPath.empty())
      lxhJsonOverride = SchoolDays::Launcher::rememberedLxhJsonPath(
          options.settingsPath, options.translationPath);
#endif
    const std::filesystem::path lxhPrefRoot =
        documents.empty()
#if defined(__ANDROID__) || defined(ANDROID)
            ? Kotonoha::preferredDocumentsDirectory("school-days")
#else
            ? (!options.settingsPath.empty()
                   ? std::filesystem::path(options.settingsPath).parent_path()
                   : Kotonoha::resourceDirectory())
#endif
            : std::filesystem::path(documents);
    const auto gameDataDir = Kotonoha::gameDataDirectory(directory);
    const auto lxhPackAvail =
        Kotonoha::SchoolDaysLxhPreference::ProbePack(gameDataDir, documentPacks);
    const std::optional<bool> persistedLxhPref =
        cliLxhOverride.has_value()
            ? cliLxhOverride
            : (launcherEffectiveLxh.has_value()
                   ? launcherEffectiveLxh
                   : Kotonoha::SchoolDaysLxhPreference::Read(lxhPrefRoot));
    bool lxhEffective = false;
    std::string lxhStatusNote;
#if defined(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY)
    lxhEffective = false;
    lxhStatusNote = "Retail-only edition: 29 original GPKs, no LxH";
#else
    if (launcherEffectiveLxh.has_value() && !cliLxhOverride.has_value()) {
      lxhEffective = *launcherEffectiveLxh && lxhPackAvail.packValid;
      lxhStatusNote = lxhEffective
                          ? "LxH endings enabled (7 epilogues active)"
                          : "LxH endings disabled or unavailable; running base School Days HQ";
    } else {
      bool transReady = true;
      std::string transReason;
      if (!gpkLocale.empty()) {
        const std::string gpkProfileSettings =
            documentPacks
                ? (std::filesystem::path(directory) / "settings.json").string()
                : options.settingsPath;
        const auto lxhGpkPath = Kotonoha::SchoolDaysGpkLanguageProfile::LxhPath(
            gpkProfileSettings, gpkLocale);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(lxhGpkPath, ec) || ec) {
          transReady = false;
          transReason = "missing " + lxhGpkPath.filename().string();
        }
      } else {
        const auto locale = Kotonoha::SchoolDaysTranslationGate::LocaleId(
            options.translationPath);
        const std::string dlcPath =
            lxhJsonOverride.empty()
                ? Kotonoha::SchoolDaysTranslationGate::DlcJsonPath(
                      locale, options.translationPath)
                : lxhJsonOverride;
        if (dlcPath.empty() && locale != "es") {
          transReady = false;
          transReason = "missing " + locale + "-lxh.json";
        }
      }
      const auto resolved = Kotonoha::SchoolDaysLxhPreference::Resolve(
          persistedLxhPref, lxhPackAvail, transReady, transReason);
      lxhEffective = resolved.effectiveEnabled;
      lxhStatusNote = resolved.statusReason;
    }
#endif
    if (!gpkLocale.empty()) {
      if (!options.translationPath.empty())
        throw std::runtime_error(
            "Native Script.GPK locale conflicts with stored JSON settings. "
            "Re-select the language in the launcher.");
      const std::string gpkProfileSettings =
          documentPacks
              ? (std::filesystem::path(directory) / "settings.json").string()
              : options.settingsPath;
      const auto script = Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(
          gpkProfileSettings, gpkLocale);
      const std::filesystem::path lxh =
          lxhEffective ? Kotonoha::SchoolDaysGpkLanguageProfile::LxhPath(
                             gpkProfileSettings, gpkLocale)
                       : std::filesystem::path{};
      const Kotonoha::SchoolDaysTranslatedGpkSelection selection{
          gpkLocale, script, lxh, lxhEffective};
      std::string error;
      if (!Kotonoha::ConfigureSchoolDaysTranslatedGpks(selection, &error))
        throw std::runtime_error("Cannot select " + gpkLocale +
                                 " Script.GPK: " + error);
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                  "School Days native GPK language selected: %s (LxH=%s)",
                  gpkLocale.c_str(), lxhEffective ? "ON" : "OFF");
    } else {
      std::string error;
      if (!Kotonoha::ClearSchoolDaysTranslatedGpks(&error))
        throw std::runtime_error("Cannot reset native GPK locale: " + error);
    }
    const auto lxhMountMode =
        lxhEffective ? Kotonoha::SchoolDaysLxhMountMode::Required
                     : Kotonoha::SchoolDaysLxhMountMode::Disabled;
    auto assets = std::make_shared<SchoolDays::Assets>(
        gameDataDir,
        scripts.empty() ? options.assetOverlayDirectory() : scripts,
        // --no-asset-overlay is explicit even though it resolves to an empty
        // path. Do not re-enable the automatic overlay in the asset provider.
        !scripts.empty() || !options.assetOverlaySpecified ||
            !options.assetOverlayPath.empty(),
        documentPacks, lxhMountMode);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "School Days HQ startup: LxH=%s (%s)",
                assets->lxhMounted() ? "ON" : "OFF", lxhStatusNote.c_str());
    DevStartupLog("assets-ready");
    application = std::make_unique<Application>(
        std::move(assets), route, options, documents, lxhJsonOverride,
        gpkLocale, documentPacks ? directory : std::string{});
    if (!resolvedShaderPaths.empty() &&
        application->engine.GetSettings().upscaling.shaderFiles != resolvedShaderPaths) {
      auto live = application->engine.GetSettings();
      live.upscaling.shaderFiles = resolvedShaderPaths;
      // Engine's constructor has already read the portable settings.json.
      // This happens before the first frame; persist=false avoids replacing
      // the portable basename with an unusable absolute-only reference.
      application->engine.applySettings(live, false);
    }
    Kotonoha::Mobile::setStartupStage(Kotonoha::Mobile::StartupStage::Ready);
    DevStartupLog("engine-ready");
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
    if (!startNode.empty()) {
      application->runtime.SetState(Kotonoha::GameState::Gameplay);
      application->engine.debugJumpToNode(startNode);
    }
    if (!dump.empty()) application->engine.dumpKtrf(dump);
#endif
  }
};

}

struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **appstate, int argc, char **argv);
  static SDL_AppResult event(void *appstate, SDL_Event *event);
  static SDL_AppResult iterate(void *appstate);
  static void quit(void *appstate, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc,
                                          char **argv) {
  *appstate = nullptr;
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);
#endif
  DevStartupLog("app-init-entered");
  KOTONOHA_SET_APP_METADATA("School Days HQ",
                            "me.hirameki.kotonoha.schooldays");
  try {
    auto app = std::make_unique<App>();
    std::string directory;
    app->options.persistSettings = true;
    // Ruaneri's shared EngineOptions default enables desktop launchers.
#if defined(_WIN32) && !defined(__ANDROID__)
    app->options.launcher = true;
#endif
#if defined(_WIN32) && !defined(__ANDROID__)
    // The School Days commercial-test profile exposed Anime4K/ArtCNN to the
    // player.  In v3 the MPV-compatible upscaler is hosted by SDL's OpenGL
    // renderer, so prefer that renderer for a fresh/default Windows launch.
    // This is a FIRST-RUN default, not a forced rendererOverride: the latter
    // would silently defeat an explicit renderer chosen in the launcher and
    // saved in settings.json on every subsequent Windows launch. A genuine
    // command-line --renderer remains authoritative via rendererOverride.
    app->options.settings.renderer = "opengl";
    app->options.settings.mode = Kotonoha::WindowMode::Borderless;
    app->options.settings.vsync = true;
    app->options.settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
    app->options.settings.upscaling.preset = Kotonoha::UpscalePreset::ArtCNN;
    app->options.settings.upscaling.factor = 1.5f;
#elif defined(__ANDROID__) || defined(ANDROID)
    // The physical display mode is selected separately by the Display panel.
    app->options.rendererOverride = "opengles2";
    SchoolDays::applyMobileDisplayDefaults(app->options.settings);
#endif
    Kotonoha::CommandLine arguments("SchoolDays", app->options);
    arguments.option("--school-days", "DIRECTORY|ISO",
                     "Retail Packs, extracted game directory or ISO",
                     directory);
    arguments.alias("--school-days", "--assets");
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
    arguments.option("--ktrf", "FILE", "School Days routing graph", app->route);
#endif
    arguments.option("--scripts", "DIRECTORY",
                     "Translated ORS script directory", app->scripts);
    arguments.option("--documents", "DIRECTORY",
                     "Save-game and GlobalFlag directory", app->documents);
    bool cliEnableLxh = false;
    bool cliDisableLxh = false;
    arguments.flag("--lxh", "Enable optional LxH endings when available",
                   cliEnableLxh);
    arguments.flag("--no-lxh", "Disable optional LxH endings (base HQ only)",
                   cliDisableLxh);
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
    arguments.option("--node", "ID", "Start at an index or scene key",
                     app->startNode);
    arguments.option("--dump-ktrf", "FILE", "Export the active routing graph",
                     app->dump);
#endif
#ifdef __ANDROID__
    std::string catalog;
    arguments.option("--document-catalog", "FILE", "Private SAF launch catalog",
                     catalog);
    arguments.option("--gpk-locale", "LOCALE",
                     "Use native translated Script/LxH GPK locale",
                     app->cliGpkLocale);
#endif
    arguments.parse(argc, argv);
    if (cliEnableLxh && cliDisableLxh)
      throw std::runtime_error("Cannot specify both --lxh and --no-lxh");
    if (cliEnableLxh)
      app->cliLxhOverride = true;
    else if (cliDisableLxh)
      app->cliLxhOverride = false;
    // Android's SAF/language/options Activity owns the launch flow. Apply
    // this policy only after parsing so command-line handling remains shared
    // and desktop startup never inherits a premature launcher-disable state.
#if defined(__ANDROID__) || defined(ANDROID)
    app->options.launcher = false;
    if (!catalog.empty()) {
      Kotonoha::Mobile::setStartupStage(Kotonoha::Mobile::StartupStage::Files);
      app->documentPacks = Kotonoha::Filesystem::openDocumentDirectory(catalog);
      app->route =
          Kotonoha::FileResolver::resolve(std::filesystem::path(directory) /
                                          "school-days-hq.ktnroute")
              .string();
    }
#endif
    // Bounded headless QA must reach the game loop rather than the launcher.
    // DIST must not expose route-jump, KTRF export or developer controls.
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
    if (!app->dump.empty() || !app->startNode.empty() || arguments.frames > 0 ||
        app->options.enableMCP)
      app->options.launcher = false;
#else
    if (arguments.frames > 0)
      app->options.launcher = false;
#endif
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    DevStartupLog("arguments-parsed");
    app->frames = arguments.frames;
    if (app->options.subtitleStylePath.empty())
      if (const auto styles = Kotonoha::FileResolver::findGameAsset("school-days", "styles.ass"))
        app->options.subtitleStylePath = styles->string();
    if (app->options.enableMCP && app->options.settingsPath.empty())
      app->options.persistSettings = false;
    if (app->options.persistSettings && app->options.settingsPath.empty()) {
#if defined(_WIN32) && !defined(__ANDROID__)
      // The commercial Windows profile keeps its display configuration beside
      // SchoolDays.exe. Preserve that ownership while using the v3 JSON format.
      app->options.settingsPath =
          (Kotonoha::resourceDirectory() / "settings.json").string();
#else
      app->options.settingsPath =
          Kotonoha::EngineSettings::defaultPath("school-days");
#endif
    }
    const std::string settings = app->options.persistSettings ? app->options.settingsPath : "";
    // Physical display settings stay next to this executable, but a relocated
    // Windows build must still recognize paths selected by the older AppData
    // launcher. Only reuse the path files, never the old display settings.
    std::string legacySettings;
#if defined(_WIN32) && !defined(__ANDROID__) && !defined(KOTONOHA_DISTRIBUTION_BUILD)
    // defaultPath() uses the *current* SDL app identifier. The former v3
    // launcher stored dir.txt under SDL_GetPrefPath("Kotonoha", "school-days")
    // instead, so looking up defaultPath("school-days") cannot find it.
    if (app->options.persistSettings) {
      if (char *legacyDir = SDL_GetPrefPath("Kotonoha", "school-days")) {
        legacySettings =
            (std::filesystem::path(legacyDir) / "settings.json").string();
        SDL_free(legacyDir);
      }
    }
#endif
    
    // Route precedence is part of the launcher contract: an explicit --ktrf
    // wins, then the user's remembered selection, and only then the bundled
    // original-profile route.  Looking up the bundled route first made a valid
    // remembered custom route impossible to reuse on the next launch.
    const auto rememberedValidRoute = [](const std::string &settingsPath) {
      if (settingsPath.empty()) return std::string{};
      const auto saved = Kotonoha::GameLauncher::rememberedPath(
          settingsPath, SchoolDays::Launcher::RouteRememberFile);
      // A clean local install keeps route.txt portable. Resolve its relative
      // path beside settings.json, never against a random process CWD.
      const auto route = std::filesystem::path(saved);
      std::string info;
      if (saved.empty()) return std::string{};
      const auto root = std::filesystem::path(settingsPath).parent_path();
      const auto candidates = route.is_absolute()
          ? std::vector<std::filesystem::path>{route}
          : std::vector<std::filesystem::path>{root / route,
                                                root / "game-assets" / route};
      for (const auto &candidate : candidates) {
        const auto path = candidate.lexically_normal().string();
        if (SchoolDays::Launcher::validateRoute(path, info)) return path;
      }
      return std::string{};
    };
    if (app->route.empty()) app->route = rememberedValidRoute(settings);
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
    // A new distribution must not inherit a custom route from AppData or
    // expose a writable KTRF route override to the player.
    if (!app->documentPacks)
      app->route = SchoolDays::Assets::defaultRoute().string();
#endif
    if (app->route.empty() && !legacySettings.empty() &&
        legacySettings != settings)
      app->route = rememberedValidRoute(legacySettings);
    if (app->route.empty())
      app->route = SchoolDays::Assets::defaultRoute().string();
        
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
    if (directory.empty())
      if (const char *env = std::getenv("SCHOOL_DAYS_DIR")) directory = env;
#endif
    const auto rememberedValidDirectory = [](const std::string &settingsPath) {
      if (settingsPath.empty()) return std::string{};
      const auto saved = Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt");
      const auto directory = std::filesystem::path(saved);
      const auto path = saved.empty() ? std::string{}
          : (directory.is_absolute() ? directory
             : std::filesystem::path(settingsPath).parent_path() / directory)
                .lexically_normal().string();
      std::string info;
      return !path.empty() && SchoolDays::Assets::checkDirectory(path, info)
                 ? path : std::string{};
    };
    if (directory.empty()) directory = rememberedValidDirectory(settings);
    if (directory.empty()) {
#if defined(_WIN32) && !defined(__ANDROID__)
      // Installed game folders keep Packs beside SchoolDays.exe, while the
      // default discovery API normally starts from game-assets/. Prefer the
      // installed retail packs even if launched by a desktop shortcut whose
      // Start In folder is unrelated to the executable directory.
      std::string installedInfo;
      const auto installedRoot = Kotonoha::resourceDirectory();
      if (SchoolDays::Assets::checkDirectory(installedRoot, installedInfo))
        directory = installedRoot.string();
#endif
    }
    if (directory.empty() && !legacySettings.empty() &&
        legacySettings != settings)
      directory = rememberedValidDirectory(legacySettings);
#if defined(__ANDROID__) || defined(ANDROID)
    // A document launch supplies its own root. Legacy private installs use
    // discovery.
    if (directory.empty()) directory = SchoolDays::Assets::defaultDirectory().string();
#endif
#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
    if (directory.empty()) directory = SchoolDays::Assets::defaultDirectory().string();
#else
    // A clean distribution has no licensed Packs yet. Let GameLauncher show
    // an empty path / first-run file selection; do not insert the AppData
    // fallback as if that directory were a real game installation.
#endif
    DevStartupLog("data-directory-selected", directory);
    if (app->options.launcher) {
      // Use the same dedicated School Days launcher, not a second/parallel
      // UI. Integrated LxH is always present; the selected language controls
      // both original retail content and the appended epilogues.
      app->launcher = std::make_unique<SchoolDays::Launcher>(
          settings, directory, app->route);
      app->launcher->setRetailConfigRoot(
          app->documents.empty()
#if defined(__ANDROID__) || defined(ANDROID)
              ? Kotonoha::preferredDocumentsDirectory("school-days")
#else
              ? Kotonoha::resourceDirectory()
#endif
              : std::filesystem::path(app->documents));
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
      if (!app->launcher->pathValid())
        app->launcher->promptForOriginalGameFiles();
#endif
      if (!app->scripts.empty())
        app->launcher->setScriptOverlayPath(app->scripts);
      if (!app->options.translationPath.empty()) {
        // Explicit --translation remains authoritative over a remembered
        // language. The player can still change it in the launcher.
        app->launcher->settings().translationPath = app->options.translationPath;
        app->launcher->setLanguage(SchoolDays::Launcher::Language::CustomJson);
      }
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *appstate = app.release();
    DevStartupLog("app-init-ready");
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    DevStartupLog("app-init-error", error.what());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays initialization: %s", error.what());
#if defined(_WIN32) && !defined(__ANDROID__)
    // A double-clicked Windows build has no console to show the startup error.
    (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                   "School Days HQ initialization failed",
                                   error.what(), nullptr);
#endif
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  if (!app) return SDL_APP_FAILURE;
  try {
    if (!app->launcher && app->application &&
        (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
         event->type == SDL_EVENT_QUIT) &&
        app->application->runtime.handleNativeQuitRequest())
      return SDL_APP_CONTINUE;
    return app->launcher ? app->launcher->event(*event) : app->application->engine.Event(event);
  } catch (const std::exception &error) {
    DevStartupLog("event-error", error.what());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays event: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  if (!app) return SDL_APP_FAILURE;
  try {
    if (app->launcher) {
      app->launcher->iterate();
      if (app->launcher->state() == SchoolDays::Launcher::State::Quit) return SDL_APP_SUCCESS;
      if (app->launcher->state() == SchoolDays::Launcher::State::Launch) {
        const auto directory = app->launcher->path();
        app->route = app->launcher->routePath();
        app->options.settings = app->launcher->settings();
        // Seed --translation into the launcher when it opens, then honor the
        // player's final choice. Choosing English must be able to clear a
        // previously supplied JSON rather than loading it silently anyway.
        app->options.translationPath = app->launcher->translationPath();
        app->lxhJsonOverride = app->launcher->manualLxhJsonPath();
        app->launcherEffectiveLxh = app->launcher->effectiveLxhEnabled();
        app->launcher.reset();
        app->start(directory);
      }
      return SDL_APP_CONTINUE;
    }
    static bool firstFrameLogged = false;
    if (!firstFrameLogged) DevStartupLog("first-frame-entered");
#if defined(__ANDROID__) || defined(ANDROID)
    const Uint64 renderStartNs = SDL_GetTicksNS();
#endif
    const auto result = app->application->engine.Iterate();
#if defined(__ANDROID__) || defined(ANDROID)
    if (result == SDL_APP_CONTINUE && app->frameLimit > 0) {
      const Uint64 elapsed = SDL_GetTicksNS() - renderStartNs;
      const Uint64 targetNs = Uint64{1000000000} / app->frameLimit;
      if (elapsed < targetNs) SDL_DelayNS(targetNs - elapsed);
    }
#endif
    if (!firstFrameLogged) {
      DevStartupLog("first-frame-complete");
      firstFrameLogged = true;
    }
    if (result != SDL_APP_CONTINUE)
      DevStartupLog("engine-iterate-result", std::to_string(result));
    // The generic --frames option must behave the same in DEV and DIST.  It
    // gives headless tests a real game loop without reinstating debug APIs.
    if (app->frames > 0 && --app->frames == 0)
      return SDL_APP_SUCCESS;
    return result;
  } catch (const std::exception &error) {
    DevStartupLog("frame-error", error.what());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays frame: %s", error.what());
#if defined(_WIN32) && !defined(__ANDROID__)
    // Starting the game from the launcher happens in SDL_AppIterate, not
    // SDL_AppInit. A GUI-subsystem distribution has no visible stderr, so a
    // failed language/GPK mount otherwise looks like Play simply closed it.
    (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                   "School Days HQ could not start",
                                   error.what(), nullptr);
#endif
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult result) {
  DevStartupLog("app-quit", std::to_string(result));
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
