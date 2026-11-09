#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "SchoolDaysRuntimeConfigStorageTest: " << message << '\n';
  std::exit(2);
}

void Check(bool value, const std::string &message) {
  if (!value)
    Fail(message);
}

std::vector<uint8_t> ReadBytes(const fs::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  Check(bool(file), "cannot read " + path.string());
  const auto end = file.tellg();
  Check(end >= 0, "invalid file size for " + path.string());
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  file.seekg(0);
  if (!bytes.empty())
    Check(bool(file.read(reinterpret_cast<char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()))),
          "read failed for " + path.string());
  return bytes;
}

void WriteBytes(const fs::path &path, const std::vector<uint8_t> &bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  Check(bool(file), "write failed for " + path.string());
}

struct TemporaryDirectory {
  fs::path path =
      fs::temp_directory_path() / ("school-days-runtime-config-" +
                                   std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Check(fs::create_directory(path), "cannot create temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};

Kotonoha::EngineOptions
Options(const std::shared_ptr<SchoolDays::Assets> &assets) {
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  return options;
}

struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;

  Application(const std::shared_ptr<SchoolDays::Assets> &assets,
              const char *route, const fs::path &documents,
              bool persistPhysicalSettings = false)
      : runtime(assets, route, documents), engine(runtime, [&] {
          auto options = Options(assets);
          if (persistPhysicalSettings) {
            options.persistSettings = true;
            options.settingsPath = (documents / "settings.json").string();
          }
          return options;
        }()) {}
  ~Application() { runtime.shutdown(); }
};

const std::string kLogical = "< Config.dat >\n"
                             "[DisplayType]=\"1\"\n"
                             "[TypeMiniNote]=\"0\"\n"
                             "[WindowMode]=\"1\"\n"
                             "[MasterVolume]=\"-7.000000\"\n"
                             "[VoiceVolume]=\"8\"\n"
                             "[BgmVolume]=\"6\"\n"
                             "[SeVolume]=\"4\"\n"
                             "[TextView]=\"1\"\n"
                             "[MenVoice]=\"0\"\n"
                             "[Mute]=\"1\"\n"
                             "[Skip]=\"1\"\n"
                             "[AutoDraw]=\"0\"\n"
                             "[AutoMode]=\"1\"\n"
                             "[SuperSkip]=\"0\"\n"
                             "Skip]=\"malformed-opaque\"\n"
                             "[OpaqueFuture]=\"keep-me\"\n";

} // namespace

namespace SchoolDays {

class RuntimeConfigStorageTestAccess {
public:
  static const Kotonoha::SchoolDaysConfigStorage &
  Storage(const Runtime &runtime) {
    return runtime.configStorage;
  }

  static void Apply(Runtime &runtime,
                    Kotonoha::SchoolDaysConfigMenuModel::ActionKind kind,
                    int value = 0, int soundBank = -1, int choice = -1) {
    Kotonoha::SchoolDaysConfigMenuModel::Action action;
    action.kind = kind;
    action.value = value;
    action.soundBank = soundBank;
    action.choice = choice;
    runtime.handleConfig(action);
  }

  static void SetPlaybackSpeed(Runtime &runtime, int index) {
    Kotonoha::SchoolDaysMenuBarPresenter::Action action;
    action.kind =
        Kotonoha::SchoolDaysMenuBarPresenter::ActionKind::SetPlaybackSpeed;
    action.value = index;
    runtime.activateMenuAction(action);
  }

  static void AddActivePlayer(Runtime &runtime, Kotonoha_orsEvent &event,
                              double rate) {
    Runtime::Playing item;
    item.player =
        std::make_unique<Kotonoha::MediaPlayer>(runtime.engine->GetAudio());
    item.player->setPlaybackRate(rate);
    item.event = &event;
    runtime.playing.push_back(std::move(item));
  }

