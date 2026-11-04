#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>
#include <Kotonoha/media/upscaling/OpenGLUpscaler.hpp>
#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/script/KtrfScript.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace Kotonoha;

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

template <typename F> void rejects(F operation, const char *message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}

struct TemporaryDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("kotonoha-infra-" + std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() { std::filesystem::create_directory(path); }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

std::atomic<int> forwarded{0};
void SDLCALL countLog(void *, int, SDL_LogPriority, const char *) {
  ++forwarded;
}

void testLog(const std::filesystem::path &directory) {
  SDL_LogOutputFunction previous = nullptr;
  void *previousData = nullptr;
  SDL_GetLogOutputFunction(&previous, &previousData);
  SDL_SetLogOutputFunction(countLog, nullptr);
  const auto file = (directory / "engine.log").string();
  {
    Log log(file);
    for (int i = 0; i < 1005; ++i)
      SDL_LogInfo(LogCore, "Message %d", i);
    const auto entries = Log::entries();
    require(entries.size() == 1000 && entries.front().message == "Message 5",
            "Log history is bounded and ordered");
    require(forwarded == 1005, "Previous SDL output is preserved");
    Log::clear();
    std::thread worker([] {
      for (int i = 0; i < 50; ++i)
        SDL_LogWarn(LogMedia, "Worker %d", i);
    });
    for (int i = 0; i < 50; ++i)
      SDL_LogInfo(LogCore, "Main %d", i);
    worker.join();
    require(Log::entries().size() == 100, "Worker logs are captured safely");
    rejects([] { Log another; }, "Reject nested log ownership");
  }
  SDL_LogOutputFunction restored = nullptr;
  void *restoredData = nullptr;
  SDL_GetLogOutputFunction(&restored, &restoredData);
  require(restored == countLog, "Log shutdown restores SDL output");
  SDL_SetLogOutputFunction(previous, previousData);
  std::ifstream input(file);
  const std::string text((std::istreambuf_iterator<char>(input)), {});
  require(text.find("Message 1004") != std::string::npos &&
              text.find("Worker 49") != std::string::npos,
          "File logging persists flushed messages");
}

void testSettings(const std::filesystem::path &directory) {
  const std::vector<std::string> shaders{"/tmp/custom shaders/first.glsl",
                                         "/tmp/colon:name.glsl",
                                         "C:\\shaders\\second.glsl"};
  require(
      UpscaleShaderFiles(UpscaleShaderFileList(shaders)) == shaders,
      "Shader lists preserve order, spaces, colons and Windows drive paths");
  require(UpscaleShaderFiles("first.glsl:second.glsl") ==
              std::vector<std::string>{"first.glsl", "second.glsl"},
          "Legacy colon-separated presets remain supported");
  require(UpscaleShaderFiles("C:\\shaders\\first.glsl") ==
              std::vector<std::string>{"C:\\shaders\\first.glsl"},
          "A drive letter is not a shader separator");
  const auto file = (directory / "settings.json").string();
  EngineSettings settings;
  settings.width = 1600;
  settings.height = 900;
  settings.display = 2;
  settings.mode = WindowMode::Exclusive;
  settings.fullscreenWidth = 1920;
  settings.fullscreenHeight = 1080;
  settings.refreshRate = 59.94f;
  settings.vsync = true;
  settings.swapControllerAB = true;
  settings.masterVolume = 0.4f;
  settings.masterMuted = true;
  settings.debugUIScale = 1.75f;
  settings.resizable = false;
  settings.renderer = "opengl";
  settings.upscaling = {UpscaleBackend::MPV, UpscalePreset::ArtCNNDenoise, 1.5f,
                        "custom.glsl"};
  settings.logPriority = SDL_LOG_PRIORITY_DEBUG;
  settings.translationPath = "trans.json";
  settings.fontPath = "font.ttf";
  settings.vnFontPath = "vn.ttf";
  settings.vnTextColorOverride = true;
  settings.vnTextColor = {21, 43, 65, 187};
  settings.channelVolumes = {{"BGM", .3f}, {"Voice", .7f}};
  settings.save(file);
  const auto read = EngineSettings::load(file);
  require(read.width == 1600 && read.height == 900 && read.display == 2 &&
              read.mode == WindowMode::Exclusive &&
              read.fullscreenWidth == 1920 && read.fullscreenHeight == 1080 &&
              std::abs(read.refreshRate - 59.94f) < 0.001f && read.vsync &&
              read.swapControllerAB && !read.resizable &&
              read.renderer == "opengl" &&
              read.upscaling.backend == UpscaleBackend::MPV &&
              read.upscaling.preset == UpscalePreset::ArtCNNDenoise &&
              read.upscaling.factor == 1.5f &&
              UpscaleShaderFiles(read.upscaling.shaderFiles) ==
                  std::vector<std::string>{"custom.glsl"} &&
              read.logPriority == SDL_LOG_PRIORITY_DEBUG &&
              read.masterVolume == 0.4f && read.masterMuted &&
              read.debugUIScale == 1.75f &&
              read.translationPath == "trans.json" &&
              read.fontPath == "font.ttf" && read.vnFontPath == "vn.ttf" &&
              read.vnTextColorOverride && read.vnTextColor.r == 21 &&
              read.vnTextColor.a == 187 &&
              read.channelVolumes == settings.channelVolumes,
          "Every settings field survives persistence");
  for (auto preset : UpscalePresets()) {
    auto profile = settings;
    profile.upscaling.preset = preset;
    profile.upscaling.shaderFiles.clear();
    profile.save(file);
    require(EngineSettings::load(file).upscaling.preset == preset,
            "All shader profiles persist across settings reloads");
  }
  settings.save(file);
  settings.width = 0;
  rejects([&] { settings.save(file); }, "Validate before overwriting settings");
  require(EngineSettings::load(file).width == 1600,
          "Invalid save leaves file intact");
  for (const char *invalid : {"[]",
                              "{",
                              "{}garbage",
                              "{\"width\": \"1280\"}",
                              "{\"width\":9223372036854775807}",
                              "{\"mode\":99}",
                              "{\"upscaler\":99}",
                              "{\"preset\":-1}",
                              "{\"version\":2}",
                              "{\"vsync\":1}",
                              "{\"factor\":0.5}",
                              "{\"factor\":1e300}",
                              "{\"refreshRate\":-1}",
                              "{\"logPriority\":0}",
                              "{\"fullscreenWidth\":1920}",
                              "{\"renderer\":\"x\\u0000y\"}",
                              "{\"masterVolume\":-0.1}",
                              "{\"masterVolume\":1.1}",
                              "{\"masterVolume\":1e300}",
                              "{\"masterMuted\":1}",
                              "{\"debugUIScale\":0.9}",
                              "{\"debugUIScale\":3.1}"}) {
    writeTextFileAtomically(file, invalid);
    rejects([&] { EngineSettings::load(file); },
            "Reject invalid settings JSON/value");
  }
  writeTextFileAtomically(file, "{}");
  require(EngineSettings::load(file).width == 1280,
          "Partial settings use defaults");
  rejects([&] { EngineSettings::load((directory / "missing.json").string()); },
          "Explicit missing settings load reports an error");
  for (float invalid : {0.0f, 4.1f, std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity()}) {
    UpscaleSettings value;
    value.factor = invalid;
    rejects([&] { value.validate(); }, "Reject invalid upscale factor");
  }
}

void testKtrfRuntime(Engine &engine, const std::filesystem::path &directory) {
  auto &manager = GetVNManagerRuntime(engine);
  ScriptNode first{0, NodeType::Dialogue, "Test", "Original", "", "", {}, 1};
  first.backgroundAsset = "background.bmp";
  ScriptNode last;
  last.id = 1;
  last.type = NodeType::End;
  manager.loadScript({first, last});
  engine.GetRuntime().SetState(GameState::Gameplay);
  manager.startScript();
  const auto listed = engine.debugNodes();
  require(listed.size() == 2 && listed.front().assets ==
                                    std::vector<std::string>{"background.bmp"},
          "Debug nodes expose scene assets");
  const auto path = (directory / "runtime.json").string();
  engine.dumpKtrf(path);
  auto edited = KtrfScript::load(path);
  edited.nodes[0].text = "Customized runtime dialogue";
  edited.dump(path);
  engine.loadKtrf(path);
  require(GetVNTextBoxRuntime(engine, "", "", 30).getText() ==
              "Customized runtime dialogue",
          "Loaded KTRF changes the live dialogue");
  edited.nodes[0].nextNodeId = 9999;
  rejects([&] { edited.dump(path); }, "Invalid graph cannot replace the file");
  std::ofstream(path)
      << "{\"profile\":\"kotonoha.vn\",\"version\":1,\"nodes\":[]}";
  rejects([&] { engine.loadKtrf(path); }, "Invalid loaded graph is rejected");
  require(manager.getCurrentNodeId() == 0,
          "Rejected KTRF preserves the current runtime graph");
  require(GetVNTextBoxRuntime(engine, "", "", 30).getText() ==
              "Customized runtime dialogue",
          "Rejected load preserves the visible scene");
  engine.debugJumpToNode("1");
  require(manager.isFinished(), "Debug facade jumps to an engine node");
}
SDL_Event key(SDL_Keycode code) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = code;
  return event;
}

