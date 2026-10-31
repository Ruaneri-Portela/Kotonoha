#include <Kotonoha/componentes/FadeScene.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Kotonoha;
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char *message) {
  bool failed = false;
  try { action(); } catch (const std::exception &) { failed = true; }
  require(failed, message);
}
void parse(CommandLine &parser, std::initializer_list<const char *> values) {
  std::vector<std::string> strings;
  for (const char *value : values) strings.emplace_back(value);
  std::vector<char *> argv;
  for (auto &value : strings) argv.push_back(value.data());
  parser.parse(static_cast<int>(argv.size()), argv.data());
}
void arguments() {
  EngineOptions options;
  options.persistSettings = true;
  CommandLine parser("TestGame", options);
  std::string game;
  bool launcher = false;
  parser.option("--game", "DIRECTORY", "Game-specific data", game);
  parser.alias("--game", "--assets");
  parser.flag("--launcher", "Game-specific launcher", launcher);
  parse(parser, {"game", "--assets=data", "--subtitle-style", "style.ass",
                 "--frames", "12", "--no-config", "--mcp-root", "/tmp", "--launcher", "-h"});
  require(game == "data" && launcher && parser.frames == 12 &&
          options.subtitleStylePath == "style.ass" && !options.persistSettings &&
          options.mcpFileRoot == "/tmp" && parser.wantsHelp(),
          "Shared and game-specific arguments and aliases");
  const auto help = parser.help();
  require(help.find("--game DIRECTORY") != std::string::npos &&
          help.find("--subtitle-style FILE.ass") != std::string::npos,
          "Help includes engine and game options");
  parse(parser, {"game", "--ass-style=alias.ass"});
  require(options.subtitleStylePath == "alias.ass", "--ass-style aliases --subtitle-style");
  for (const char *count : {"0", "-1", "3oops", "99999999999999999999999999"})
    rejects([&] { parse(parser, {"game", "--frames", count}); }, "Strict frame count");
  rejects([&] { parse(parser, {"game", "--unknown"}); }, "Unknown option");
  rejects([&] { parse(parser, {"game", "--game"}); }, "Missing game value");
  rejects([&] { parse(parser, {"game", "--game", "--help"}); }, "Flag is not a value");
  rejects([&] { parse(parser, {"game", "--mcp=true"}); }, "Flag does not accept a value");
  rejects([&] { parser.flag("--mcp", "duplicate", launcher); }, "Duplicate registration");
}

std::string styleFile() {
  return "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n"
      "Title: Original script metadata\n[V4+ Styles]\n"
      "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
      "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
      "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
      "Style: Default,Noto Sans JP,46,&H000000FF,&H000000FF,&H00000000,&H00000000,"
      "0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
      "Style: Person,Noto Sans JP,46,&H0000FF00,&H000000FF,&H00000000,&H00000000,"
      "0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
      "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
      "Dialogue: 0,0:00:00.00,0:01:00.00,Default,,0,0,0,,DISCARDED ORIGINAL TEXT\n"
      "[Fonts]\nfontname: NotImported.ttf\n";
}

struct TestRuntime : Runtime {
  TestRuntime() : Runtime("Presentation tests", "1", "org.kotonoha.presentation.tests") {}
  std::string debugNode() const override { return "custom-scene"; }
  std::optional<double> debugNodeSeconds() const override { return 12.375; }
};

int colored(RendererRef renderer, int color) {
  SurfacePtr image(SDL_RenderReadPixels(&renderer, nullptr));
  require(bool(image), "Read subtitle pixels");
  int result = 0;
  for (int y = 0; y < image->h; ++y)
    for (int x = 0; x < image->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      require(SDL_ReadSurfacePixel(image.get(), x, y, &r, &g, &b, &a), "Read pixel");
      if (color == 0 ? (r > 140 && g < 80) : (g > 140 && r < 80))
        ++result;
    }
  return result;
}
void subtitles(Engine &engine) {
  const auto style = AssStyle::parse(styleFile());
  require(style.header.find("Original script metadata") != std::string::npos &&
          style.names.size() == 2 &&
          style.header.find("DISCARDED") == std::string::npos &&
          style.header.find("[Events]") == std::string::npos &&
          style.header.find("[Fonts]") == std::string::npos,
          "Import script info and styles, discard all original texts and attachments");
  rejects([] { AssStyle::parse("[Script Info]\n[V4+ Styles]\n"); }, "Missing style rejected");
  rejects([] { AssStyle::load("/nonexistent/style.ass"); }, "Missing style file rejected");
  auto &renderer = engine.GetRenderer();
  SubtitleScene scene(engine);
  scene.setStyle(style);
  scene.setCues({{1, 2, "NEW CUE", "Person"}});
  const auto draw = [&](double position) {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    scene.setPosition(position);
    scene.render(renderer, 1);
  };
  draw(0.5);
  require(colored(renderer, 0) == 0 && colored(renderer, 1) == 0,
          "Imported dialogue was discarded; cue not started");
  draw(1.5);
  require(colored(renderer, 1) > 100 && colored(renderer, 0) == 0,
          "libass renders new text with the matching speaker style");
  draw(2);
  require(colored(renderer, 1) == 0, "End time is exclusive");
  draw(1.2);
  require(colored(renderer, 1) > 100, "Seeking backwards restores subtitle");
  SDL_SetRenderScale(&renderer, 0.5f, 0.5f);
  draw(1.5);
  require(colored(renderer, 1) > 20, "Subtitle resizes with renderer scale");
  SDL_SetRenderScale(&renderer, 1, 1);
  rejects([&] { scene.setPosition(-1); }, "Reject negative subtitle clock");
  rejects([&] { scene.setCues({{2, 1, "bad", ""}}); }, "Reject reversed cue");
  scene.clear();
  draw(1.5);
  require(colored(renderer, 1) == 0, "Clearing removes the track");
}