  static int SpeedIndex(const Runtime &runtime) { return runtime.speedIndex; }
  static double Speed(const Runtime &runtime) { return runtime.speed; }
  static const Kotonoha::SchoolDaysDisplayPolicy::RetailBackendState &
  DisplayBackend(const Runtime &runtime) {
    return runtime.displayBackend;
  }
  static const Kotonoha::SchoolDaysDisplayPolicy::RetailState &
  DisplayState(const Runtime &runtime) {
    return runtime.displayState;
  }
  static Kotonoha::SchoolDaysDisplayPolicy::Variant
  UiVariant(const Runtime &runtime) {
    return runtime.uiVariant;
  }
  static Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState
  VisualState(const Runtime &runtime) {
    return runtime.configVisualState();
  }
  static double FirstPlayerRate(const Runtime &runtime) {
    return runtime.playing.empty()
               ? -1.0
               : runtime.playing.front().player->playbackRate();
  }
};

} // namespace SchoolDays

int main(int argc, char **argv) {
  Check(argc == 3,
        "usage: SchoolDaysRuntimeConfigStorageTest <route> <fixture-root>");
  using Storage = Kotonoha::SchoolDaysConfigStorage;
  using Kind = Kotonoha::SchoolDaysConfigMenuModel::ActionKind;

  TemporaryDirectory temporary;
  auto assets = std::make_shared<SchoolDays::Assets>(argv[2]);
  std::string error;
  std::vector<uint8_t> encoded;
  Check(Storage::EncodeDocument(kLogical, &encoded, &error), error);
  const fs::path fixture = temporary.path / "fixture" / "Config.DAT";
  WriteBytes(fixture, encoded);

  // Untouched Runtime: load from a copy in the isolated documents directory,
  // project authenticated keys, and keep the exact DFLT bytes on shutdown.
  const fs::path untouchedDocs = temporary.path / "untouched";
  fs::create_directories(untouchedDocs);
  fs::copy_file(fixture, untouchedDocs / "Config.DAT");
  const auto untouchedBefore = ReadBytes(untouchedDocs / "Config.DAT");
  {
    Application app(assets, argv[1], untouchedDocs);
    const auto &state = app.runtime.retailConfig();
    Check(state.voiceVolume == 8 && state.bgmVolume == 6 &&
              state.seVolume == 4 && state.textView && !state.menVoice &&
              state.mute && state.skip && !state.autoDraw && state.autoMode &&
              !state.superSkip,
          "Runtime did not project Config.DAT retail state");
    Check(state.masterVolume == -1.0f,
          "Runtime loaded opaque persisted MasterVolume");
    Check(app.engine.GetSettings().masterMuted,
          "Runtime did not project persisted Mute into engine settings");
    Check(!app.engine.GetDebug().getSettings().showFPS &&
              !app.engine.GetDebug().getSettings().showNodeClock,
          "School Days normal runtime debug HUD defaults are visible");
    const auto &display =
        SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(app.runtime);
    const auto &projected =
        SchoolDays::RuntimeConfigStorageTestAccess::DisplayState(app.runtime);
    Check(display.baseDisplayType == 1 && display.deviceDisplayType == 1 &&
              display.windowMode == 1 && display.typeMiniNote == 0 &&
              projected.effectiveDisplayType == 1 &&
              projected.windowMode == 1 && projected.typeMiniNote == 0 &&
              SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(
                  app.runtime) ==
                  Kotonoha::SchoolDaysDisplayPolicy::Variant::WideFull,
          "Runtime did not preserve the authored Config.DAT WindowMode over "
          "the physical host");
    const auto visuals =
        SchoolDays::RuntimeConfigStorageTestAccess::VisualState(app.runtime);
    Check(visuals.defWideCurrent == 4 && visuals.defFullCurrent == 7,
          "Config visual ids do not reflect effective DisplayType/WindowMode");
    const auto &storage =
        SchoolDays::RuntimeConfigStorageTestAccess::Storage(app.runtime);
    Check(
        storage.Path() ==
                fs::absolute(untouchedDocs / "Config.DAT").lexically_normal() &&
            storage.IsLoaded() && !storage.IsDirty(),
        "Runtime ConfigStorage did not bind isolated documents/Config.DAT "
        "cleanly");
  }
  Check(ReadBytes(untouchedDocs / "Config.DAT") == untouchedBefore,
        "untouched Runtime mutated Config.DAT");

  // The retail W selector still changes its authored UI family. On Windows
  // the remaster also mirrors General Window/Full to the physical host; on
  // other platforms it must not overwrite the retail selection with the
  // unchanged host mode after a restart.
  const fs::path displayDocs = temporary.path / "display";
#if defined(_WIN32) && !defined(__ANDROID__)
  constexpr auto expectedFullMode = Kotonoha::WindowMode::Borderless;
#else
  constexpr auto expectedFullMode = Kotonoha::WindowMode::Windowed;
#endif
  fs::create_directories(displayDocs);
  fs::copy_file(fixture, displayDocs / "Config.DAT");
  const auto displayBefore = ReadBytes(displayDocs / "Config.DAT");
  {
    Application app(assets, argv[1], displayDocs);
    const auto physical = app.engine.GetSettings();
    Check(physical.mode == Kotonoha::WindowMode::Windowed,
          "display test requires the default physical Windowed mode");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestFull, 1);
    const auto windowFamily =
        SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(app.runtime);
    Check(windowFamily.windowMode == 0 &&
              SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(
                  app.runtime) ==
                  Kotonoha::SchoolDaysDisplayPolicy::Variant::Wide,
          "General Full->Window did not update only retail projection");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestFull, 0);
    const auto &full =
        SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(app.runtime);
    const auto fullVisuals =
        SchoolDays::RuntimeConfigStorageTestAccess::VisualState(app.runtime);
    Check(full.windowMode == 1 &&
              SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(
                  app.runtime) ==
                  Kotonoha::SchoolDaysDisplayPolicy::Variant::WideFull &&
              fullVisuals.defWideCurrent == 4 &&
              fullVisuals.defFullCurrent == 7,
          "RequestFull did not apply the authenticated W transition");
    const auto afterFull = app.engine.GetSettings();
#if defined(_WIN32) && !defined(__ANDROID__)
    const auto *desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    Check(desktop && afterFull.mode == Kotonoha::WindowMode::Borderless &&
              afterFull.width == desktop->w && afterFull.height == desktop->h,
          "General/Full did not enter borderless at desktop-native geometry");
#else
    Check(afterFull.mode == Kotonoha::WindowMode::Windowed,
          "Retail Full must not change the host window outside Windows");
#endif
    Check(SchoolDays::RuntimeConfigStorageTestAccess::Storage(app.runtime)
                  .IsDirty() &&
              ReadBytes(displayDocs / "Config.DAT") == displayBefore,
          "General/Full did not cache its explicit WindowMode choice until "
          "shutdown");

    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestWide, 0);
    const auto &wide =
        SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(app.runtime);
    const auto wideVisuals =
        SchoolDays::RuntimeConfigStorageTestAccess::VisualState(app.runtime);
    Check(wide.baseDisplayType == 0 && wide.deviceDisplayType == 0 &&
              wide.windowMode == 1 &&
              SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(
                  app.runtime) ==
                  Kotonoha::SchoolDaysDisplayPolicy::Variant::Standard &&
              wideVisuals.defWideCurrent == 5 &&
              wideVisuals.defFullCurrent == 7,
          "RequestWide did not update B/D and project Standard");