class EventRuntime : public Runtime {
public:
  int novelEvents = 0, screenEvents = 0;
  bool touch = false, held = false;
  int backCalls = 0;
  std::function<bool(EventRef)> screenHandler;
  bool handleBackAction() override {
    ++backCalls;
    return true;
  }
  int touchDowns = 0, touchUps = 0;
  int controllerButtons = 0;
  Uint8 lastControllerButton = 0;
  bool continuousTouch() const override { return touch; }
  EventRuntime() : Runtime("Infrastructure tests", "1", "org.kotonoha.infra") {}
  bool onEvent(EventRef event) override {
    ++novelEvents;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
      ++controllerButtons;
      lastControllerButton = event.gbutton.button;
    }
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.which == SDL_TOUCH_MOUSEID) {
      held = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      if (held)
        ++touchDowns;
      else
        ++touchUps;
      return true;
    }
    return false;
  }
  bool onScreenEvent(EventRef event) override {
    ++screenEvents;
    return screenHandler ? screenHandler(event) : false;
  }
};

void testControllerNavigation(Engine &engine, EventRuntime &runtime) {
  SDL_Event axis{};
  axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  axis.gaxis.which = 999;
  axis.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTY;
  axis.gaxis.value = 24000;
  const int before = runtime.controllerButtons;
  engine.Event(&axis);
  require(
      runtime.controllerButtons == before + 1 &&
          runtime.lastControllerButton == SDL_GAMEPAD_BUTTON_DPAD_DOWN,
      "Engine routes left-stick movement through the normal controller path");
  axis.gaxis.value = 14000;
  engine.Event(&axis);
  require(runtime.controllerButtons == before + 1,
          "Analog jitter does not repeat menu navigation");
  axis.gaxis.value = 0;
  engine.Event(&axis);
  require(runtime.controllerButtons == before + 2,
          "Engine delivers stick release on returning to the dead zone");
  engine.openSettings();
  SDL_Event cancel{};
  cancel.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  cancel.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  cancel.gbutton.down = true;
  const int novel = runtime.novelEvents;
  engine.Event(&cancel);
  require(!engine.GetSettingsMenu().isOpen() && runtime.novelEvents == novel,
          "Controller cancels engine settings without reaching the story");
  const auto original = engine.GetSettings();
  auto swapped = original;
  swapped.swapControllerAB = true;
  engine.applySettings(swapped, false);
  SDL_Event physicalSouth{};
  physicalSouth.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  physicalSouth.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  physicalSouth.gbutton.down = true;
  engine.Event(&physicalSouth);
  require(runtime.lastControllerButton == SDL_GAMEPAD_BUTTON_EAST,
          "Engine applies the A/B swap before dispatching to a runtime");
  physicalSouth.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
  physicalSouth.gbutton.down = false;
  engine.Event(&physicalSouth);
  engine.applySettings(original, false);
}

