#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>
#include <Kotonoha/media/upscaling/MetalFXUpscaler.hpp>
#include <Kotonoha/media/upscaling/OpenGLUpscaler.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>
#include <Kotonoha/script/KtrfScript.hpp>

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
  const std::vector<std::string> shaders{
      "/tmp/custom shaders/first.glsl", "/tmp/colon:name.glsl",
      "C:\\shaders\\second.glsl"};
  require(UpscaleShaderFiles(UpscaleShaderFileList(shaders)) == shaders,
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
  settings.save(file);
  const auto read = EngineSettings::load(file);
  require(read.width == 1600 && read.height == 900 && read.display == 2 &&
              read.mode == WindowMode::Exclusive &&
              read.fullscreenWidth == 1920 && read.fullscreenHeight == 1080 &&
              std::abs(read.refreshRate - 59.94f) < 0.001f && read.vsync &&
              !read.resizable && read.renderer == "opengl" &&
              read.upscaling.backend == UpscaleBackend::MPV &&
              read.upscaling.preset == UpscalePreset::ArtCNNDenoise &&
              read.upscaling.factor == 1.5f &&
              read.upscaling.shaderFiles == "custom.glsl" &&
              read.logPriority == SDL_LOG_PRIORITY_DEBUG &&
              read.masterVolume == 0.4f && read.masterMuted &&
              read.debugUIScale == 1.75f &&
              read.translationPath == "trans.json" &&
              read.fontPath == "font.ttf",
          "Every settings field survives persistence");
  settings.width = 0;
  rejects([&] { settings.save(file); }, "Validate before overwriting settings");
  require(EngineSettings::load(file).width == 1600,
          "Invalid save leaves file intact");
  for (const char *invalid :
       {"[]", "{", "{}garbage", "{\"width\": \"1280\"}",
        "{\"width\":9223372036854775807}", "{\"mode\":99}", "{\"upscaler\":99}",
        "{\"preset\":-1}", "{\"version\":2}", "{\"vsync\":1}",
        "{\"factor\":0.5}", "{\"factor\":1e300}", "{\"refreshRate\":-1}",
        "{\"logPriority\":0}", "{\"fullscreenWidth\":1920}",
        "{\"renderer\":\"x\\u0000y\"}", "{\"masterVolume\":-0.1}",
        "{\"masterVolume\":1.1}", "{\"masterVolume\":1e300}",
        "{\"masterMuted\":1}", "{\"debugUIScale\":0.9}",
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
  require(GetVNTextBoxRuntime(engine, "", "", 30).getText() == "Customized runtime dialogue",
          "Loaded KTRF changes the live dialogue");
  edited.nodes[0].nextNodeId = 9999;
  rejects([&] { edited.dump(path); }, "Invalid graph cannot replace the file");
  std::ofstream(path) << "{\"profile\":\"kotonoha.vn\",\"version\":1,\"nodes\":[]}";
  rejects([&] { engine.loadKtrf(path); }, "Invalid loaded graph is rejected");
  require(manager.getCurrentNodeId() == 0,
          "Rejected KTRF preserves the current runtime graph");
  require(GetVNTextBoxRuntime(engine, "", "", 30).getText() == "Customized runtime dialogue",
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
  EventRuntime() : Runtime("Infrastructure tests", "1", "org.kotonoha.infra") {}
  void onEvent(EventRef) override { ++novelEvents; }
  bool onScreenEvent(EventRef) override {
    ++screenEvents;
    return false;
  }
};

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
  for (int i = 0; i < 9; ++i)
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
  const int count = backend == UpscaleBackend::MPV ? 4 : 1;
  for (int preset = 0; preset < count; ++preset) {
    UpscaleSettings settings{
        backend, static_cast<UpscalePreset>(preset), 2, {}};
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
    settings.factor = 1;
    upscaler.configure(settings);
    require(&upscaler.process(*source, 5) == source.get(),
            "1x disables processing");
  }
  if (backend == UpscaleBackend::MPV) {
    TemporaryDirectory custom;
    const auto first = custom.path / "first custom shader.glsl";
    const auto second = custom.path / "second custom shader.glsl";
    std::ofstream(first) <<
        "//!HOOK MAIN\n//!BIND HOOKED\n"
        "vec4 hook() { return vec4(0.0, 1.0, 0.0, 1.0); }\n";
    std::ofstream(second) <<
        "//!HOOK MAIN\n//!BIND HOOKED\n"
        "vec4 hook() { return vec4(HOOKED_tex(HOOKED_pos).g, 0.0, 0.0, 1.0); }\n";
    UpscaleSettings settings{backend, UpscalePreset::Anime4K, 2,
                             UpscaleShaderFileList({first.string(), second.string()})};
    upscaler.configure(settings);
    upload({0, 0, 255, 255});
    auto color = readPixel(renderer, upscaler.process(*source, 6), 32, 18);
    require(color.r > 240 && color.g < 10 && color.b < 10,
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
                gameAssetDirectory("example") == resourceDirectory() / "game-assets",
            "Packaged engine and game assets have separate stable directories");
    require(preferredDataDirectory("example") == resourceDirectory(),
            "Desktop data resolves to the package resource directory");
    require(std::filesystem::is_directory(preferredDocumentsDirectory("example")) &&
                std::filesystem::path(EngineSettings::defaultPath("example")).parent_path() ==
                    preferredDocumentsDirectory("example"),
            "Settings use the writable preferred documents directory");
    rejects([] { gameAssetDirectory("../game"); }, "Reject invalid game identifiers");
    SDL_HideWindow(&engine.GetWindow());
    if (argc == 1) {
      testWindowAndMenu(engine, runtime, options.settingsPath);
      testKtrfRuntime(engine, temporary.path);
      testRenderState(engine);
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