#if defined(_WIN32) && !defined(__ANDROID__)
    Check(std::abs(app.runtime.narrativePresentationHeightScale() - 0.75f) <
              0.001f,
          "Standard FULL did not compress the authored Novel picture to 75 "
          "percent");
#else
    Check(std::abs(app.runtime.narrativePresentationHeightScale() - 1.0f) <
              0.001f,
          "Non-Windows platforms must retain the authored Novel height");
#endif
    Check(SchoolDays::RuntimeConfigStorageTestAccess::Storage(app.runtime)
              .IsDirty(),
          "accepted RequestWide did not cache DisplayType");
    const auto afterWide = app.engine.GetSettings();
    Check(afterWide.mode == afterFull.mode &&
              afterWide.width == afterFull.width &&
              afterWide.height == afterFull.height,
          "RequestWide mutated physical Engine/SDL display settings");
  }
  std::string displayDecoded;
  Check(Storage::DecodeDocument(ReadBytes(displayDocs / "Config.DAT"),
                                &displayDecoded, &error),
        error);
  Check(displayDecoded.find("[DisplayType]=\"0\"") != std::string::npos &&
            displayDecoded.find("[WindowMode]=\"1\"") != std::string::npos,
        "display cache did not persist DisplayType/WindowMode independently");

  // Full/Window selects native W/skin everywhere and changes physical SDL mode
  // on Windows only. Keep both preferences consistent across restarts. All
  // files live in temporary Documents, never in a user save folder.
  const fs::path persistentDocs = temporary.path / "persistent-general-sound";
  fs::create_directories(persistentDocs);
  fs::copy_file(fixture, persistentDocs / "Config.DAT");
  {
    Application app(assets, argv[1], persistentDocs, true);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestWide, 1);
    {
      std::ifstream aspectFile(persistentDocs / "Kotonoha.ini");
      const std::string aspect((std::istreambuf_iterator<char>(aspectFile)),
                               std::istreambuf_iterator<char>());
      Check(aspect.find("ContentAspectPreference=2") != std::string::npos,
            "Selecting already-active WIDE did not set explicit Display 16:9 "
            "instead of Auto");
    }
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestFull, 1);
    Check(app.engine.GetSettings().mode == Kotonoha::WindowMode::Windowed,
          "General Window did not select host Windowed");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestFull, 0);
    Check(app.engine.GetSettings().mode == expectedFullMode &&
              SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(
                  app.runtime)
                      .windowMode == 1,
          "General Full did not change both physical mode and native W");
    // Exercise minus/plus rather than silently assigning all sound channels.
    for (int i = 0; i < 6; ++i)
      SchoolDays::RuntimeConfigStorageTestAccess::Apply(
          app.runtime, Kind::StepSoundBank, 0, 0, 1);
    for (int i = 0; i < 6; ++i)
      SchoolDays::RuntimeConfigStorageTestAccess::Apply(
          app.runtime, Kind::StepSoundBank, 0, 1, 0);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(
        app.runtime, Kind::SetSoundLevel, 2, 2);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetMenVoice, 1);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetMute, 0);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetSkip, 0);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetSuperSkip, 1);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetDrawMessage, 0);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestWide, 0);
    Check(SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(app.runtime) ==
              Kotonoha::SchoolDaysDisplayPolicy::Variant::Standard,
          "General STANDARD did not select the Display 4:3 authored layout");
    {
      std::ifstream aspectFile(persistentDocs / "Kotonoha.ini");
      const std::string aspect((std::istreambuf_iterator<char>(aspectFile)),
                               std::istreambuf_iterator<char>());
      Check(aspect.find("ContentAspectPreference=1") != std::string::npos,
            "General STANDARD did not save the corresponding Display 4:3 "
            "preference");
    }
    const auto &sound = app.runtime.retailConfig();
    Check(sound.bgmVolume == 0 && sound.seVolume == 10 &&
              sound.voiceVolume == 2 && sound.menVoice && !sound.mute &&
              !sound.skip && sound.superSkip && !sound.textView,
          "General/Sound immediate actions did not update all selected values");
  }