void testModalInputRouting(Engine &engine) {
  struct RoutingRuntime : Runtime {
    RoutingRuntime() : Runtime("Routing", "1", "org.kotonoha.routing") {
      SetState(GameState::Gameplay);
    }
    void add(Scene &scene) { gameplayScenes.push_back(&scene); }
  } runtime;
  struct CountingScene : Scene {
    int calls = 0;
    bool modal = false, consume = false;
    CountingScene(Engine &engine) : Scene(engine) {}
    bool capturesInput() const override { return modal; }
    bool onEvent(EventRef) override {
      ++calls;
      return consume;
    }
  } story(engine), modal(engine);
  modal.modal = true;
  runtime.add(story);
  runtime.add(modal);
  require(
      runtime.onEvent(key(SDLK_F7)) && modal.calls == 1 && story.calls == 0,
      "An unbound modal event is delivered once and never reaches the story");
  modal.consume = true;
  require(runtime.onEvent(key(SDLK_RETURN)) && modal.calls == 2 &&
              story.calls == 0,
          "A consumed modal confirmation cannot advance the underlying story");
  modal.modal = false;
  modal.consume = false;
  story.consume = true;
  require(runtime.onEvent(key(SDLK_RETURN)) && modal.calls == 3 &&
              story.calls == 1,
          "Non-modal scenes pass unbound input to the next scene exactly once");
}