void controls(Engine &engine, TestRuntime &runtime) {
  auto settings = engine.GetSettings();
  settings.debugUIScale = 2;
  settings.masterVolume = 0.35f;
  engine.applySettings(settings, false);
  require(engine.GetAudio().getMasterVolume() == 0.35f, "Engine applies master volume");
  SettingsMenu menu(engine);
  const auto press = [&](SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    menu.onEvent(event);
  };
  // Software rendering has no MPV-only rows: volume follows the scale row.
  menu.open();
  for (int i = 0; i < 7; ++i) press(SDLK_DOWN);
  press(SDLK_LEFT);
  require(engine.GetAudio().getMasterVolume() == 0.35f, "Volume draft does not apply early");
  press(SDLK_ESCAPE);
  menu.open();
  for (int i = 0; i < 7; ++i) press(SDLK_DOWN);
  press(SDLK_LEFT);
  press(SDLK_DOWN);
  press(SDLK_RETURN);
  press(SDLK_DOWN);
  press(SDLK_RETURN);
  require(!menu.isOpen() && engine.GetAudio().getMasterVolume() == 0.30f &&
          engine.GetAudio().isMasterMuted(),
          "Generic menu applies volume and master mute");
  engine.applySettings(settings, false);
  engine.GetDebug().renderMenu(engine.GetRenderer());
  const float padding = ImGui::GetStyle().FramePadding.x;
  engine.GetDebug().renderMenu(engine.GetRenderer());
  require(padding >= 16 && ImGui::GetStyle().FramePadding.x == padding,
          "Debug UI enlargement is not cumulative");
  runtime.SetState(GameState::Gameplay);
  require(engine.GetDebug().nodeClockText().find("12.38s") != std::string::npos,
          "Custom node HUD shows runtime clock");
  const auto performance = engine.GetDebug().performanceText();
  require(performance.find("FPS: ") == 0 && performance.find(" ms") != std::string::npos,
          "Performance HUD reports both FPS and milliseconds per frame");
}
void resolver() {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() /
      ("kotonoha-resolver-" + std::to_string(SDL_GetPerformanceCounter()));
  const fs::path previous = fs::current_path();
  fs::create_directories(root / "games/resolver-test/game-assets/data");
  writeTextFileAtomically((root / "games/resolver-test/game-assets/data/a.txt").string(), "a");
  fs::current_path(root);
  try {
    const auto roots = FileResolver::roots();
    require(std::find(roots.begin(), roots.end(), fs::current_path()) != roots.end(),
            "Resolver searches the working directory");
    const auto asset = FileResolver::findGameAsset("resolver-test", "data/a.txt");
    require(asset && fs::equivalent(*asset, root / "games/resolver-test/game-assets/data/a.txt"),
            "Resolver finds source-tree game-assets");
    require(!FileResolver::find("does/not/exist.bin"), "Missing files are not found");
    bool listed = false;
    try { FileResolver::resolve("does/not/exist.bin"); }
    catch (const std::exception &error) {
      listed = std::string(error.what()).find("does/not/exist.bin") != std::string::npos;
    }
    require(listed, "resolve() reports the searched locations");
    require(fs::exists(FileResolver::font()), "Engine font resolves from assets/fonts");
  } catch (...) {
    fs::current_path(previous);
    fs::remove_all(root);
    throw;
  }
  fs::current_path(previous);
  fs::remove_all(root);
}
void fades(Engine &engine) {
  FadeScene::Event out{1, 3, {0, 0, 0, 255}, FadeScene::Direction::Out, true};
  require(FadeScene::alphaAt(out, 0.5) == 0 && std::fabs(FadeScene::alphaAt(out, 2) - 0.5f) < 0.01f &&
              FadeScene::alphaAt(out, 4) == 1,
          "Fade-out ramps clear to opaque and holds");
  FadeScene::Event in{0, 2, {255, 255, 255, 255}, FadeScene::Direction::In};
  require(FadeScene::alphaAt(in, 0) == 1 && FadeScene::alphaAt(in, 2) == 0,
          "Fade-in ramps opaque to clear");
  FadeScene fade(engine);
  fade.add(out);
  fade.setPosition(2);
  require(fade.active() && fade.alpha() > 0.4f, "Timeline fade is active mid-event");
  fade.setPosition(5);
  require(!fade.active() && fade.alpha() == 1, "Held fade stays opaque but finished");
  fade.render(engine.GetRenderer(), 1);
}
} // namespace

int main() {
  try {
    arguments();
    resolver();
    TestRuntime runtime;
    struct StyleFile {
      std::filesystem::path path = std::filesystem::temp_directory_path() /
          ("kotonoha-style-" + std::to_string(SDL_GetPerformanceCounter()) + ".ass");
      StyleFile() { writeTextFileAtomically(path.string(), styleFile()); }
      ~StyleFile() { std::error_code error; std::filesystem::remove(path, error); }
    } style;
    EngineOptions options;
    options.rendererOverride = "software";
    options.subtitleStylePath = style.path.string();
    Engine engine(runtime, options);
    require(engine.GetSubtitleStyle().has_value(), "Startup loads the selected style file");
    subtitles(engine);
    controls(engine, runtime);
    fades(engine);
    std::cout << "Extensible arguments/help, ASS style import/rendering and debug scaling passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Presentation tests: " << error.what() << '\n';
    return 1;
  }
}