#if defined(_WIN32) && !defined(__ANDROID__)
  Check(fs::is_regular_file(persistentDocs / "settings.json"),
        "General Full did not write physical settings.json");
#endif
  {
    Application app(assets, argv[1], persistentDocs, true);
    const auto &state = app.runtime.retailConfig();
    Check(app.engine.GetSettings().mode == expectedFullMode &&
              SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(
                  app.runtime)
                      .windowMode == 1,
          "General Full did not survive a real settings.json reload (mode " +
              std::to_string(static_cast<int>(app.engine.GetSettings().mode)) +
              ", expected " +
              std::to_string(static_cast<int>(expectedFullMode)) +
              ", retail W " +
              std::to_string(
                  SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(
                      app.runtime)
                      .windowMode) +
              ")");
    Check(SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(app.runtime) ==
              Kotonoha::SchoolDaysDisplayPolicy::Variant::Standard,
          "General 4:3 aspect did not survive remaster preference reload");
    Check(state.bgmVolume == 0 && state.seVolume == 10 &&
              state.voiceVolume == 2 && state.menVoice && !state.mute &&
              !state.skip && state.superSkip && !state.textView,
          "General/Sound values did not survive Config.DAT reload");
    const auto visuals =
        SchoolDays::RuntimeConfigStorageTestAccess::VisualState(app.runtime);
    Check(visuals.defFullCurrent == 7 && visuals.soundLevels[0] == 0 &&
              visuals.soundLevels[1] == 10 && visuals.soundLevels[2] == 2 &&
              visuals.soundMenVoiceCurrent == 10 &&
              visuals.soundMuteCurrent == 13,
          "Reopened General/Sound UI selected chips do not match persisted "
          "choices");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestFull, 1);
    Check(app.engine.GetSettings().mode == Kotonoha::WindowMode::Windowed,
          "General Window did not leave fullscreen after reopening");
  }
  {
    Application app(assets, argv[1], persistentDocs, true);
    Check(app.engine.GetSettings().mode == Kotonoha::WindowMode::Windowed &&
              SchoolDays::RuntimeConfigStorageTestAccess::DisplayBackend(
                  app.runtime)
                      .windowMode == 0,
          "General Window choice did not persist after the second restart");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::RequestWide, 1);
    Check(SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(app.runtime) ==
              Kotonoha::SchoolDaysDisplayPolicy::Variant::Wide,
          "General WIDE did not restore the Display 16:9 authored layout");
    Check(std::abs(app.runtime.narrativePresentationHeightScale() - 1.0f) <
              0.001f,
          "General WIDE did not restore the uncompressed Novel picture");
  }
  {
    Application app(assets, argv[1], persistentDocs, true);
    Check(SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(app.runtime) ==
              Kotonoha::SchoolDaysDisplayPolicy::Variant::Wide,
          "General WIDE 16:9 did not survive a further restart");
    std::ifstream aspectFile(persistentDocs / "Kotonoha.ini");
    const std::string aspect((std::istreambuf_iterator<char>(aspectFile)),
                             std::istreambuf_iterator<char>());
    Check(
        aspect.find("ContentAspectPreference=2") != std::string::npos,
        "General WIDE did not save the corresponding Display 16:9 preference");
  }

  // A proven SysMenu state mutation changes only the cache until shutdown,
  // then round-trips while preserving opaque, malformed and MasterVolume lines.
  const fs::path changedDocs = temporary.path / "changed";
  fs::create_directories(changedDocs);
  fs::copy_file(fixture, changedDocs / "Config.DAT");
  const auto changedBefore = ReadBytes(changedDocs / "Config.DAT");
  {
    Application app(assets, argv[1], changedDocs);
    SchoolDays::RuntimeConfigStorageTestAccess::SetPlaybackSpeed(app.runtime,
                                                                 4);
    Kotonoha_orsEvent voice{};
    voice.command = PLAY_VOICE;
    voice.startTick = 0;
    voice.endTick = 48;
    SchoolDays::RuntimeConfigStorageTestAccess::AddActivePlayer(app.runtime,
                                                                voice, 24.0);
    Check(SchoolDays::RuntimeConfigStorageTestAccess::SpeedIndex(app.runtime) ==
                  4 &&
              SchoolDays::RuntimeConfigStorageTestAccess::Speed(app.runtime) ==
                  24.0 &&
              SchoolDays::RuntimeConfigStorageTestAccess::FirstPlayerRate(
                  app.runtime) == 24.0,
          "precondition: accelerated active media did not reach 24x");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetSkip, 0);
    Check(SchoolDays::RuntimeConfigStorageTestAccess::SpeedIndex(app.runtime) ==
                  0 &&
              SchoolDays::RuntimeConfigStorageTestAccess::Speed(app.runtime) ==
                  1.0 &&
              SchoolDays::RuntimeConfigStorageTestAccess::FirstPlayerRate(
                  app.runtime) == 1.0,
          "SetSkip(false) did not force index0/rate1.0 into active media");
    const auto &storage =
        SchoolDays::RuntimeConfigStorageTestAccess::Storage(app.runtime);
    Check(storage.IsDirty(),
          "proven Config action did not dirty Runtime cache");
    Check(ReadBytes(changedDocs / "Config.DAT") == changedBefore,
          "Config action wrote Config.DAT before shutdown flush");
  }
  const auto changedAfter = ReadBytes(changedDocs / "Config.DAT");
  Check(changedAfter != changedBefore,
        "shutdown did not flush changed Config.DAT");
  std::string decoded;
  Check(Storage::DecodeDocument(changedAfter, &decoded, &error), error);
  Check(decoded.find("[Skip]=\"0\"") != std::string::npos &&
            decoded.find("Skip]=\"malformed-opaque\"") != std::string::npos &&
            decoded.find("[OpaqueFuture]=\"keep-me\"") != std::string::npos &&
            decoded.find("[MasterVolume]=\"-7.000000\"") != std::string::npos,
        "Runtime Config.DAT flush lost known/opaque/malformed data");

  Storage roundtrip;
  Storage::LoadResult reloaded;
  Check(roundtrip.Initialize(changedDocs, &error) &&
            roundtrip.Load(&reloaded, &error) && !reloaded.state.skip &&
            reloaded.state.masterVolume == -1.0f,
        "Runtime-flushed Config.DAT did not round-trip through authenticated "
        "storage");

  // Missing Config.DAT stays absent through startup/shutdown and through an
  // idempotent UI request. Only an actual proven state change may create it.
  const fs::path missingDocs = temporary.path / "missing";
  fs::create_directories(missingDocs);
  {
    Application app(assets, argv[1], missingDocs);
    Check(!fs::exists(missingDocs / "Config.DAT"),
          "Runtime created missing Config.DAT at startup");
    int outputWidth = 0, outputHeight = 0;
    Check(SDL_GetRenderOutputSize(&app.engine.GetRenderer(), &outputWidth,
                                  &outputHeight),
          "cannot query render output for auto display-profile test");
    Kotonoha::SchoolDaysDisplayPolicy::Decision expected;
    Check(Kotonoha::SchoolDaysDisplayPolicy::SelectAuto(
              outputWidth, outputHeight, &expected),
          "cannot derive automatic logical display profile");
    const auto canonical =
        Kotonoha::SchoolDaysDisplayPolicy::CanonicalRetailStateForVariant(
            expected.variant);
    Kotonoha::SchoolDaysDisplayPolicy::RetailBackendState expectedBackend;
    expectedBackend.baseDisplayType = canonical.effectiveDisplayType;
    expectedBackend.deviceDisplayType = canonical.effectiveDisplayType;
    expectedBackend.windowMode =
        app.engine.GetSettings().mode == Kotonoha::WindowMode::Windowed ? 0 : 1;
    expectedBackend.typeMiniNote = canonical.typeMiniNote;
    const auto expectedVariant =
        Kotonoha::SchoolDaysDisplayPolicy::SelectRetailVariant(
            Kotonoha::SchoolDaysDisplayPolicy::ProjectRetailState(
                expectedBackend));
    Check(SchoolDays::RuntimeConfigStorageTestAccess::UiVariant(app.runtime) ==
              expectedVariant,
          "missing Config.DAT did not combine SelectAuto with physical host "
          "mode");
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetSkip, 0);
    Check(!SchoolDays::RuntimeConfigStorageTestAccess::Storage(app.runtime)
               .IsDirty(),
          "idempotent Config action dirtied first-run cache");
  }
  Check(!fs::exists(missingDocs / "Config.DAT"),
        "clean first-run shutdown created Config.DAT");
  {
    Application app(assets, argv[1], missingDocs);
    SchoolDays::RuntimeConfigStorageTestAccess::Apply(app.runtime,
                                                      Kind::SetSkip, 1);
    Check(SchoolDays::RuntimeConfigStorageTestAccess::Storage(app.runtime)
                  .IsDirty() &&
              !fs::exists(missingDocs / "Config.DAT"),
          "changed first-run state did not remain deferred in cache");
  }
  Check(fs::exists(missingDocs / "Config.DAT"),
        "proven changed first-run state did not create Config.DAT on shutdown");

  std::cout << "SchoolDaysRuntimeConfigStorageTest PASS\n";
  return 0;
}