void testContinuousTouch(Engine &engine, EventRuntime &runtime) {
  runtime.touch = true;
  SDL_Event finger{};
  finger.type = SDL_EVENT_FINGER_DOWN;
  finger.tfinger.touchID = 7;
  finger.tfinger.fingerID = 9;
  finger.tfinger.x = .5f;
  finger.tfinger.y = .5f;
  engine.Event(&finger);
  require(runtime.held && runtime.touchDowns == 1 && runtime.touchUps == 0,
          "Native touch exposes held pointer state before the release");
  engine.Iterate();
  require(runtime.held, "A native script can poll touch-down on its own frame");
  finger.type = SDL_EVENT_FINGER_UP;
  engine.Event(&finger);
  require(!runtime.held && runtime.touchDowns == 1 && runtime.touchUps == 1,
          "Native touch releases once without an additional synthesized tap");
  finger.type = SDL_EVENT_FINGER_DOWN;
  engine.Event(&finger);
  SDL_Event lost{};
  lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  engine.Event(&lost);
  require(!runtime.held,
          "Focus loss releases a held native touch outside clickable controls");
  finger.type = SDL_EVENT_FINGER_DOWN;
  engine.Event(&finger);
  SDL_Event swipe = finger;
  swipe.type = SDL_EVENT_FINGER_MOTION;
  swipe.tfinger.y = .3f;
  engine.Event(&swipe);
  require(!runtime.held,
          "A navigation swipe cancels the underlying pointer press");
  swipe.type = SDL_EVENT_FINGER_UP;
  engine.Event(&swipe);
  engine.Event(&finger);
  finger.tfinger.fingerID = 10;
  engine.Event(&finger);
  require(!runtime.held,
          "A second finger cancels the pending pointer activation");
  finger.type = SDL_EVENT_FINGER_UP;
  engine.Event(&finger);
  finger.tfinger.fingerID = 9;
  engine.Event(&finger);
  require(!runtime.held, "Multitouch release cannot generate a fresh click");
  runtime.touch = false;
  runtime.SetState(GameState::Gameplay);
  const int before = runtime.backCalls;
  runtime.screenHandler = [&](EventRef event) {
    return BackAction(engine, event);
  };
  auto back = key(SDLK_ESCAPE);
  engine.Event(&back);
  require(
      runtime.backCalls == before,
      "Back consumed by screen UI must not be dispatched again to navigation");
  runtime.screenHandler = {};
}

void testUnchangedWindowGeometry(Engine &engine) {
  const auto original = engine.GetSettings();
  int previousWidth = 0, previousHeight = 0;
  SDL_GetWindowSize(&engine.GetWindow(), &previousWidth, &previousHeight);
  require(SDL_SetWindowSize(&engine.GetWindow(), 1000, 620),
          "Set an external window size");
  require(SDL_SetWindowPosition(&engine.GetWindow(), 35, 45),
          "Set an external window position");
  SDL_SyncWindow(&engine.GetWindow());
  int x = 0, y = 0;
  SDL_GetWindowPosition(&engine.GetWindow(), &x, &y);
  auto edited = original;
  edited.masterVolume = .42f;
  edited.vnTextColorOverride = true;
  edited.vnTextColor = {90, 180, 220, 255};
  engine.applySettings(edited, false);
  int width = 0, height = 0, afterX = 0, afterY = 0;
  SDL_GetWindowSize(&engine.GetWindow(), &width, &height);
  SDL_GetWindowPosition(&engine.GetWindow(), &afterX, &afterY);
  require(
      width == 1000 && height == 620 && x == afterX && y == afterY,
      "Audio and appearance changes must not resize or reposition the window");
  engine.applySettings(original, false);
  SDL_SetWindowSize(&engine.GetWindow(), previousWidth, previousHeight);
}

void testTouchSettings(Engine &engine, EventRuntime &runtime) {
  const auto original = engine.GetSettings();
  engine.openSettings();
  SDL_FingerID fingerId = 98;
  auto touch = [&](Uint32 type, float x, float y) {
    SDL_Event event{};
    event.type = type;
    event.tfinger.touchID = 97;
    event.tfinger.fingerID = fingerId;
    event.tfinger.x = x / 1280;
    event.tfinger.y = y / 720;
    if (type == SDL_EVENT_FINGER_UP)
      ++fingerId;
    event.tfinger.windowID = SDL_GetWindowID(&engine.GetWindow());
    engine.Event(&event);
  };
  auto tap = [&](float x, float y) {
    touch(SDL_EVENT_FINGER_DOWN, x, y);
    touch(SDL_EVENT_FINGER_UP, x, y);
  };
  touch(SDL_EVENT_FINGER_DOWN, 10, 400);
  touch(SDL_EVENT_FINGER_MOTION, 250, 400);
  touch(SDL_EVENT_FINGER_UP, 250, 400);
  require(!engine.GetSettingsMenu().isOpen(),
          "Edge swipe closes settings without leaving a held pointer");
  engine.openSettings();
  tap(600, 105); // Audio tab.
  touch(SDL_EVENT_FINGER_DOWN, 700, 172);
  touch(SDL_EVENT_FINGER_MOTION, 945, 172);
  touch(SDL_EVENT_FINGER_UP, 945, 172);
  tap(755, 268); // BGM gain (first sorted channel).
  tap(800, 105); // VN text tab (valid with and without the Input tab).
  tap(500, 218); // Override colors.
  tap(500, 266); // Dialogue color selector.
  tap(500, 216); // Black, second option.
  const int before = runtime.novelEvents;
  tap(400, 646); // Apply.
  require(!engine.GetSettingsMenu().isOpen(),
          "Touch applies settings and closes the modal");
  require(std::abs(engine.GetSettings().masterVolume - .75f) < .01f &&
              std::abs(engine.GetAudio().channel("BGM")->getUserVolume() -
                       .25f) < .01f,
          "Finger drag changes master gain and finger tap sets channel gain");
  require(engine.GetSettings().vnTextColorOverride &&
              engine.GetSettings().vnTextColor.r == 0,
          "Touch selects VN text colors through the shared selection box");
  require(runtime.novelEvents == before,
          "Closing settings with touch must not click through to the game");
  engine.applySettings(original, false);
}

