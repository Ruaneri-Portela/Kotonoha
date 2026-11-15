#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysAssets.hpp"
#include <Kotonoha/componentes/DisplayModeCatalog.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>
#include <Kotonoha/routing/SchoolDaysDesktopStartupProfile.hpp>
#include <Kotonoha/routing/SchoolDaysRetailConfigPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysLxhContent.hpp>
#include <Kotonoha/routing/SchoolDaysTranslationCoverage.hpp>
#include <Kotonoha/routing/SchoolDaysUpscalingPolicy.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/script/KtrfScript.hpp>

#include <SDL3/SDL.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void write(const fs::path &path, const std::string &contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
  require(bool(output), "Cannot write synthetic launcher fixture: " +
                             path.string());
}

void writeSyntheticLxhOverlay(const fs::path &overlay) {
  for (const auto ending : Kotonoha::SchoolDaysLxhContent::Endings) {
    const std::string name(ending);
    write(overlay / "Script/English/05" / (name + ".ENG.ORS"),
          "[PrintText]=00:00:00\tFixture\tSynthetic data, not game assets.\t00:01:00;\n"
          "[EndRoll]=00:01:00\tCredits\t00:02:00;\n"
          "[PlayMovie]=00:02:01\tLxH/Media/" + name +
              "-LXH\t0\t00:03:00;\n"
          "[PrintText]=00:02:04\tFixture\tExtra synthetic LxH dialogue.\t00:02:12;\n"
          "[Next]=00:04:00;\n");
    write(overlay / "LxH" / (name + "-LXH.png"),
          std::string(8, 'p'));
    write(overlay / "LxH/Media" / (name + "-LXH.wmv"),
          std::string(16, 'v'));
    write(overlay / "LxH/Media" / (name + "-LXH.ogg"),
          std::string(4, 'a'));
  }
  const auto expected = Kotonoha::SchoolDaysLxhContent::PackEntries();
  require(expected.size() == 28u,
          "The integrated LxH manifest must contain 28 resources");
  for (const auto &logical : expected)
    require(fs::is_regular_file(overlay / logical),
            "Synthetic epilogue asset missing: " + logical);
}

std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

void run(const std::vector<std::string> &arguments) {
  std::vector<const char *> argv;
  for (const auto &argument : arguments)
    argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  SDL_Process *process = SDL_CreateProcess(argv.data(), false);
  require(process != nullptr,
          std::string("Cannot start School Days: ") + SDL_GetError());
  int exitCode = -1;
  const bool waited = SDL_WaitProcess(process, true, &exitCode);
  SDL_DestroyProcess(process);
  require(waited && exitCode == 0, "School Days process failed");
}

struct TemporaryDirectory {
  fs::path path =
      fs::temp_directory_path() /
      ("school-days-launcher-" + std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    require(fs::create_directory(path), "Cannot create test directory");
  }
  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};
} // namespace