void testWindowAndMenu(Engine &engine, EventRuntime &runtime,
                       const std::string &file) {
  auto settings = engine.GetSettings();
  settings.width = 960;
  settings.height = 540;
  settings.upscaling.backend = UpscaleBackend::Linear;
  engine.applySettings(settings);
  int width = 0, height = 0;
  require(SDL_GetWindowSize(&engine.GetWindow(), &width, &height) &&
              width == 960 && height == 540 &&
              (SDL_GetWindowFlags(&engine.GetWindow()) & SDL_WINDOW_RESIZABLE),
          "Window settings affect the actual SDL window");
  require(EngineSettings::load(file).width == 960, "Apply persists settings");
  settings.display = 99999;
  rejects([&] { engine.applySettings(settings); }, "Reject missing display");
  require(engine.GetSettings().display == 0 &&
              EngineSettings::load(file).display == 0,
          "Failed apply rolls back runtime and persistent settings");
  settings = engine.GetSettings();
  settings.renderer = "not-a-renderer";
  rejects([&] { engine.applySettings(settings); },
          "Renderer change requires restart");
  settings = engine.GetSettings();
  settings.upscaling.backend = UpscaleBackend::MPV;
  rejects([&] { engine.applySettings(settings); },
          "Reject GPU backend on software SDL");

  engine.openSettings();
  require(engine.isStoryPaused(), "Settings pause the story");
  auto event = key(SDLK_DOWN);
  engine.Event(&event);
  event = key(SDLK_RETURN);
  engine.Event(&event);
  require(runtime.novelEvents == 0 && runtime.screenEvents == 0,
          "Settings capture input in both render spaces");
  event = key(SDLK_ESCAPE);
  engine.Event(&event);
  require(!engine.GetSettingsMenu().isOpen() && !engine.isStoryPaused() &&
              engine.GetSettings().width == 960,
          "Cancel does not apply the draft or leak Escape");
  engine.openSettings();
  event = key(SDLK_DOWN);
  event = key(SDLK_END);
  engine.Event(&event);
  event = key(SDLK_RETURN);
  engine.Event(&event);
  require(!engine.GetSettingsMenu().isOpen(), "Apply/save closes settings");
  require(engine.Iterate() == SDL_APP_CONTINUE,
          "Render game and tools after apply");
  GameMenu mainMenu(engine);
  event = key(SDLK_DOWN);
  mainMenu.onEvent(event);
  mainMenu.onEvent(event);
  mainMenu.onEvent(key(SDLK_RETURN));
  require(engine.GetSettingsMenu().isOpen(),
          "Main menu defaults to engine settings");
  engine.GetSettingsMenu().close();
  InGameMenu pauseMenu(engine);
  pauseMenu.open();
  pauseMenu.onEvent(event);
  pauseMenu.onEvent(event);
  pauseMenu.onEvent(key(SDLK_RETURN));
  require(engine.GetSettingsMenu().isOpen(),
          "Pause menu defaults to engine settings");
  TimelineScript timeline;
  timeline.duration = 2;
  engine.GetTimeline().play(timeline);
  engine.setAgentPaused(true);
  rejects([&] { engine.advanceTimeline(1); },
          "Agent stepping cannot bypass the settings modal");
  require(engine.GetTimeline().seconds() == 0,
          "Settings prevent temporal advancement through API");
  engine.GetSettingsMenu().close();
  engine.GetTimeline().stop();
  engine.setAgentPaused(false);
}

SDL_Color readPixel(RendererRef renderer, TextureRef texture, int x, int y) {
  auto *target = SDL_GetRenderTarget(&renderer);
  require(SDL_SetRenderTarget(&renderer, &texture), "Read output target");
  SDL_Rect pixel{x, y, 1, 1};
  SurfacePtr image{SDL_RenderReadPixels(&renderer, &pixel)};
  require(SDL_SetRenderTarget(&renderer, target),
          "Restore target after readback");
  require(bool(image), "Read output pixels");
  SDL_Color color{};
  require(SDL_ReadSurfacePixel(image.get(), 0, 0, &color.r, &color.g, &color.b,
                               &color.a),
          "Decode output pixel");
  return color;
}

void testUpscaler(Engine &engine, UpscaleBackend backend) {
  auto &renderer = engine.GetRenderer();
  auto &upscaler = engine.GetUpscaler();
  TexturePtr source{SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, 32, 18)};
  require(bool(source), "Create upscaler source");
  std::vector<Uint8> pixels(32 * 18 * 4);
  auto upload = [&](SDL_Color color) {
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
      pixels[i] = color.r;
      pixels[i + 1] = color.g;
      pixels[i + 2] = color.b;
      pixels[i + 3] = color.a;
    }
    require(SDL_UpdateTexture(source.get(), nullptr, pixels.data(), 32 * 4),
            "Upload source");
  };
  upscaler.configure({});
  require(&upscaler.process(*source, 1) == source.get(),
          "Disabled upscaler passes through");
  upscaler.configure({backend, UpscalePreset::Anime4KModeAFast, 2, {}});
  const auto properties = SDL_GetRendererProperties(&renderer);
  const auto maximum = SDL_GetNumberProperty(
      properties, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
  SDL_SetNumberProperty(properties, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER,
                        16);
  require(&upscaler.process(*source, 0) == source.get(),
          "An image that exceeds upscale limits falls back to its original");
  SDL_SetNumberProperty(properties, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER,
                        maximum);
  require(&upscaler.process(*source, 0) == source.get(),
          "A failed static image is skipped rather than retried every draw");
  upscaler.clear();
  // The forced failure above is expected. Shader-profile validation below
  // checks only logs emitted while executing the real GPU chains.
  Log::clear();
  const int count = backend == UpscaleBackend::MPV
                        ? static_cast<int>(UpscalePresets().size())
                        : 1;
  for (int preset = 0; preset < count; ++preset) {
    UpscaleSettings settings{backend, UpscalePresets()[preset], 2, {}};
    upscaler.configure(settings);
    const Upscaler *implementation = upscaler.getImplementation();
    require(implementation && implementation->backend() == backend,
            "Controller uses a concrete generic Upscaler implementation");
    require(implementation->isAvaliable(),
            "Concrete upscaler reports availability through the base API");
    if (backend == UpscaleBackend::MPV)
      require(dynamic_cast<const OpenGLUpscaler *>(implementation) != nullptr,
              "MPV executes through the OpenGLUpscaler subclass");
    if (backend == UpscaleBackend::MetalFX)
      require(dynamic_cast<const MetalFXUpscaler *>(implementation) != nullptr,
              "MetalFX executes through the MetalFXUpscaler subclass");
    upload({240, 10, 10, 255});
    auto &output = upscaler.process(*source, 1);
    require(output.w == 64 && output.h == 36,
            "Upscale dimensions are exactly 2x");
    auto red = readPixel(renderer, output, 32, 18);
    require(red.r > 150 && red.r > red.b + 100,
            "Backend renders red without channel swaps");
    require(&upscaler.process(*source, 1) == &output,
            "Unchanged frame reuses cached output");
    TexturePtr second(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, 32, 18));
    require(bool(second) &&
                SDL_UpdateTexture(second.get(), nullptr, pixels.data(), 32 * 4),
            "Create another cached image");
    auto &secondOutput = upscaler.process(*second, 1);
    require(&secondOutput != &output &&
                &upscaler.process(*source, 1) == &output,
            "Alternating images retain separate cached upscale textures");
    bool secondCacheAlive = true;
    SDL_SetPointerPropertyWithCleanup(
        SDL_GetTextureProperties(&secondOutput), "test.cache-lifetime",
        &secondCacheAlive,
        [](void *, void *value) { *static_cast<bool *>(value) = false; },
        nullptr);
    second.reset();
    require(!secondCacheAlive,
            "Destroying an original releases its cached upscale");
    upload({10, 240, 10, 255});
    const auto retained =
        readPixel(renderer, upscaler.process(*source, 1), 32, 18);
    require(retained.r > 150 && retained.r > retained.g + 100,
            "An unchanged content serial never reprocesses source pixels after "
            "drawing another image");
    upload({10, 10, 240, 255});
    auto &updated = upscaler.process(*source, 2);
    auto blue = readPixel(renderer, updated, 32, 18);
    require(blue.b > 150 && blue.b > blue.r + 100,
            "New frame serial invalidates cached pixels");

    if (backend != UpscaleBackend::MetalFX) {
      upload({20, 40, 100, 0});
      const auto alpha =
          readPixel(renderer, upscaler.process(*source, 3), 32, 18);
      require(alpha.a == 0,
              "Transparent frame does not retain previous opaque frame");
    }
    settings.factor = 1.5f;
    upscaler.configure(settings);
    auto &resized = upscaler.process(*source, 4);
    require(resized.w == 48 && resized.h == 27,
            "Factor change recreates correctly sized output");
    bool cacheAlive = true;
    SDL_SetPointerPropertyWithCleanup(
        SDL_GetTextureProperties(&resized), "test.cache-lifetime", &cacheAlive,
        [](void *, void *value) { *static_cast<bool *>(value) = false; },
        nullptr);
    settings.factor = 1;
    upscaler.configure(settings);
    require(!cacheAlive,
            "Disabling upscaling immediately releases cached GPU textures");
    require(&upscaler.process(*source, 5) == source.get(),
            "1x disables processing");
  }
  if (backend == UpscaleBackend::MPV) {
    TemporaryDirectory custom;
    const auto first = custom.path / "first custom shader.glsl";
    const auto second = custom.path / "second custom shader.glsl";
    std::ofstream(first)
        << "//!HOOK MAIN\n//!BIND HOOKED\n"
           "vec4 hook() { return vec4(0.0, 1.0, 0.0, 1.0); }\n";
    std::ofstream(second)
        << "//!HOOK MAIN\n//!BIND HOOKED\n"
           "vec4 hook() { return vec4(HOOKED_tex(HOOKED_pos).g, 0.0, 0.0, "
           "1.0); }\n";
    UpscaleSettings settings{
        backend, UpscalePreset::Anime4K, 2,
        UpscaleShaderFileList({first.string(), second.string()})};
    upscaler.configure(settings);
    upload({0, 0, 255, 255});
    auto color = readPixel(renderer, upscaler.process(*source, 6), 32, 18);
    require(
        color.r > 240 && color.g < 10 && color.b < 10,
        "Selected absolute shader paths execute in list order, not the preset");
    for (const auto &entry : Log::entries())
      require(entry.priority < SDL_LOG_PRIORITY_WARN,
              "GPU presets execute without shader errors or fallback");
  }
  upscaler.clear();
}