int main(int argc, char **argv) {
  try {
    // Two-way settings contract: Direct3D12 can resize with Linear but never
    // runs GLSL/ArtCNN. A preset selection switches explicitly to MPV/OpenGL
    // with a non-zero factor; selecting Direct3D12 afterward changes only
    // the backend (not the user's chosen preset) and must stay consistent.
    {
      using Policy = Kotonoha::SchoolDaysUpscalingPolicy;
      using Backend = Kotonoha::UpscaleBackend;
      using Preset = Kotonoha::UpscalePreset;
      Kotonoha::EngineSettings values;
      values.renderer = "direct3d12";
      values.upscaling.backend = Backend::Auto;
      values.upscaling.factor = 2.0f;
      values.upscaling.preset = Preset::ArtCNNLarge;
      require(!Policy::UsesShaders(values) &&
                  !Policy::IncompatibleMpvSelection(values),
              "Direct3D12+Auto mistakenly advertised GLSL shader support");
      values.upscaling.backend = Backend::MPV;
      require(Policy::IncompatibleMpvSelection(values),
              "Direct3D12+MPV impossible configuration went unnoticed");
      Policy::SelectRenderer(values, "direct3d11");
      require(values.renderer == "direct3d11" &&
                  values.upscaling.backend == Backend::Auto &&
                  values.upscaling.preset == Preset::ArtCNNLarge &&
                  !Policy::UsesShaders(values),
              "Switching away from OpenGL left MPV active or lost the preset");
      values.upscaling.factor = 1.0f;
      values.upscaling.shaderFiles = "example.glsl";
      Policy::SelectPreset(values, Preset::Anime4KModeCAFast);
      require(values.renderer == "opengl" &&
                  values.upscaling.backend == Backend::MPV &&
                  values.upscaling.factor >= 2.0f &&
                  values.upscaling.shaderFiles.empty() &&
                  values.upscaling.preset == Preset::Anime4KModeCAFast &&
                  Policy::UsesShaders(values),
              "Selecting shader preset failed to enable OpenGL MPV and scale");
      values.upscaling.factor = 1.0f;
      require(!Policy::UsesShaders(values),
              "GLSL must not be advertised active at factor 1.0");
      values.upscaling.factor = 2.0f;
      values.upscaling.backend = Backend::Linear;
      require(!Policy::UsesShaders(values),
              "Linear must not advertise MPV shader effects on OpenGL");
      Policy::SelectRenderer(values, "direct3d12");
      require(values.renderer == "direct3d12" &&
                  values.upscaling.backend == Backend::Linear &&
                  !Policy::UsesShaders(values),
              "Direct3D12 renderer switch mutated an independent Linear backend");
      values.upscaling.backend = Backend::MPV;
      Policy::SelectRenderer(values, {});
      require(values.renderer.empty() &&
                  values.upscaling.backend == Backend::Auto &&
                  !Policy::UsesShaders(values),
              "Automatic renderer kept an MPV backend without guaranteed OpenGL");
      Policy::SelectRenderer(values, "direct3d12");
      values.upscaling.backend = Backend::Auto;
      require(!Policy::UsesShaders(values),
              "Direct3D12+Auto unexpectedly gained MPV shaders");
      values.upscaling.backend = Backend::None;
      require(!Policy::UsesShaders(values),
              "Disabled backend must not advertise shader effects");
    }
    require(!SchoolDays::Launcher::statusNoticeExpired(1000, 5999) &&
                SchoolDays::Launcher::statusNoticeExpired(1000, 6000) &&
                SchoolDays::Launcher::statusNoticeExpired(1000, 9000) &&
                !SchoolDays::Launcher::statusNoticeExpired(1000, 999),
            "Launcher notification did not expire at the five-second boundary");
    require(argc == 4,
            "Usage: SchoolDaysLauncherTest route fixtures executable");
    const fs::path original = fs::absolute(argv[1]);
    const fs::path fixtures = fs::absolute(argv[2]);
    TemporaryDirectory temporary;
    // Reopen both profiles exactly as the game does after launcher OK. A
    // Direct3D12 selection must not regenerate MPV (and vice versa) simply
    // because settings.json is round-tripped through the generic serializer.
    {
      const auto file = (temporary.path / "upscale-roundtrip.json").string();
      Kotonoha::EngineSettings draft;
      draft.width = 1600;
      draft.height = 900;
      draft.renderScale = 0.75f;
      draft.masterVolume = 0.25f;
      draft.renderer = "direct3d12";
      draft.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
      draft.upscaling.factor = 1.0f;
      Kotonoha::SchoolDaysUpscalingPolicy::SelectPreset(
          draft, Kotonoha::UpscalePreset::ArtCNN);
      draft.save(file);
      auto game = Kotonoha::EngineSettings::load(file);
      require(game.renderer == "opengl" &&
                  game.upscaling.backend == Kotonoha::UpscaleBackend::MPV &&
                  game.upscaling.preset == Kotonoha::UpscalePreset::ArtCNN &&
                  game.upscaling.factor >= 2.0f &&
                  game.width == 1600 && game.height == 900 &&
                  std::abs(game.renderScale - 0.75f) < 0.001f &&
                  std::abs(game.masterVolume - 0.25f) < 0.001f,
              "Shader selection did not survive launcher -> game settings load");
      Kotonoha::SchoolDaysUpscalingPolicy::SelectRenderer(game, "direct3d12");
      game.save(file);
      const auto reopened = Kotonoha::EngineSettings::load(file);
      require(reopened.renderer == "direct3d12" &&
                  reopened.upscaling.backend == Kotonoha::UpscaleBackend::Auto &&
                  reopened.upscaling.preset == Kotonoha::UpscalePreset::ArtCNN &&
                  !Kotonoha::SchoolDaysUpscalingPolicy::UsesShaders(reopened),
              "Direct3D12 re-opened with a forbidden MPV shader backend");
    }
    const auto workspace = temporary.path / "workspace";
    const auto gameAssets = workspace / "games/school-days/game-assets";
    fs::create_directories(gameAssets / "Script/English");
    fs::copy_file(original, gameAssets / "school-days-hq.ktnroute");
    const auto previousDirectory = fs::current_path();
    fs::current_path(workspace);
    const auto discoveredGame =
        fs::absolute(SchoolDays::Assets::defaultDirectory());
    const auto discoveredRoute =
        fs::absolute(SchoolDays::Assets::findDefaultRoute());
    fs::current_path(previousDirectory);
    require(fs::is_directory(discoveredGame) &&
                fs::is_regular_file(discoveredRoute) &&
                fs::equivalent(discoveredGame, gameAssets) &&
                fs::equivalent(discoveredRoute,
                               gameAssets / "school-days-hq.ktnroute"),
            "Source game-assets directory and route were not discovered: " +
                discoveredGame.string() + ", " + discoveredRoute.string());

    const auto custom = temporary.path / "custom route.ktnroute";
    fs::copy_file(fixtures / "edited.ktnroute", custom);
    require(read(custom) != read(original),
            "Custom route fixture must differ from the default");
    const auto settings = (temporary.path / "settings.json").string();
    const auto invalid = (temporary.path / "invalid.ktnroute").string();

    // A user may reorganize installed dictionaries into translations/jsons
    // while language.txt and settings.json still reference the old flat
    // location. Rebind only the same filename, never to another locale.
    {
      const auto root = temporary.path / "json-layout";
      const auto profile = (root / "settings.json").string();
      const auto jsons = root / "translations/jsons";
      write(jsons / "es.json", "{\"0\":{\"Hello\":\"Hola\"}}");
      write(jsons / "es-lxh.json", "{\"0\":{\"Hello\":\"Hola\"}}");
      write(jsons / "zh-CN.json", "{\"0\":{\"Hello\":\"你好\"}}");
      const auto staleBase = (root / "translations/es.json").string();
      const auto staleLxh = (root / "translations/es-lxh.json").string();
      write(root / "language.txt",
            "spanish\n" + staleBase + "\nlxh-json=" + staleLxh + "\n");
      require(SchoolDays::Launcher::rememberedLocaleJsonPath(profile) ==
                  fs::absolute(jsons / "es.json").string(),
              "Old Spanish language.txt path failed to rebind into jsons");
      require(SchoolDays::Launcher::resolveStoredTranslation(profile, "es.json") ==
                  fs::absolute(jsons / "es.json").string() &&
                  SchoolDays::Launcher::resolveStoredTranslation(profile, staleBase) ==
                      fs::absolute(jsons / "es.json").string(),
              "Old JSON basename or flat path failed new folder discovery");
      require(SchoolDays::Launcher::rememberedLxhJsonPath(
                  profile, (jsons / "es.json").string()) ==
                  fs::absolute(jsons / "es-lxh.json").string(),
              "Old Spanish LxH sidecar path failed new folder discovery");
      require(SchoolDays::Launcher::resolveStoredTranslation(
                  profile, "missing-other-locale.json") ==
                  "missing-other-locale.json",
              "Missing translated language silently fell back to another JSON");
    }

    // Synthetic, no-display policy: the monitor's native mode seeds only a
    // fresh launcher profile. Persisted choices always survive restart.
    {
      Kotonoha::EngineSettings first;
      require(SchoolDays::Launcher::initializeFirstRunDisplay(
                  first, false, 2560, 1440, 144.0f) &&
                  first.width == 2560 && first.height == 1440 &&
                  first.fullscreenWidth == 2560 &&
                  first.fullscreenHeight == 1440 &&
                  std::abs(first.refreshRate - 144.f) < 0.001f,
              "Launcher initial monitor auto-detection ignored desktop mode");
      const auto original = first;
      require(!SchoolDays::Launcher::initializeFirstRunDisplay(
                  first, true, 1920, 1080, 60.0f) &&
                  first.width == original.width &&
                  first.fullscreenWidth == original.fullscreenWidth &&
                  first.refreshRate == original.refreshRate,
              "Launcher startup overwrote a stored/manual resolution");

      SDL_DisplayMode native{};
      native.w = 1920;
      native.h = 1080;
      native.refresh_rate = 60.0f;
      first.mode = Kotonoha::WindowMode::Borderless;
      require(SchoolDays::Launcher::selectPhysicalResolution(
                  first, {1600, 900, 120.0f}, &native) &&
                  first.mode == Kotonoha::WindowMode::Exclusive &&
                  first.fullscreenWidth == 1600 &&
                  first.fullscreenHeight == 900 &&
                  std::abs(first.refreshRate - 120.f) < 0.001f,
              "Manual non-native borderless resolution did not become exclusive");
      first.mode = Kotonoha::WindowMode::Windowed;
      require(SchoolDays::Launcher::selectPhysicalResolution(
                  first, {1280, 720, 144.0f}, &native) &&
                  first.width == 1280 && first.height == 720 &&
                  std::abs(first.refreshRate - 60.f) < 0.001f,
              "Windowed size request persisted a fictitious monitor refresh");
      first.validate();
    }

    Kotonoha::ScriptNode end;
    end.id = 0;
    end.type = Kotonoha::NodeType::End;
    Kotonoha::KtrfScript{{end}, Kotonoha::ScriptMode::Standard, 0}.dump(
        invalid);

    {
      SchoolDays::Launcher launcher(
          settings, fixtures.string(),
          (temporary.path / "missing.ktnroute").string());
      require(launcher.pathValid() && !launcher.routeValid(),
              "Missing route was accepted");
      require(!launcher.launch() && !fs::exists(settings),
              "Missing route must block Play");
      launcher.setRoutePath(invalid);
      require(!launcher.routeValid() && !launcher.launch(),
              "Other KTRF profiles must be rejected");
      launcher.setRoutePath(custom.string());
      require(launcher.routeValid(), launcher.routeInfo());
      launcher.open();
      launcher.iterate();
      launcher.iterate();
      require(launcher.pathValid() && launcher.routeValid(),
              "Launcher frames must keep the selected game and route");
      require(launcher.launch() &&
                  launcher.state() == Kotonoha::Launcher::State::Launch,
              "Valid route and game directory must launch");
      launcher.close();
    }

    SchoolDays::Launcher reopened(settings, "", "");
    require(reopened.path() == fixtures.string() && reopened.pathValid(),
            "The game directory was not remembered");
    require(reopened.routePath() == custom.string() && reopened.routeValid(),
            "The selected route was not remembered");
    SchoolDays::Launcher explicitRoute(settings, fixtures.string(),
                                       original.string());
    require(explicitRoute.routePath() == original.string() &&
                explicitRoute.routeValid(),
            "An explicit route must override the remembered route");

    const auto documents = (temporary.path / "documents").string();
    {
      const auto root = temporary.path / "new-launcher-profile";
      fs::create_directory(root);
      const auto enginePath = (root / "settings.json").string();
      SchoolDays::Launcher fresh(enginePath, fixtures.string(), original.string());
      fresh.setRetailConfigRoot(root);
      require(!fs::exists(root / "Config.DAT"),
              "Opening a fresh launcher profile created Config.DAT before Play");
      require(fresh.launch(), fresh.getStatus());
      require(fs::is_regular_file(root / "Config.DAT"),
              "Play did not initialize a fresh Config.DAT");
    }

    // An explicitly owned Config.DAT is the only source of retail General /
    // Sound settings. Validate read-only construction, delayed DFLT flush,
    // unknown-line retention, authored aspect and shared EngineSettings.
    {
      using Storage = Kotonoha::SchoolDaysConfigStorage;
      using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
      const auto root = temporary.path / "retail-config-launcher";
      const auto retailPath = root / "Config.DAT";
      const auto enginePath = (root / "settings.json").string();
      const std::string originalRetail =
          "< Config.dat >\n"
          "[VoiceVolume]=\"3\"\n"
          "[BgmVolume]=\"4\"\n"
          "[SeVolume]=\"5\"\n"
          "[MenVoice]=\"0\"\n"
          "[Mute]=\"0\"\n"
          "[WindowMode]=\"1\"\n"
          "[MasterVolume]=\"-7.000000\"\n"
          "[OpaqueFuture]=\"preserve-this\"\n";
      std::vector<uint8_t> encoded;
      std::string failure;
      require(Storage::EncodeDocument(originalRetail, &encoded, &failure),
              "Cannot encode synthetic retail Config.DAT: " + failure);
      write(retailPath, std::string(encoded.begin(), encoded.end()));
      {
        SchoolDays::Launcher coupled(enginePath, fixtures.string(),
                                     original.string());
        require(!coupled.hasRetailConfigRoot(),
                "Launcher must not infer commercial Config.DAT root");
        coupled.setRetailConfigRoot(root);
        require(coupled.hasRetailConfigRoot() &&
                    coupled.retailSettings().voiceVolume == 3 &&
                    coupled.retailSettings().bgmVolume == 4 &&
                    coupled.retailSettings().seVolume == 5 &&
                    !coupled.retailSettings().menVoice &&
                    !coupled.retailSettings().mute,
                "Launcher failed to read selected retail Sound/General state");
        require(std::abs(coupled.settings().channelVolumes["BGM"] -
                         Policy::LinearGain(coupled.retailSettings(),
                                            Policy::NativeAudioBank::Bgm)) < 0.0001f,
                "Launcher mixer did not match native retail dB curve");
        auto modified = coupled.retailSettings();
        modified.voiceVolume = 8;
        modified.bgmVolume = 9;
        modified.seVolume = 2;
        modified.menVoice = true;
        modified.mute = true;
        modified.skip = true;
        modified.superSkip = true;
        coupled.setRetailSettings(modified);
        coupled.setContentAspectPreference(2);
        coupled.settings().mode = Kotonoha::WindowMode::Exclusive;
        coupled.settings().fullscreenWidth = 1600;
        coupled.settings().fullscreenHeight = 900;
        coupled.settings().refreshRate = 75.0f;
        require(read(retailPath) ==
                    std::string(encoded.begin(), encoded.end()) &&
                    !fs::exists(root / "Kotonoha.ini"),
                "Launcher wrote Config.DAT or aspect preference before Play");
        require(coupled.launch(), coupled.getStatus());
        require(coupled.settings().mode == Kotonoha::WindowMode::Exclusive &&
                    coupled.settings().masterMuted,
                "Launcher Play changed selected mode or sound mute");
      }
      const auto flushedBytes = read(retailPath);
      const std::vector<uint8_t> flushed(flushedBytes.begin(), flushedBytes.end());
      std::string decoded;
      require(Storage::DecodeDocument(flushed, &decoded, &failure) &&
                  decoded.find("[VoiceVolume]=\"8\"") != std::string::npos &&
                  decoded.find("[BgmVolume]=\"9\"") != std::string::npos &&
                  decoded.find("[SeVolume]=\"2\"") != std::string::npos &&
                  decoded.find("[MenVoice]=\"1\"") != std::string::npos &&
                  decoded.find("[Mute]=\"1\"") != std::string::npos &&
                  decoded.find("[Skip]=\"1\"") != std::string::npos &&
                  decoded.find("[SuperSkip]=\"1\"") != std::string::npos &&
                  decoded.find("[WindowMode]=\"1\"") != std::string::npos &&
                  decoded.find("[DisplayType]=\"1\"") != std::string::npos &&
                  decoded.find("[OpaqueFuture]=\"preserve-this\"") !=
                      std::string::npos &&
                  decoded.find("[MasterVolume]=\"-7.000000\"") !=
                      std::string::npos,
              "Retail General/Sound cache lost settings or opaque lines");
      require(read(root / "Kotonoha.ini").find("ContentAspectPreference=2") !=
                  std::string::npos,
              "Launcher did not persist in-game aspect sidecar");
      const auto stored = Kotonoha::EngineSettings::load(enginePath);
      require(stored.mode == Kotonoha::WindowMode::Exclusive &&
                  stored.fullscreenWidth == 1600 &&
                  stored.fullscreenHeight == 900 &&
                  std::abs(stored.refreshRate - 75.f) < 0.01f &&
                  stored.masterMuted,
              "Launcher physical EngineSettings did not round-trip on Play");
      SchoolDays::Launcher reopenedRetail(enginePath, fixtures.string(),
                                          original.string());
      reopenedRetail.setRetailConfigRoot(root);
      require(reopenedRetail.retailSettings().bgmVolume == 9 &&
                  reopenedRetail.retailSettings().voiceVolume == 8 &&
                  reopenedRetail.retailSettings().menVoice &&
                  reopenedRetail.contentAspectPreference() == 2 &&
                  reopenedRetail.settings().fullscreenWidth == 1600,
              "Launcher reopened the same settings.json/Config.DAT inconsistently");

      // Emulate a subsequent in-game save, then open a new launcher in the
      // same process. Play should carry those manual choices forward.
      auto savedInGame = stored;
      savedInGame.fullscreenWidth = 1920;
      savedInGame.fullscreenHeight = 1080;
      savedInGame.refreshRate = 119.88f;
      savedInGame.vsync = false;
      savedInGame.upscaling.backend = Kotonoha::UpscaleBackend::Linear;
      savedInGame.upscaling.factor = 2.5f;
      savedInGame.save(enginePath);
      Storage gameStorage;
      Storage::LoadResult gameConfig;
      require(gameStorage.Initialize(root, &failure) &&
                  gameStorage.Load(&gameConfig, &failure),
              "Cannot load synthetic game profile: " + failure);
      gameConfig.state.voiceVolume = 6;
      gameConfig.state.mute = false;
      require(gameStorage.CacheState(gameConfig.state, &failure) &&
                  gameStorage.Flush(&failure),
              "Cannot write synthetic in-game Config.DAT: " + failure);
      write(root / "Kotonoha.ini",
            "[KotonohaConfig][Settings]\nContentAspectPreference=1\n");

      SchoolDays::Launcher afterGame(enginePath, fixtures.string(),
                                     original.string());
      afterGame.setRetailConfigRoot(root);
      const auto hasGameDisplay = [](const Kotonoha::EngineSettings &settings) {
        return settings.mode == Kotonoha::WindowMode::Exclusive &&
               settings.fullscreenWidth == 1920 &&
               settings.fullscreenHeight == 1080 &&
               std::abs(settings.refreshRate - 119.88f) < 0.01f &&
               !settings.vsync &&
               settings.upscaling.backend == Kotonoha::UpscaleBackend::Linear &&
               std::abs(settings.upscaling.factor - 2.5f) < 0.01f;
      };
      require(hasGameDisplay(afterGame.settings()) &&
                  afterGame.retailSettings().voiceVolume == 6 &&
                  !afterGame.settings().masterMuted &&
                  afterGame.contentAspectPreference() == 1,
              "Reopened launcher did not read the latest in-game preferences");
      require(afterGame.launch(), afterGame.getStatus());
      require(hasGameDisplay(Kotonoha::EngineSettings::load(enginePath)),
              "Play replaced manual display preferences saved by the game");

      // Another session may change the same authored aspect or retail file
      // while the launcher is open; no partial settings.json must be saved.
      const auto conflictSettings = (root / "conflict-settings.json").string();
      SchoolDays::Launcher conflict(conflictSettings, fixtures.string(),
                                    original.string());
      conflict.setRetailConfigRoot(root);
      conflict.setContentAspectPreference(2);
      write(root / "Kotonoha.ini",
            "[KotonohaConfig][Settings]\nContentAspectPreference=0\n");
      require(!conflict.launch() && !fs::exists(conflictSettings),
              "Concurrent aspect change was not blocked before saving settings");
      const auto configConflictSettings =
          (root / "config-conflict-settings.json").string();
      SchoolDays::Launcher configConflict(configConflictSettings, fixtures.string(),
                                          original.string());
      configConflict.setRetailConfigRoot(root);
      write(retailPath, "externally modified retail Config.DAT\n");
      require(!configConflict.launch() && !fs::exists(configConflictSettings),
              "Concurrent Config.DAT change was not blocked before saving settings");
    }

    // The generic serializer stores portable GLSL basenames. Preserve exact
    // paths in the School Days profile, never in the engine-owned JSON format.
    {
      const auto root = temporary.path / "shader-profile";
      const auto shaderA = root / "external" / "custom-a.glsl";
      const auto shaderB = root / "other" / "custom-b.glsl";
      write(shaderA, "//!HOOK MAIN\n");
      write(shaderB, "//!HOOK MAIN\n");
      const auto settingsFile = (root / "settings.json").string();
      const std::string paths =
          Kotonoha::UpscaleShaderFileList(
              {shaderA.string(), shaderB.string()});
      std::string failure;
      require(SchoolDays::Launcher::persistShaderPaths(settingsFile, paths,
                                                        &failure),
              "Failed to persist full GLSL references: " + failure);
      Kotonoha::EngineSettings initial;
      initial.upscaling.shaderFiles = paths;
      initial.renderer = "software";
      initial.save(settingsFile);
      const auto portable =
          Kotonoha::EngineSettings::load(settingsFile).upscaling.shaderFiles;
      require(portable != paths, "Synthetic GLSL fixture did not exercise basename loss");
      require(SchoolDays::Launcher::resolveStoredShaderPaths(settingsFile,
                                                             portable) == paths,
              "Game-owned shader sidecar failed to restore both exact paths");
      SchoolDays::Launcher shaderReopened(settingsFile, fixtures.string(),
                                          original.string());
      require(shaderReopened.settings().upscaling.shaderFiles == paths,
              "Launcher did not restore full custom GLSL path on restart");
      require(shaderReopened.settings().renderer == "software",
              "Launcher overwrote an explicit renderer selection");
      shaderReopened.settings().renderer = "opengl";
      require(shaderReopened.launch(), shaderReopened.getStatus());
      require(Kotonoha::EngineSettings::load(settingsFile).renderer == "opengl",
              "Launcher renderer choice did not persist to settings.json");
      write(root / "shader-paths.txt",
            "SchoolDaysShaderPaths.v1\nC:/unrelated/custom-x.glsl\n");
      require(SchoolDays::Launcher::resolveStoredShaderPaths(settingsFile,
                                                             portable) == portable,
              "Stale GLSL sidecar must fall back to settings.json basenames");
      write(root / "shader-paths.txt", "CORRUPTED_VERSION\n");
      require(SchoolDays::Launcher::resolveStoredShaderPaths(settingsFile,
                                                             portable) == portable,
              "Corrupt GLSL sidecar must not prevent launcher startup");
      require(SchoolDays::Launcher::persistShaderPaths(settingsFile, paths,
                                                        &failure),
              "Could not restore valid shader sidecar");
      fs::remove(shaderB);
      require(SchoolDays::Launcher::resolveStoredShaderPaths(settingsFile,
                                                             portable) == portable,
              "Missing absolute GLSL must fall back to portable settings");
      require(SchoolDays::Launcher::persistShaderPaths(settingsFile, "",
                                                        &failure) &&
                  SchoolDays::Launcher::resolveStoredShaderPaths(settingsFile,
                                                                 "") == "",
              "Clearing GLSL paths resurrected an old external shader");
    }
    const auto languageRoot = temporary.path / "locale-workspace";
    fs::create_directories(languageRoot);
    const auto languageSettings = (languageRoot / "settings.json").string();
    // Exercise the existing School Days-specific launcher, not a separate
    // language UI. All content below is synthetic (no retail translated text).
    const auto spanish = temporary.path / "spanish-synthetic.json";
    const auto incompleteSpanish = temporary.path / "spanish-INCOMPLETE.json";
    std::ofstream(incompleteSpanish, std::ios::binary | std::ios::trunc)
        << "{\"nodo\":{\"Hello\":\"Hola\"}}\n";
    {
      // Build a COMPLETE synthetic translation of the generated test ORS.
      // This is not a commercial/community localization and stays in the
      // temporary fixture; every key is uniquely audited by KTRF NODE.
      SchoolDays::Assets fixtureAssets(fixtures);
      const auto sources = Kotonoha::SchoolDaysTranslationGate::Collect(
          fixtureAssets, original.string());
      Kotonoha::Translation testTranslation;
      for (const auto &[node, lines] : sources) {
        auto &dictionary = testTranslation.mutableSections()[std::to_string(node)];
        for (const auto &line : lines)
          if (!line.empty() && dictionary.find(line) == dictionary.end())
            dictionary[line] = "Texto español " +
                               std::to_string(dictionary.size() + 1u);
      }
      testTranslation.dump(spanish.string(), sources);
      require(Kotonoha::SchoolDaysTranslationGate::Check(
                  sources, testTranslation).certifiedForNonNativeDisplay(),
              "Complete synthetic launcher language fixture was not complete");
    }
    {
      SchoolDays::Launcher localized(languageSettings, fixtures.string(),
                                     original.string());
      // No commercial display-options overlay exists in the synthetic test
      // executable folder. Use the fixture's own complete loose ORS tree.
      localized.setScriptOverlayPath(fixtures.string());
      localized.setSpanishJsonPath(incompleteSpanish.string());
      require(!localized.launch(),
              "Launcher accepted a Spanish JSON that leaves English fallbacks");
      localized.setSpanishJsonPath(spanish.string());
      require(localized.language() == SchoolDays::Launcher::Language::Spanish &&
                  localized.translationPath() == spanish.string(),
              "School Days launcher did not select Spanish JSON");
      require(localized.launch(), localized.getStatus());
      require(read(languageRoot / "language.txt") ==
                      "spanish\n" + spanish.string() + "\n",
              "Dedicated launcher did not persist Spanish language selection");
      require(!fs::exists(fs::path(documents) / "LxH.mode") &&
                  !fs::exists(languageRoot / "LxH.mode") &&
                  !fs::exists(languageRoot / "LxH.overlay"),
              "Launcher must not recreate legacy LxH mode/overlay preferences");
    }
    {
      SchoolDays::Launcher reopenedLanguage(languageSettings, fixtures.string(),
                                            original.string());
      reopenedLanguage.setScriptOverlayPath(fixtures.string());
      require(reopenedLanguage.language() == SchoolDays::Launcher::Language::Spanish &&
                  reopenedLanguage.translationPath() == spanish.string(),
              "School Days launcher lost language preference on restart: lang=" +
                  std::to_string(static_cast<int>(reopenedLanguage.language())) +
                  " translation=" + reopenedLanguage.translationPath() +
                  " expected=" + spanish.string());
      reopenedLanguage.setLanguage(SchoolDays::Launcher::Language::English);
      require(reopenedLanguage.translationPath().empty() &&
                  reopenedLanguage.language() == SchoolDays::Launcher::Language::English,
              "Native English selection left a stale Spanish JSON active");
      require(reopenedLanguage.launch(), reopenedLanguage.getStatus());
      require(read(languageRoot / "language.txt") == "english\n",
              "Launcher did not persist original English");
      require(!fs::exists(languageRoot / "LxH.mode") &&
                  !fs::exists(languageRoot / "LxH.overlay"),
              "English launch recreated the deprecated LxH settings");
    }
    // Validate the same native ORS graft and locale layers as Application,
    // before Play persists preferences or closes the launcher. Media and ORS
    // here are synthetic; no retail files are used or installed.
    const auto lxhOverlay = temporary.path / "synthetic-lxh-overlay";
    writeSyntheticLxhOverlay(lxhOverlay);
    const auto layeredRoot = temporary.path / "layered-locales";
    fs::create_directories(layeredRoot);
    const auto layeredSettings = (layeredRoot / "settings.json").string();
    // Pre-migration preferences must not turn permanent epilogues off or be
    // rewritten. The synthetic integration source below is complete.
    write(layeredRoot / "LxH.mode", "off\n");
    write(layeredRoot / "LxH.overlay", "legacy-selection-is-ignored\n");
    const auto esBasePath = layeredRoot / "es.json";
    const auto esDlcPath = layeredRoot / "es-lxh.json";
    const auto frBasePath = layeredRoot / "fr.json";
    const auto frDlcPath = layeredRoot / "fr-lxh.json";
    const auto zhBasePath = layeredRoot / "zh-CN.json";
    const auto zhDlcPath = layeredRoot / "zh-CN-lxh.json";
    Kotonoha::Translation esBase, esDlc, frBase, frDlc, zhBase, zhDlc;
    Kotonoha::SchoolDaysTranslationGate::SourceTexts esSources, frSources,
        esDlcSources, frDlcSources, zhSources, zhDlcSources;
    {
      SchoolDays::Assets layeredAssets(fixtures, lxhOverlay.string());
      require(!layeredAssets.resolve("LxH/05-A2-F00-LXH.png").empty(),
              "Complete synthetic epilogue assets were not resolved");
      const auto slots = Kotonoha::SchoolDaysTranslationGate::CollectDetailed(
          layeredAssets, original.string());
      std::size_t lxhCount = 0;
      for (const auto &slot : slots) {
        const auto node = std::to_string(slot.node);
        esSources[slot.node].push_back(slot.original);
        esBase.mutableSections()[node][slot.original] =
            slot.lxh ? "Diálogo español V4b." : "Diálogo español HQ.";
        if (slot.lxh) {
          ++lxhCount;
          const auto key = Kotonoha::SchoolDaysTranslationGate::ContextKey(
              slot.eventOrdinal, slot.slot);
          esDlc.mutableSections()[node][key] = "Diálogo español V4b.";
          esDlcSources[slot.node].push_back(key);
          frDlc.mutableSections()[node][key] = "Épilogue français.";
          frDlcSources[slot.node].push_back(key);
        } else {
          frBase.mutableSections()[node][slot.original] = "Dialogue français HQ.";
          frSources[slot.node].push_back(slot.original);
        }
        zhBase.mutableSections()[node][slot.original] = "中文主线";
        zhSources[slot.node].push_back(slot.original);
        if (slot.lxh) {
          const auto key = Kotonoha::SchoolDaysTranslationGate::ContextKey(
              slot.eventOrdinal, slot.slot);
          zhDlc.mutableSections()[node][key] = "中文结局";
          zhDlcSources[slot.node].push_back(key);
        }
      }
      require(lxhCount == 7,
              "Synthetic overlay must add one LxH PrintText per ending");
    }
    esBase.dump(esBasePath.string(), esSources);
    esDlc.dump(esDlcPath.string(), esDlcSources);
    frBase.dump(frBasePath.string(), frSources);
    zhBase.dump(zhBasePath.string(), zhSources);
    zhDlc.dump(zhDlcPath.string(), zhDlcSources);
    const auto chineseRoot = temporary.path / "chinese-locale-profile";
    fs::create_directories(chineseRoot);
    const auto chineseSettings = (chineseRoot / "settings.json").string();
    {
      // A preset should pair installed zh-CN.json with zh-CN-lxh.json,
      // including the exact case on platforms with case-sensitive paths.
      // Explicit LxH selection is persisted alongside the base profile;
      // direct game start must read the same pair, never an unrelated locale.
      SchoolDays::Launcher chinese(chineseSettings, fixtures.string(),
                                   original.string());
      chinese.setScriptOverlayPath(lxhOverlay.string());
      chinese.setChineseJsonPath(zhBasePath.string());
      require(chinese.language() == SchoolDays::Launcher::Language::ChineseSimplified &&
                  chinese.translationPath() == zhBasePath.string() &&
                  fs::equivalent(chinese.lxhJsonPath(), zhDlcPath),
              "Chinese HQ JSON did not automatically pair with zh-CN LxH JSON");
      chinese.setLxhJsonPath(zhDlcPath.string());
      require(chinese.manualLxhJsonPath() == zhDlcPath.string(),
              "Explicit Chinese LxH choice was not accepted");
      require(chinese.launch(), chinese.getStatus());
      require(read(chineseRoot / "language.txt") ==
                  "chinese\n" + zhBasePath.string() + "\nlxh-json=" +
                      zhDlcPath.string() + "\n",
              "Chinese base/LxH choices were not persisted together");
      require(SchoolDays::Launcher::rememberedLocaleJsonPath(chineseSettings) ==
                  zhBasePath.string() &&
                  SchoolDays::Launcher::rememberedLxhJsonPath(
                      chineseSettings, zhBasePath.string()) == zhDlcPath.string() &&
                  SchoolDays::Launcher::rememberedLxhJsonPath(
                      chineseSettings, frBasePath.string()).empty(),
              "Direct startup reused Chinese epilogue JSON with an unrelated base");
    }
    {
      SchoolDays::Launcher reopened(chineseSettings, fixtures.string(),
                                    original.string());
      reopened.setScriptOverlayPath(lxhOverlay.string());
      require(reopened.language() == SchoolDays::Launcher::Language::ChineseSimplified &&
                  reopened.translationPath() == zhBasePath.string() &&
                  reopened.manualLxhJsonPath() == zhDlcPath.string(),
              "Launcher did not restore Chinese base and manual LxH together");
      reopened.setLxhJsonPath({});
      require(reopened.manualLxhJsonPath().empty() &&
                  fs::equivalent(reopened.lxhJsonPath(), zhDlcPath),
              "Reset LxH to automatic did not discover its matching zh-CN file");
      reopened.setLanguage(SchoolDays::Launcher::Language::English);
      require(reopened.translationPath().empty() &&
                  reopened.manualLxhJsonPath().empty(),
              "Switching to English retained the previous Chinese JSON override");
      reopened.setChineseJsonPath(zhBasePath.string());
      require(reopened.launch(), reopened.getStatus());
      require(read(chineseRoot / "language.txt") ==
                  "chinese\n" + zhBasePath.string() + "\n",
              "Automatic LxH resolution persisted a stale manual override");
    }
    {
      SchoolDays::Launcher layered(layeredSettings, fixtures.string(),
                                   original.string());
      layered.setScriptOverlayPath(lxhOverlay.string());
      layered.setLanguage(SchoolDays::Launcher::Language::English);
      require(!layered.launch() &&
                  layered.getStatus().find("INCOMPLETE") != std::string::npos,
              "Native English allowed integrated LxH without its English JSON");
      require(read(layeredRoot / "LxH.mode") == "off\n" &&
                  read(layeredRoot / "LxH.overlay") ==
                      "legacy-selection-is-ignored\n",
              "The launcher must ignore and preserve obsolete LxH preferences");
      require(!fs::exists(layeredSettings),
              "Rejected English Play must not persist launcher settings");

      // The non-native third locale must supply both complete HQ and LxH
      // dictionaries. A complete base alone may not reuse Spanish ORS text.
      layered.setSpanishJsonPath(frBasePath.string());
      layered.setLanguage(SchoolDays::Launcher::Language::CustomJson);
      const bool frWithoutDlcLaunched = layered.launch();
      require(!frWithoutDlcLaunched &&
                  layered.getStatus().find("fr-lxh.json") != std::string::npos,
              "Custom locale missing sidecar was not refused for that reason "
              "(launched=" + std::to_string(frWithoutDlcLaunched) +
              ", status=" + layered.getStatus() + ")");

      // A future locale must keep an independent DLC layer, EVEN IF a custom
      // base JSON happens to include translations for all grafted ORS slots.
      // The Gate permits base fallback for the explicit Spanish V4b exception;
      // the launch policy must not silently extend that exception to FR.
      for (const auto &[node, keys] : frDlcSources) {
        for (const auto &key : keys) {
          frBase.mutableSections()[std::to_string(node)][key] =
              "Épilogue français.";
          frSources[node].push_back(key);
        }
      }
      frBase.dump(frBasePath.string(), frSources);
      const bool frBaseOnlyLaunched = layered.launch();
      require(!frBaseOnlyLaunched &&
                  layered.getStatus().find("fr-lxh.json") != std::string::npos,
              "Custom locale with DLC entries in HQ JSON bypassed required "
              "independent sidecar (launched=" +
              std::to_string(frBaseOnlyLaunched) +
              ", status=" + layered.getStatus() + ")");
      frDlc.dump(frDlcPath.string(), frDlcSources);
      require(layered.launch(), layered.getStatus());
      require(read(layeredRoot / "LxH.mode") == "off\n" &&
                  read(layeredRoot / "LxH.overlay") ==
                      "legacy-selection-is-ignored\n",
              "Launching an integrated locale touched obsolete LxH settings");
    }
    {
      SchoolDays::Launcher layered(layeredSettings, fixtures.string(),
                                   original.string());
      layered.setScriptOverlayPath(lxhOverlay.string());
      layered.setSpanishJsonPath(esBasePath.string());
      require(layered.launch(),
              "Equivalent Spanish V4b sidecar blocked Play: " + layered.getStatus());
      const auto firstNode = esDlcSources.begin()->first;
      const auto firstKey = esDlcSources.begin()->second.front();
      esDlc.mutableSections()[std::to_string(firstNode)][firstKey] =
          "La DLC revirtió una corrección existente.";
      esDlc.dump(esDlcPath.string(), esDlcSources);
      require(!layered.launch() &&
                  layered.getStatus().find("conflicts") != std::string::npos,
              "Launcher accepted Spanish DLC overriding an approved V4b line");
      esDlc.mutableSections()[std::to_string(firstNode)][firstKey] =
          "Diálogo español V4b.";
      esDlc.dump(esDlcPath.string(), esDlcSources);
      require(layered.launch(),
              "Integrated LxH with equivalent Spanish JSON failed: " +
                  layered.getStatus());
    }
    const auto rememberedDump =
        (temporary.path / "remembered.ktnroute").string();
    const auto explicitDump = (temporary.path / "explicit.ktnroute").string();
    const std::vector<std::string> common = {argv[3],
                                             "--school-days",
                                             fixtures.string(),
                                             "--config",
                                             settings,
                                             "--documents",
                                             documents,
                                             "--renderer",
                                             "software",
                                             "--frames",
                                             "4"};
    auto command = common;
    command.insert(command.end(), {"--dump-ktrf", rememberedDump});
    run(command);
    require(read(rememberedDump) == read(custom),
            "Starting without the launcher did not use the remembered route");
    command = common;
    command.insert(command.end(),
                   {"--ktrf", original.string(), "--dump-ktrf", explicitDump});
    run(command);
    require(read(explicitDump) == read(original),
            "The command-line route did not override the remembered route");
    const auto defaultDump = (temporary.path / "default.ktnroute").string();
    command = {argv[3],       "--school-days", fixtures.string(),
               "--no-config", "--documents",   documents,
               "--renderer",  "software",      "--frames",
               "4",           "--dump-ktrf",   defaultDump};
    run(command);
    require(read(defaultDump) == read(original),
            "Disabling configuration must use the bundled default route");

    std::ofstream(custom, std::ios::binary | std::ios::trunc) << "corrupt";
    require(!reopened.launch() &&
                reopened.getStatus().find("Invalid School Days route") !=
                    std::string::npos,
            "A route changed after selection must be checked again");
    reopened.setRoutePath("");
    require(!reopened.routeValid(), "An empty route must be rejected");
    std::cout << "School Days launcher route selection passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysLauncherTest: " << error.what() << '\n';
    return 1;
  }
}