void testNativeWindow(Engine &engine) {
  auto settings = engine.GetSettings();
  settings.width = 960;
  settings.height = 540;
  settings.resizable = false;
  settings.vsync = true;
  engine.applySettings(settings);
  require(SDL_SyncWindow(&engine.GetWindow()),
          "Wait for native window settings");
  int width = 0, height = 0, vsync = 0;
  require(
      SDL_GetWindowSize(&engine.GetWindow(), &width, &height) && width == 960 &&
          height == 540 &&
          !(SDL_GetWindowFlags(&engine.GetWindow()) & SDL_WINDOW_RESIZABLE) &&
          SDL_GetRenderVSync(&engine.GetRenderer(), &vsync) && vsync == 1,
      "Native window applies size, resizable and VSync settings");
}

void testMedia(Engine &engine, const std::string &path,
               UpscaleBackend backend) {
  auto &renderer = engine.GetRenderer();
  auto &upscaler = engine.GetUpscaler();
  upscaler.configure({backend, UpscalePreset::Anime4K, 2, {}});
  TexturePtr target{SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 1280, 720)};
  require(bool(target) && SDL_SetRenderTarget(&renderer, target.get()),
          "Set video output target");
  {
    MediaPlayer media;
    media.open(path, false);
    for (double position : {0.0, 0.1, 0.2}) {
      media.update(renderer, position);
      require(media.hasVideoFrame(), "Decode source video frame");
      media.render(renderer, &upscaler);
      const auto pixel = readPixel(renderer, *target, 640, 360);
      require(pixel.r || pixel.g || pixel.b,
              "Video is visible after native upscaling");
    }
    media.stop();
    media.open(path, false);
    media.update(renderer, 0);
    media.render(renderer, &upscaler);
  }
  require(SDL_SetRenderTarget(&renderer, nullptr),
          "Restore window after video");
  upscaler.clear();
}

void testRenderState(Engine &engine) {
  auto &renderer = engine.GetRenderer();
  require(!OpenGLUpscaler::isAvaliable(renderer) &&
              !MetalFXUpscaler::isAvaliable(renderer),
          "Native availability probes reject software without constructing a "
          "pipeline");
  rejects([&] { OpenGLUpscaler wrongRenderer(renderer); },
          "OpenGL subclass rejects a software renderer");
  rejects([&] { MetalFXUpscaler wrongRenderer(renderer); },
          "MetalFX subclass rejects a software renderer");
  auto &upscaler = engine.GetUpscaler();
  upscaler.configure({UpscaleBackend::Linear, UpscalePreset::Anime4K, 2, {}});
  TexturePtr target{SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 100, 80)};
  TexturePtr source{SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, 4, 2)};
  require(bool(target) && bool(source), "Create state preservation textures");
  require(SDL_SetRenderTarget(&renderer, target.get()), "Set caller target");
  require(SDL_SetRenderLogicalPresentation(&renderer, 50, 40,
                                           SDL_LOGICAL_PRESENTATION_STRETCH),
          "Set caller presentation");
  const SDL_Rect viewport{2, 3, 40, 30}, clip{1, 2, 10, 12};
  require(SDL_SetRenderViewport(&renderer, &viewport) &&
              SDL_SetRenderClipRect(&renderer, &clip) &&
              SDL_SetRenderScale(&renderer, 0.5f, 0.75f) &&
              SDL_SetRenderDrawColor(&renderer, 11, 22, 33, 44) &&
              SDL_SetTextureBlendMode(source.get(), SDL_BLENDMODE_ADD),
          "Set caller render state");
  upscaler.process(*source, 1);
  SDL_Rect actualViewport{}, actualClip{};
  float x = 0, y = 0;
  Uint8 r = 0, g = 0, b = 0, a = 0;
  SDL_BlendMode blend;
  int width = 0, height = 0;
  SDL_RendererLogicalPresentation presentation;
  require(SDL_GetRenderTarget(&renderer) == target.get() &&
              SDL_GetRenderViewport(&renderer, &actualViewport) &&
              SDL_GetRenderClipRect(&renderer, &actualClip) &&
              SDL_GetRenderScale(&renderer, &x, &y) &&
              SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a) &&
              SDL_GetTextureBlendMode(source.get(), &blend) &&
              SDL_GetRenderLogicalPresentation(&renderer, &width, &height,
                                               &presentation),
          "Get restored caller render state");
  require(SDL_RectsEqual(&viewport, &actualViewport) &&
              SDL_RectsEqual(&clip, &actualClip) && x == 0.5f && y == 0.75f &&
              r == 11 && g == 22 && b == 33 && a == 44 &&
              blend == SDL_BLENDMODE_ADD && width == 50 && height == 40 &&
              presentation == SDL_LOGICAL_PRESENTATION_STRETCH,
          "Upscaling preserves target, viewport, clip, scale, presentation, "
          "color and blending");
  require(SDL_SetRenderTarget(&renderer, nullptr), "Restore window");
  upscaler.clear();
}
} // namespace

int main(int argc, char **argv) {
  try {
    Kotonoha::OrsDocument emptyOrs(Kotonoha_OrsParserBytes(nullptr, 0));
    Kotonoha::OrsDocument movedOrs(std::move(emptyOrs));
    require(emptyOrs.get().data == nullptr && movedOrs.get().size == 0,
            "Parser resource ownership moves safely");
    TemporaryDirectory temporary;
    if (argc == 1) {
      testLog(temporary.path);
      testSettings(temporary.path);
    }
    EventRuntime runtime;
    EngineOptions options;
    options.settingsPath = (temporary.path / "engine.json").string();
    if (argc > 1)
      options.rendererOverride = argv[1];
    Engine engine(runtime, options);
    require(engineAssetDirectory() == resourceDirectory() / "assets" &&
                gameAssetDirectory("example") ==
                    resourceDirectory() / "game-assets",
            "Packaged engine and game assets have separate stable directories");
    require(preferredDataDirectory("example") == resourceDirectory(),
            "Desktop data resolves to the package resource directory");
    require(
        std::filesystem::is_directory(preferredDocumentsDirectory("example")) &&
            std::filesystem::path(EngineSettings::defaultPath("example"))
                    .parent_path() == preferredDocumentsDirectory("example"),
        "Settings use the writable preferred documents directory");
    rejects([] { gameAssetDirectory("../game"); },
            "Reject invalid game identifiers");
    SDL_HideWindow(&engine.GetWindow());
    if (argc == 1) {
      testWindowAndMenu(engine, runtime, options.settingsPath);
      testKtrfRuntime(engine, temporary.path);
      testRenderState(engine);
      testControllerNavigation(engine, runtime);
      testModalInputRouting(engine);
      testContinuousTouch(engine, runtime);
      testTouchSettings(engine, runtime);
      testUnchangedWindowGeometry(engine);
    } else
      testNativeWindow(engine);
    const auto backend = argc == 1 ? UpscaleBackend::Linear
                         : std::string(argv[1]) == "metal"
                             ? UpscaleBackend::MetalFX
                             : UpscaleBackend::MPV;
    testUpscaler(engine, backend);
    if (argc > 2)
      testMedia(engine, argv[2], backend);
    std::cout << "Settings, logs, window/input and upscaler checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
