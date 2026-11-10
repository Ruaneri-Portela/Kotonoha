#include "RealLiveMouseCursor.hpp"
#include "RealLiveRuntime.hpp"
#include "RealLiveTransition.hpp"
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/SurfaceBlend.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <RealLive/Images.hpp>
#include <RealLive/Script.hpp>
#include <RealLive/Voice.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
using namespace Kotonoha;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
VNTextBox *text(Runtime &game) {
  for (auto *s : game.GetActiveScenes())
    if (auto *t = dynamic_cast<VNTextBox *>(s))
      return t;
  return nullptr;
}
SDL_Event pad(Uint8 button, bool down) {
  SDL_Event event{};
  event.type =
      down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
  event.gbutton.button = button;
  event.gbutton.down = down;
  return event;
}
SurfacePtr capture(Engine &engine, Runtime &game, const char *name) {
  auto &r = engine.GetRenderer();
  TexturePtr target(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 960, 720));
  SDL_SetRenderTarget(&r, target.get());
  game.render(r, 1);
  game.renderOverlay(r, 1);
  SurfacePtr s(SDL_RenderReadPixels(&r, nullptr));
  check(bool(s), "Capture failed");
  SDL_SaveBMP(s.get(), name);
  SDL_SetRenderTarget(&r, nullptr);
  return s;
}
void verifyDitherSampling(RendererRef renderer) {
  TexturePtr mask(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, 8, 8));
  check(bool(mask), "Create dither mask reference");
  const auto blend = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
  if (!SDL_SetTextureBlendMode(mask.get(), blend)) {
    std::cout << "Dither GPU comparison skipped: renderer uses native effect "
                 "pixels\n";
    return;
  }
  check(SDL_SetTextureScaleMode(mask.get(), SDL_SCALEMODE_NEAREST),
        "Dither masks must not interpolate script cells");
  auto *previous = SDL_GetRenderTarget(&renderer);
  for (int scale : {1, 2, 4}) {
    TexturePtr old(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_TARGET, 16 * scale,
                                     16 * scale));
    TexturePtr output(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 16 * scale,
                                        16 * scale));
    check(bool(old) && bool(output) &&
              SDL_SetTextureBlendMode(old.get(), SDL_BLENDMODE_BLEND),
          "Create isolated dither composition targets");
    for (int style : {4, 5})
      for (double progress : {0.0, 0.125, 0.5, 0.875, 1.0}) {
        std::array<Uint8, 8 * 8 * 4> pixels{};
        for (int y = 0; y < 8; ++y)
          for (int x = 0; x < 8; ++x)
            pixels[(y * 8 + x) * 4 + 3] =
                RealLive::transitionRevealsNewPixel(style, progress, x, y)
                    ? 0
                    : 255;
        check(SDL_UpdateTexture(mask.get(), nullptr, pixels.data(), 8 * 4) &&
                  SDL_SetRenderTarget(&renderer, old.get()) &&
                  SDL_SetRenderDrawColor(&renderer, 255, 255, 255, 255) &&
                  SDL_RenderClear(&renderer) &&
                  SDL_RenderTextureTiled(&renderer, mask.get(), nullptr,
                                         float(scale), nullptr) &&
                  SDL_SetRenderTarget(&renderer, output.get()) &&
                  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255) &&
                  SDL_RenderClear(&renderer) &&
                  SDL_RenderTexture(&renderer, old.get(), nullptr, nullptr),
              "Compose the original REALLIVE dither mask on the GPU");
        SurfacePtr actual(SDL_RenderReadPixels(&renderer, nullptr));
        check(bool(actual), "Read dither mask composition");
        for (int y = 0; y < actual->h; ++y)
          for (int x = 0; x < actual->w; ++x) {
            Uint8 r, g, b, a;
            check(SDL_ReadSurfacePixel(actual.get(), x, y, &r, &g, &b, &a),
                  "Read dither cell");
            const auto expected = RealLive::transitionRevealsNewPixel(
                                      style, progress, x / scale, y / scale)
                                      ? 0
                                      : 255;
            check(r == expected && g == expected && b == expected && a == 255,
                  "GPU dither must preserve every original cell and endpoint "
                  "at all factors");
          }
      }
    check(SDL_SetRenderTarget(&renderer, previous),
          "Restore dither comparison target");
  }
}
void verifyCompatibilityNode(std::shared_ptr<RealLive::RealLiveAssets> assets,
                             const std::string &node) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  options.assets = std::move(assets);
  options.persistSettings = false;
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    ~Guard() { game.shutdown(); }
  } guard{game};
  SDL_SetRenderVSync(&engine.GetRenderer(), 0);
  game.debugJumpToNode(node);
  game.update();
  check(game.diagnostic().empty(), game.diagnostic().c_str());
  check(game.debugNode() != node,
        "Original REALLIVE compatibility node did not execute");
  std::cout << "Original REALLIVE compatibility passed at " << game.debugNode()
            << '\n';
}
// Locate the script node that invokes the REALLIVE 8005 minigame. The
// translated Seen.txt shifts scenario indices, so the caller must be found
// dynamically instead of hardcoding a node such as "170:580". Session::flow
// treats opcodes 11/12/18 (module 1/6) as cross-scenario jumps/calls whose
// first parameter is the target scenario number.
std::string find8005Entry(RealLive::ScriptArchive archive) {
  RLMachine machine;
  for (int scenarioNum : archive.ScenarioNumbers()) {
    const auto &scenario = archive.Scenario(scenarioNum);
    std::size_t index = 0;
    for (const auto &element : scenario) {
      if (const auto *command =
              dynamic_cast<const libreallive::CommandElement *>(
                  element.get())) {
        if (command->modtype() == 0 &&
            (command->module() == 1 || command->module() == 6) &&
            (command->opcode() == 11 || command->opcode() == 12 ||
             command->opcode() == 18) &&
            command->GetParamCount() > 0) {
          try {
            if (RealLive::IntegerParameter(machine, *command, 0) == 8005) {
              return std::to_string(scenarioNum) + ":" + std::to_string(index);
            }
          } catch (const std::exception &) {
            // The first parameter is not a plain scenario number; keep
            // scanning.
          }
        }
      }
      ++index;
    }
  }
  throw std::runtime_error(
      "REALLIVE 8005 caller was not found in the scenario archive");
}
// The REALLIVE 8005 regression tests exercise hardcoded indices inside the
// original scenario 8005 script (up to index 4212). A translated Seen.txt may
// replace that scenario with a much smaller one, so those tests cannot run
// meaningfully against every data set. Return the element count of scenario
// 8005, or 0 when it is unavailable.
std::size_t scenario8005ElementCount(RealLive::ScriptArchive &archive) {
  try {
    return std::distance(archive.Scenario(8005).begin(),
                         archive.Scenario(8005).end());
  } catch (const std::exception &) {
    return 0;
  }
}
// The original scenario 8005 (the REALLIVE minigame) has 4216 elements; the
// 8005 regression tests exercise hardcoded indices up to 4212. A translated
// Seen.txt may replace that scenario with a much smaller one, so skip the
// tests when the data set does not contain the original scenario.
constexpr std::size_t kMin8005ScenarioElements = 4213;
void verify8005Call(std::shared_ptr<RealLive::RealLiveAssets> assets) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  auto scripts = assets->scripts();
  options.assets = assets;
  options.persistSettings = false;
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    ~Guard() { game.shutdown(); }
  } guard{game};
  SDL_SetRenderVSync(&engine.GetRenderer(), 0);
  std::string entry = find8005Entry(std::move(scripts));
  game.debugJumpToNode(entry);
  const auto start = SDL_GetTicks();
  while (game.debugNode().find("8005:") != 0 &&
         SDL_GetTicks() - start < 10000) {
    engine.Iterate();
    check(game.diagnostic().empty(), game.diagnostic().c_str());
    SDL_Delay(1);
  }
  check(game.debugNode().find("8005:") == 0,
        "REALLIVE 8005 must be reached through its real caller");

  // Exercise every executable command index in scenario 8005, so the test
  // covers whichever command call sites the loaded Seen.txt contains rather
  // than hardcoding indices that only exist in the original retail script.
  auto archive = assets->scripts();
  const auto &scenario = archive.Scenario(8005);
  std::size_t exercised = 0, index = 0;
  for (const auto &element : scenario) {
    if (const auto *command =
            dynamic_cast<const libreallive::CommandElement *>(element.get())) {
      const int type = command->modtype(), module = command->module();
      if (type != 1 && !(type == 0 && module == 3) &&
          !(type == 0 && module == 2 && command->opcode() == 1) &&
          !(type == 2 && module == 1 && command->opcode() == 12)) {
        ++index;
        continue;
      }
      game.debugJumpToNode("8005:" + std::to_string(index));
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      ++exercised;
    }
    ++index;
  }
  check(exercised > 0, "REALLIVE 8005 command inventory was not exercised");
  std::cout << "REALLIVE 8005 dispatched " << exercised
            << " command call sites from scenario 8005\n";
}
void verify8005Commands(std::shared_ptr<RealLive::RealLiveAssets> assets) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    ~Guard() { game.shutdown(); }
  } guard{game};
  SDL_SetRenderVSync(&engine.GetRenderer(), 0);
  game.debugJumpToNode(find8005Entry(assets->scripts()));
  const auto start = SDL_GetTicks();
  while (game.debugNode().find("8005:") != 0 &&
         SDL_GetTicks() - start < 10000) {
    engine.Iterate();
    check(game.diagnostic().empty(), game.diagnostic().c_str());
    SDL_Delay(1);
  }
  check(game.debugNode().find("8005:") == 0,
        "REALLIVE 8005 command check must enter through its original caller");

  auto archive = assets->scripts();
  const auto &scenario = archive.Scenario(8005);
  std::size_t index = 0, exercised = 0;
  std::vector<std::string> failures;
  for (const auto &element : scenario) {
    const auto *command =
        dynamic_cast<const libreallive::CommandElement *>(element.get());
    if (!command) {
      ++index;
      continue;
    }
    const int type = command->modtype(), module = command->module();
    if (type != 1 && !(type == 0 && module == 3) &&
        !(type == 0 && module == 2 && command->opcode() == 1) &&
        !(type == 2 && module == 1 && command->opcode() == 12)) {
      ++index;
      continue;
    }
    game.debugJumpToNode("8005:" + std::to_string(index));
    game.debugStep();
    if (!game.diagnostic().empty()) {
      std::ostringstream error;
      error << "REALLIVE 8005 call " << index << " (" << type << ':' << module
            << ':' << command->opcode() << '/' << command->overload()
            << ") failed: " << game.diagnostic();
      failures.push_back(error.str());
    }
    ++exercised;
    ++index;
  }
  check(exercised > 0, "REALLIVE 8005 command inventory was not exercised");
  if (!failures.empty()) {
    std::ostringstream error;
    std::cout << "REALLIVE 8005 dispatched " << exercised
              << " original command call sites\n";
    error << "REALLIVE 8005 has " << failures.size()
          << " call sites blocked by runtime errors:";
    for (const auto &failure : failures)
      error << '\n' << failure;
    throw std::runtime_error(error.str());
  }
  std::cout << "REALLIVE 8005 dispatched " << exercised
            << " original command call sites\n";
}
void verifyAllOpcodes(std::shared_ptr<RealLive::RealLiveAssets> assets) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    ~Guard() { game.shutdown(); }
  } guard{game};

  auto archive = assets->scripts();
  using OpcodeKey = std::tuple<int, int, int, int>;
  struct OpcodeInfo {
    const libreallive::CommandElement *cmd = nullptr;
    int scenario = 0;
    std::size_t index = 0;
    size_t count = 0;
  };
  std::map<OpcodeKey, OpcodeInfo> inventory;

  for (int scenarioNum : archive.ScenarioNumbers()) {
    const auto &scenario = archive.Scenario(scenarioNum);
    std::size_t index = 0;
    for (const auto &element : scenario) {
      if (const auto *command =
              dynamic_cast<const libreallive::CommandElement *>(
                  element.get())) {
        OpcodeKey key{command->modtype(), command->module(), command->opcode(),
                      command->overload()};
        auto &info = inventory[key];
        if (!info.count) {
          info.cmd = command;
          info.scenario = scenarioNum;
          info.index = index;
        }
        ++info.count;
      }
      ++index;
    }
  }

  std::cout << "Inventory contains " << inventory.size()
            << " unique opcode signatures across "
            << archive.ScenarioNumbers().size() << " scenarios.\n";

  std::vector<std::pair<OpcodeKey, std::string>> pending, failures;
  for (const auto &[key, info] : inventory) {
    game.debugJumpToNode(std::to_string(info.scenario) + ":" +
                         std::to_string(info.index));
    game.debugStep();
    std::string diag = game.diagnostic();
    if (diag.find("Comando REALLIVE pendente") != std::string::npos) {
      pending.push_back({key, diag});
    } else if (!diag.empty()) {
      failures.push_back({key, diag});
    }
  }

  std::cout << "FOUND " << pending.size() << " PENDING / UNHANDLED OPCODES:\n";
  for (const auto &[key, diag] : pending) {
    const auto &[type, mod, op, over] = key;
    const auto &info = inventory[key];
    std::cout << "  - " << type << ":" << mod << ":" << op << "/" << over
              << " (count: " << info.count << ", first at " << info.scenario
              << ":" << info.index << ") -> " << diag << "\n";
    RLMachine machine;
    info.cmd->PrintSourceRepresentation(&machine, std::cout);
    std::cout << '\n';
  }
  std::cout << "FOUND " << failures.size()
            << " INCONCLUSIVE / FAILED OPCODE PROBES (isolated execution may "
               "lack script context):\n";
  for (const auto &[key, diag] : failures) {
    const auto &[type, mod, op, over] = key;
    const auto &info = inventory[key];
    std::cout << "  - " << type << ":" << mod << ":" << op << "/" << over
              << " at " << info.scenario << ":" << info.index << " -> " << diag
              << '\n';
  }
  if (!pending.empty() || !failures.empty())
    throw std::runtime_error("REALLIVE opcode audit is incomplete");
}
void verify8005Walk(std::shared_ptr<RealLive::RealLiveAssets> assets,
                    bool shaders = false, bool performance = false) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  if (shaders)
    options.rendererOverride = "opengl";
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    ~Guard() { game.shutdown(); }
  } guard{game};
  verifyDitherSampling(engine.GetRenderer());
  const auto step = [&] {
    check(game.debugStep(), "REALLIVE walking command did not execute");
    check(game.diagnostic().empty(), game.diagnostic().c_str());
  };
  game.debugJumpToNode("8005:1437");
  while (game.debugNode() != "8005:1505")
    step();
  const auto start = [&] {
    engine.setAgentPaused(true);
    game.update();
    engine.setAgentPaused(false);
    game.debugJumpToNode("8005:1531");
    while (game.debugNode() != "8005:1538")
      step();
    game.debugJumpToNode("170:24");
    step();
    text(game)->hide();
  };
  const auto same = [](const SDL_Surface &a, const SDL_Surface &b,
                       SDL_Rect region = {0, 0, 960, 720}) {
    if (a.w != b.w || a.h != b.h)
      return false;
    for (int y = region.y; y < region.y + region.h; ++y)
      if (std::memcmp(
              static_cast<const Uint8 *>(a.pixels) + y * a.pitch + region.x * 4,
              static_cast<const Uint8 *>(b.pixels) + y * b.pitch + region.x * 4,
              region.w * 4) != 0)
        return false;
    return true;
  };
  start();
  auto first = capture(engine, game, "/tmp/reallive-8005-walk-first.bmp");
  game.debugJumpToNode("8005:464");
  while (game.debugNode() != "8005:470")
    step();
  auto shifted = capture(engine, game, "/tmp/reallive-8005-walk-shifted.bmp");
  check(!same(*first, *shifted),
        "REALLIVE object adjustment commands must move the walking sprites");
  game.debugJumpToNode("8005:435");
  step();
  for (const auto node : {"8005:1446", "8005:1456", "8005:445", "8005:1437"}) {
    game.debugJumpToNode(node);
    step();
  }
  auto promoted = capture(engine, game, "/tmp/reallive-8005-walk-promoted.bmp");
  check(same(*shifted, *promoted),
        "REALLIVE background copies must retain offsets "
        "independently of foreground resets");
  game.debugJumpToNode("8005:1437");
  while (game.debugNode() != "8005:1505")
    step();
  start();
  auto centered = capture(engine, game, "/tmp/reallive-8005-walk-centered.bmp");
  check(same(*first, *centered),
        "REALLIVE object initialization must clear offsets "
        "inherited from earlier scenes");
  SDL_Delay(180);
  game.update();
  auto next = capture(engine, game, "/tmp/reallive-8005-walk-next.bmp");
  check(!same(*first, *next),
        "REALLIVE objLoop must advance G00 walking patterns every 164 ms");
  check(!same(*first, *next, {330, 0, 270, 400}),
        "REALLIVE walking legs must advance independently");
  check(!same(*first, *next, {700, 400, 260, 200}),
        "REALLIVE walking shadow must animate with the legs");
  engine.setAgentPaused(true);
  game.update();
  auto paused = capture(engine, game, "/tmp/reallive-8005-walk-paused.bmp");
  SDL_Delay(350);
  game.update();
  auto still = capture(engine, game, "/tmp/reallive-8005-walk-still.bmp");
  check(same(*paused, *still),
        "REALLIVE object loops must freeze while paused");
  engine.setAgentPaused(false);
  SDL_Delay(180);
  game.update();
  auto resumed = capture(engine, game, "/tmp/reallive-8005-walk-resumed.bmp");
  check(!same(*still, *resumed),
        "REALLIVE object loops must resume after pause");

  const auto path =
      std::filesystem::temp_directory_path() /
      ("reallive-walk-" + std::to_string(SDL_GetTicksNS()) + ".vn");
  game.debugJumpToNode("8005:464");
  while (game.debugNode() != "8005:470")
    step();
  game.debugJumpToNode("170:24");
  step();
  text(game)->hide();
  game.saveGame(path.string());
  auto saved = capture(engine, game, "/tmp/reallive-8005-walk-saved.bmp");
  game.debugJumpToNode("8005:1446");
  step();
  game.debugJumpToNode("8005:1456");
  step();
  game.loadGame(path.string());
  text(game)->hide();
  auto restored = capture(engine, game, "/tmp/reallive-8005-walk-restored.bmp");
  check(same(*saved, *restored),
        "REALLIVE saves must restore animation frames and per-object offsets");
  SDL_Delay(180);
  game.update();
  auto continued =
      capture(engine, game, "/tmp/reallive-8005-walk-continued.bmp");
  check(!same(*restored, *continued),
        "REALLIVE restored object loops must continue playing");
  std::filesystem::remove(path);

  auto image = RealLive::decodeG00(assets->read(assets->graphic("OP_D00")));
  check(image.regions.size() == 12,
        "REALLIVE walking G00 must contain 12 patterns");
  start();
  auto restarted =
      capture(engine, game, "/tmp/reallive-8005-walk-restarted.bmp");
  SDL_Delay(Uint32(image.regions.size() * 164 + 20));
  game.update();
  auto looped = capture(engine, game, "/tmp/reallive-8005-walk-looped.bmp");
  check(same(*restarted, *looped),
        "REALLIVE walking animation must wrap to its first G00 pattern");

  const auto nativeSettings = engine.GetUpscaler().getSettings();
  const auto originalUploads = game.renderStats().originalImageUploads;
  for (const float factor : {2.0f, 1.5f}) {
    auto settings = nativeSettings;
    settings.backend = shaders ? UpscaleBackend::MPV : UpscaleBackend::Linear;
    settings.preset = UpscalePreset::Anime4KModeCFast;
    settings.factor = factor;
    engine.GetUpscaler().configure(settings);
    game.debugJumpToNode("8005:1437");
    while (game.debugNode() != "8005:1505")
      step();
    start();
    auto actual = capture(engine, game, "/tmp/reallive-8005-walk-upscaled.bmp");
    SurfacePtr expected(SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32));
    check(bool(expected), "Create native REALLIVE script scene reference");
    SDL_FillSurfaceRect(expected.get(), nullptr,
                        SDL_MapSurfaceRGBA(expected.get(), 255, 255, 255, 255));
    for (const auto &[name, position] :
         std::array<std::pair<const char *, SDL_Point>, 2>{
             {{"OP_D00", {220, 0}}, {"OP_D01", {400, 240}}}}) {
      auto graphic = RealLive::decodeG00(assets->read(assets->graphic(name)));
      const auto &region = graphic.regions.front();
      auto source = assets->loadSurface(assets->graphic(name));
      SDL_Rect crop{region.atlasX, region.atlasY, region.x2 - region.x1 + 1,
                    region.y2 - region.y1 + 1};
      SurfacePtr pattern(
          SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
      check(bool(pattern), "Create isolated walking pattern");
      BlitSurfaceOpacity(*source, *pattern, &crop, nullptr, 255, false);
      SDL_Rect destination{position.x - region.originX,
                           position.y - region.originY, 0, 0};
      BlitSurfaceOpacity(*pattern, *expected, nullptr, &destination, 255, true);
    }
    expected = engine.GetUpscaler().scaled(*expected, factor);
    auto &renderer = engine.GetRenderer();
    TexturePtr texture(SDL_CreateTextureFromSurface(&renderer, expected.get()));
    TexturePtr target(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 960, 720));
    check(bool(texture) && bool(target),
          "Create REALLIVE upscaling reference textures");
    check(SDL_SetRenderTarget(&renderer, target.get()) &&
              SDL_RenderTexture(&renderer, texture.get(), nullptr, nullptr),
          "Render REALLIVE reference without canvas upscaling");
    SurfacePtr reference(SDL_RenderReadPixels(&renderer, nullptr));
    SDL_SetRenderTarget(&renderer, nullptr);
    check(bool(reference) && same(*actual, *reference),
          "REALLIVE script animation must filter the completed native scene "
          "once");
    const auto prepared = game.renderStats();
    check(prepared.filteredPatterns == 0 && prepared.filteredSceneFrames > 0,
          "Script scenes must not double-filter animation textures");
    TexturePtr cacheTarget(SDL_CreateTexture(
        &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 960, 720));
    check(bool(cacheTarget) &&
              SDL_SetRenderTarget(&renderer, cacheTarget.get()),
          "Create texture cache regression target");
    for (int n = 0; n < 8; ++n)
      game.render(renderer, 1);
    check(game.renderStats().filteredSceneFrames ==
              prepared.filteredSceneFrames,
          "Unchanged script scenes must reuse the global filtered frame");
    SDL_Delay(180);
    game.update();
    game.render(renderer, 1);
    check(SDL_SetRenderTarget(&renderer, nullptr),
          "Restore cache regression target");
    const auto reused = game.renderStats();
    check(reused.filteredPatterns == prepared.filteredPatterns &&
              reused.filteredDCFrames == prepared.filteredDCFrames &&
              reused.textureUploads == prepared.textureUploads &&
              reused.filteredSceneFrames == prepared.filteredSceneFrames + 1 &&
              reused.softwareEffectFrames == prepared.softwareEffectFrames,
          "Only a new script animation frame must trigger global filtering");
  }
  if (performance) {
    auto settings = nativeSettings;
    settings.backend = UpscaleBackend::Linear;
    auto &renderer = engine.GetRenderer();
    TexturePtr target(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 960, 720));
    check(bool(target), "Create REALLIVE texture performance target");
    for (float factor : {1.0f, 2.0f, 4.0f}) {
      settings.factor = factor;
      engine.GetUpscaler().configure(settings);
      check(SDL_SetRenderTarget(&renderer, target.get()),
            "Select performance target");
      game.render(renderer, 1);
      SurfacePtr warm(SDL_RenderReadPixels(&renderer, nullptr));
      check(bool(warm), "Finish texture preparation before measurement");
      const auto prepared = game.renderStats();
      const auto began = SDL_GetPerformanceCounter();
      constexpr int count = 120;
      for (int n = 0; n < count; ++n) {
        game.update();
        game.render(renderer, 1);
      }
      SurfacePtr finished(SDL_RenderReadPixels(&renderer, nullptr));
      check(bool(finished), "Complete measured GPU commands");
      const double ms = 1000.0 * (SDL_GetPerformanceCounter() - began) /
                        SDL_GetPerformanceFrequency() / count;
      const auto reused = game.renderStats();
      check(reused.filteredPatterns == prepared.filteredPatterns &&
                reused.filteredDCFrames == prepared.filteredDCFrames &&
                reused.textureUploads == prepared.textureUploads &&
                reused.filteredSceneFrames - prepared.filteredSceneFrames ==
                    (factor > 1 ? reused.gpuFrames - prepared.gpuFrames : 0),
            "Each changed script frame filters once without sprite uploads");
      std::cout << "REALLIVE resident textures " << factor << "x: " << ms
                << " ms/rendered frame\n";
      check(SDL_SetRenderTarget(&renderer, nullptr),
            "Restore performance target");
    }
  }
  game.debugJumpToNode("8005:1437");
  while (game.debugNode() != "8005:1505")
    step();
  start();
  engine.GetUpscaler().configure(nativeSettings);
  auto native =
      capture(engine, game, "/tmp/reallive-8005-walk-native-again.bmp");
  check(same(*first, *native),
        "Disabling REALLIVE upscaling must restore the original native pixels");
  check(game.renderStats().originalImageUploads == originalUploads,
        "Disabling or changing the filter must retain the original textures");
  std::cout
      << "REALLIVE 8005 walking loop, pause and save restoration passed\n";
}
void verify8005BackgroundUpscaling(
    std::shared_ptr<RealLive::RealLiveAssets> assets, bool shaders) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  if (shaders)
    options.rendererOverride = "opengl";
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    ~Guard() { game.shutdown(); }
  } guard{game};
  const auto command = [&](int index) {
    game.debugJumpToNode("8005:" + std::to_string(index));
    check(game.debugStep(), "Execute original REALLIVE background command");
    check(game.diagnostic().empty(), game.diagnostic().c_str());
  };
  command(138);
  command(140);
  command(312);
  text(game)->hide();
  SDL_Delay(1000);
  const auto nativeSettings = engine.GetUpscaler().getSettings();
  auto native =
      capture(engine, game, "/tmp/reallive-8005-background-native.bmp");
  auto settings = nativeSettings;
  settings.backend = shaders ? UpscaleBackend::MPV : UpscaleBackend::Linear;
  settings.preset = UpscalePreset::Anime4KModeCFast;
  settings.factor = 2;
  engine.GetUpscaler().configure(settings);
  const auto before = game.renderStats();
  auto actual =
      capture(engine, game, "/tmp/reallive-8005-background-filtered.bmp");
  check(game.renderStats().filteredSceneFrames ==
                before.filteredSceneFrames + 1 &&
            game.renderStats().filteredPatterns == before.filteredPatterns,
        "Script raster must be filtered once as a complete frame, not per "
        "source crop");

  SurfacePtr nativeComposition(
      SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32));
  check(bool(nativeComposition) &&
            SDL_FillSurfaceRect(
                nativeComposition.get(), nullptr,
                SDL_MapSurfaceRGBA(nativeComposition.get(), 0, 0, 0, 255)),
        "Create independent native script raster");
  for (const auto &[name, masked] :
       std::array<std::pair<const char *, bool>, 2>{
           {{"OP_A01", false}, {"OP_A03", true}}}) {
    auto image = assets->loadSurface(assets->graphic(name));
    SDL_Rect crop{0, 0, masked ? image->w : 640, masked ? image->h : 164};
    SurfacePtr pixels(
        SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
    check(bool(pixels), "Crop original background before filtering");
    BlitSurfaceOpacity(*image, *pixels, &crop, nullptr, 255, false);
    SDL_Rect destination{0, masked ? 0 : 80, 0, 0};
    BlitSurfaceOpacity(*pixels, *nativeComposition, nullptr, &destination, 255,
                       masked);
  }
  auto expected = engine.GetUpscaler().scaled(*nativeComposition, 2);
  auto &renderer = engine.GetRenderer();
  const auto present = [&](SurfaceRef pixels) {
    TexturePtr texture(SDL_CreateTextureFromSurface(&renderer, &pixels));
    TexturePtr target(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 960, 720));
    check(bool(texture) && bool(target) &&
              SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE) &&
              SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR) &&
              SDL_SetRenderTarget(&renderer, target.get()) &&
              SDL_RenderTexture(&renderer, texture.get(), nullptr, nullptr),
          "Present independent script raster or sprite reference");
    SurfacePtr reference(SDL_RenderReadPixels(&renderer, nullptr));
    check(bool(reference) && SDL_SetRenderTarget(&renderer, nullptr),
          "Read image reference");
    return reference;
  };
  auto reference = present(*expected);
  const auto same = [](SurfaceRef a, SurfaceRef b) {
    if (a.w != b.w || a.h != b.h)
      return false;
    for (int y = 0; y < a.h; ++y)
      if (std::memcmp(static_cast<Uint8 *>(a.pixels) + y * a.pitch,
                      static_cast<Uint8 *>(b.pixels) + y * b.pitch, a.w * 4))
        return false;
    return true;
  };
  check(same(*actual, *reference),
        "The opening script raster must be composed natively before filtering "
        "the complete frame");
  const auto prepared = game.renderStats();
  for (int n = 0; n < 8; ++n)
    capture(engine, game, "/tmp/reallive-8005-background-reused.bmp");
  check(game.renderStats().filteredPatterns == prepared.filteredPatterns &&
            game.renderStats().filteredSceneFrames ==
                prepared.filteredSceneFrames &&
            game.renderStats().filteredDCFrames == prepared.filteredDCFrames &&
            game.renderStats().textureUploads == prepared.textureUploads,
        "Repeated background draws must reuse the filtered script frame");
  engine.GetUpscaler().configure(nativeSettings);
  auto restored =
      capture(engine, game, "/tmp/reallive-8005-background-restored.bmp");
  check(same(*native, *restored),
        "Disabling background upscaling must restore native pixels");
  engine.GetUpscaler().configure(settings);
  game.debugJumpToNode("170:24");
  check(game.debugStep(), "Enter a saveable original dialogue pause");
  text(game)->hide();
  const auto path = std::filesystem::temp_directory_path() /
                    ("reallive-background-" +
                     std::to_string(SDL_GetPerformanceCounter()) + ".sav");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code error;
      std::filesystem::remove(path, error);
    }
  } cleanup{path};
  game.saveGame(path.string());
  const auto saved = VNSave::read(path.string());
  auto truncated = saved;
  truncated.gameData.erase(
      truncated.gameData.rfind('\n', truncated.gameData.size() - 2) + 1);
  truncated.write(path.string());
  bool rejected = false;
  try {
    game.loadGame(path.string());
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  check(rejected, "A truncated background-provenance graph must not load as a "
                  "successful save");
  saved.write(path.string());
  command(150);
  game.loadGame(path.string());
  text(game)->hide();
  auto loaded =
      capture(engine, game, "/tmp/reallive-8005-background-loaded.bmp");
  check(same(*actual, *loaded),
        "Save/load must preserve the native script raster and filtered result");
  const auto beforeStrips = game.renderStats();
  command(4038);
  check(
      game.renderStats().filteredPatterns == beforeStrips.filteredPatterns &&
          game.renderStats().filteredDCFrames == beforeStrips.filteredDCFrames,
      "Loading an offscreen script raster must not filter intermediate crops");
  const auto start = SDL_GetTicks();
  while (game.debugNode() != "8005:4074" && SDL_GetTicks() - start < 10000) {
    const auto filters = game.renderStats().filteredSceneFrames;
    check(game.debugStep(), "Execute the original REALLIVE strip animation");
    check(game.renderStats().filteredSceneFrames == filters,
          "Script commands and transition snapshots must remain native");
    check(game.diagnostic().empty(), game.diagnostic().c_str());
    SDL_Delay(1);
  }
  check(game.debugNode() == "8005:4074",
        "The strip sequence must reach its DC1 presentation");
  check(game.renderStats().filteredSceneFrames ==
            beforeStrips.filteredSceneFrames,
        "Strip construction must not filter intermediate copies or snapshots");
  SDL_Delay(750);
  capture(engine, game, "/tmp/reallive-8005-strips-filtered.bmp");
  check(game.renderStats().filteredSceneFrames ==
                beforeStrips.filteredSceneFrames + 1 &&
            game.renderStats().filteredPatterns ==
                beforeStrips.filteredPatterns,
        "The script strip sequence must filter only its completed displayed "
        "raster, not each copy");
  const auto afterStrips = game.renderStats();
  capture(engine, game, "/tmp/reallive-8005-strips-filtered.bmp");
  check(game.renderStats().filteredDCFrames == afterStrips.filteredDCFrames &&
            game.renderStats().filteredSceneFrames ==
                afterStrips.filteredSceneFrames &&
            game.renderStats().textureUploads == afterStrips.textureUploads,
        "The same script animation frame must reuse its prepared texture");
  command(150);
  auto archive = assets->scripts();
  struct Logo {
    const char *name;
    int object, load, position, hide, x, y;
  };
  for (const auto &entry :
       std::array<Logo, 2>{{{"OP_A04", 4, 112, 134, 118, 245, 180},
                            {"OP_A05", 5, 122, 136, 128, 215, 192}}}) {
    command(entry.load);
    command(entry.position);
    int index = 0, show = -1;
    RLMachine machine;
    for (const auto &element : archive.Scenario(8005)) {
      const auto *c =
          dynamic_cast<const libreallive::CommandElement *>(element.get());
      if (c && c->modtype() == 1 && c->module() == 81 && c->opcode() == 1004 &&
          RealLive::IntegerParameter(machine, *c, 0) == entry.object &&
          RealLive::IntegerParameter(machine, *c, 1) == 1) {
        show = index;
        break;
      }
      ++index;
    }
    check(show >= 0, "Find the logo's original visibility command");
    command(show);
    auto logo = capture(engine, game, "/tmp/reallive-8005-logo-filtered.bmp");
    auto logoImage = assets->loadSurface(assets->graphic(entry.name));
    auto image = RealLive::decodeG00(assets->read(assets->graphic(entry.name)));
    SDL_Rect crop{0, 0, logoImage->w, logoImage->h};
    SDL_Point origin{0, 0};
    if (!image.regions.empty()) {
      const auto &region = image.regions.front();
      crop = {region.atlasX, region.atlasY, region.x2 - region.x1 + 1,
              region.y2 - region.y1 + 1};
      origin = {region.originX, region.originY};
    }
    SurfacePtr pixels(
        SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
    check(bool(pixels), "Crop the opening logo before filtering");
    BlitSurfaceOpacity(*logoImage, *pixels, &crop, nullptr, 255, false);
    check(SDL_FillSurfaceRect(
              nativeComposition.get(), nullptr,
              SDL_MapSurfaceRGBA(nativeComposition.get(), 0, 0, 0, 255)),
          "Clear independent logo composition");
    SDL_Rect destination{entry.x - origin.x, entry.y - origin.y, 0, 0};
    BlitSurfaceOpacity(*pixels, *nativeComposition, nullptr, &destination, 255,
                       true);
    expected = engine.GetUpscaler().scaled(*nativeComposition, 2);
    reference = present(*expected);
    check(same(*logo, *reference), "The opening logo must filter its complete "
                                   "script scene at the original position");
    command(entry.hide);
  }
  std::cout
      << "REALLIVE script-frame upscaling, static logo textures and raster "
         "cache passed\n";
}
void verifyScriptTextUpscaling(
    std::shared_ptr<RealLive::RealLiveAssets> assets) {
  RealLive::RealLiveRuntime game(assets);
  game.SetState(GameState::Menu);
  EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "opengl";
  Engine engine(game, options);
  struct Guard {
    RealLive::RealLiveRuntime &game;
    std::filesystem::path path;
    ~Guard() {
      game.shutdown();
      std::error_code error;
      std::filesystem::remove(path, error);
    }
  } guard{game, std::filesystem::temp_directory_path() /
                    ("reallive-script-text-" +
                     std::to_string(SDL_GetPerformanceCounter()) + ".sav")};
  game.debugJumpToNode("170:24");
  check(game.debugStep(), "Enter a saveable pause for script text fixture");
  game.saveGame(guard.path.string());
  auto save = VNSave::read(guard.path.string());
  std::istringstream input(save.gameData);
  std::string version, snapshot, dialogue, speaker, music;
  int pauseKind, elapsed, duration;
  bool loop, stops;
  int64_t end, loopStart;
  double offset, gain, from, to;
  check(bool(input >> version >> std::quoted(snapshot) >>
             std::quoted(dialogue) >> std::quoted(speaker) >> pauseKind >>
             std::quoted(music) >> loop >> end >> loopStart >> offset >> gain >>
             from >> to >> elapsed >> duration >> stops),
        "Read native presentation fixture header");
  const auto begin = size_t(input.tellg());
  size_t count;
  check(bool(input >> count) && count == 0,
        "Script text fixture starts without foreground objects");
  const auto tail = size_t(input.tellg());
  std::ostringstream object;
  object << "\n1\n7 \"\" \"Rasterized text must stay native\""
         << " 40 80 0 173 1 0 0 255 255 255 255 1 30 70 500 60 0 0";
  for (int n = 0; n < 16; ++n)
    object << " 0 0";
  object << '\n';
  save.gameData.replace(begin, tail - begin, object.str());
  save.write(guard.path.string());
  game.loadGame(guard.path.string());
  text(game)->hide();
  auto native = capture(engine, game, "/tmp/reallive-script-text-native.bmp");
  auto settings = engine.GetUpscaler().getSettings();
  settings.backend = UpscaleBackend::MPV;
  settings.preset = UpscalePreset::Anime4KModeCFast;
  settings.factor = 2;
  engine.GetUpscaler().configure(settings);
  auto filtered =
      capture(engine, game, "/tmp/reallive-script-text-filtered.bmp");
  bool visible = false;
  for (int y = 0; y < native->h; ++y) {
    const auto *a = static_cast<Uint8 *>(native->pixels) + y * native->pitch;
    const auto *b =
        static_cast<Uint8 *>(filtered->pixels) + y * filtered->pitch;
    check(std::memcmp(a, b, native->w * 4) == 0,
          "Rasterized script text must remain pixel-identical with Anime4K");
    for (int x = 0; x < native->w; ++x)
      visible = visible || a[x * 4] || a[x * 4 + 1] || a[x * 4 + 2];
  }
  check(visible, "Script text exclusion test must draw visible glyphs");
  const auto prepared = game.renderStats();
  capture(engine, game, "/tmp/reallive-script-text-filtered.bmp");
  check(game.renderStats().filteredSceneFrames ==
                prepared.filteredSceneFrames &&
            game.renderStats().textureUploads == prepared.textureUploads,
        "Text overlays must reuse glyphs without refiltering the script scene");
  std::cout << "Rasterized script text bypasses global Anime4K filtering\n";
}
void verifyBootUpscaling(std::shared_ptr<RealLive::RealLiveAssets> assets) {
  for (bool key : {false, true}) {
    RealLive::RealLiveRuntime game(assets);
    game.SetState(GameState::Menu);
    EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    options.rendererOverride = "opengl";
    options.settings.upscaling.backend = UpscaleBackend::MPV;
    options.settings.upscaling.preset = UpscalePreset::Anime4KModeCFast;
    options.settings.upscaling.factor = 2;
    Engine engine(game, options);
    struct Guard {
      RealLive::RealLiveRuntime &game;
      ~Guard() { game.shutdown(); }
    } guard{game};
    const auto command = [&](int index) {
      game.debugJumpToNode("9031:" + std::to_string(index));
      check(game.debugStep(), "Execute original boot logo command");
      check(game.diagnostic().empty(), game.diagnostic().c_str());
    };
    if (key) {
      for (int index : {83, 85, 87, 89, 91})
        command(index);
    } else {
      for (int index : {30, 32, 34, 36})
        command(index);
    }
    text(game)->hide();
    const auto prepared = game.renderStats();
    check(prepared.filteredPatterns == 0,
          "Script boot logos must not be filtered separately from their scene");
    auto actual = capture(engine, game, "/tmp/reallive-boot-logo-upscaled.bmp");
    const auto ready = game.renderStats();
    auto source =
        assets->loadSurface(assets->graphic(key ? "KEYLOGO0" : "S_TT_BG00"));
    auto image = RealLive::decodeG00(
        assets->read(assets->graphic(key ? "KEYLOGO0" : "S_TT_BG00")));
    SDL_Rect crop{0, 0, source->w, source->h};
    SDL_Point origin{};
    if (!image.regions.empty()) {
      const auto &region = image.regions.front();
      crop = {region.atlasX, region.atlasY, region.x2 - region.x1 + 1,
              region.y2 - region.y1 + 1};
      origin = {region.originX, region.originY};
    }
    SurfacePtr pixels(
        SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
    check(bool(pixels), "Crop boot logo source");
    BlitSurfaceOpacity(*source, *pixels, &crop, nullptr, 255, false);
    SDL_Rect destination{(key ? 240 : 0) - origin.x,
                         (key ? -190 : 0) - origin.y, 0, 0};
    auto &renderer = engine.GetRenderer();
    TexturePtr nativeLogo(
        SDL_CreateTextureFromSurface(&renderer, pixels.get()));
    TexturePtr nativeScene(SDL_CreateTexture(
        &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 640, 480));
    check(bool(nativeLogo) && bool(nativeScene) &&
              SDL_SetRenderTarget(&renderer, nativeScene.get()) &&
              SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(&renderer),
          "Compose independent native boot scene on GPU");
    const SDL_Rect clip{240, 170, 160, 120};
    const SDL_FRect area{float(destination.x), float(destination.y),
                         float(pixels->w), float(pixels->h)};
    check(SDL_SetRenderClipRect(&renderer, key ? &clip : nullptr) &&
              SDL_RenderTexture(&renderer, nativeLogo.get(), nullptr, &area) &&
              SDL_SetRenderClipRect(&renderer, nullptr) &&
              SDL_SetRenderTarget(&renderer, nullptr),
          "Compose original logo with native clipping before filtering");
    auto texture = engine.GetUpscaler().scaled(*nativeScene, 2);
    TexturePtr target(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_TARGET, 960, 720));
    check(bool(texture) && bool(target) &&
              SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_NONE) &&
              SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR) &&
              SDL_SetRenderTarget(&renderer, target.get()) &&
              SDL_RenderTexture(&renderer, texture.get(), nullptr, nullptr),
          "Present independently filtered boot logo");
    SurfacePtr reference(SDL_RenderReadPixels(&renderer, nullptr));
    check(bool(reference) && SDL_SetRenderTarget(&renderer, nullptr),
          "Read boot logo reference");
    const auto same = [](SurfaceRef a, SurfaceRef b, int tolerance = 0) {
      if (a.w != b.w || a.h != b.h)
        return false;
      for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w * 4; ++x)
          if (std::abs(int(static_cast<Uint8 *>(a.pixels)[y * a.pitch + x]) -
                       int(static_cast<Uint8 *>(b.pixels)[y * b.pitch + x])) >
              tolerance)
            return false;
      return true;
    };
    check(same(*actual, *reference),
          "Script boot logo must filter its complete native scene");
    for (int n = 0; n < 8; ++n)
      capture(engine, game, "/tmp/reallive-boot-logo-upscaled.bmp");
    check(game.renderStats().filteredPatterns == ready.filteredPatterns &&
              game.renderStats().filteredSceneFrames ==
                  ready.filteredSceneFrames &&
              game.renderStats().filteredDCFrames == ready.filteredDCFrames &&
              game.renderStats().textureUploads == ready.textureUploads,
          "Boot logos must reuse filtered textures without frame filtering");
    auto settings = engine.GetUpscaler().getSettings();
    settings.factor = 1;
    engine.GetUpscaler().configure(settings);
    auto native = capture(engine, game, "/tmp/reallive-boot-logo-native.bmp");
    check(!same(*actual, *native),
          "Boot logo shader filtering must visibly differ from native scaling");
    if (key) {
      settings.factor = 2;
      engine.GetUpscaler().configure(settings);
      game.debugJumpToNode("9030");
      SDL_SetRenderVSync(&engine.GetRenderer(), 0);
      const auto began = SDL_GetTicks();
      const auto atTitle = [&] {
        const auto node = game.debugNode();
        if (node.find("9032:") != 0)
          return false;
        const auto index = std::stoull(node.substr(5));
        return index >= 860 && index < 900;
      };
      while (!atTitle() && SDL_GetTicks() - began < 45000) {
        engine.Iterate();
        check(game.diagnostic().empty(), game.diagnostic().c_str());
        SDL_Delay(1);
      }
      check(atTitle(),
            ("The original upscaled boot must reach the title's input loop, "
             "stopped at " +
             game.debugNode())
                .c_str());
      auto title =
          capture(engine, game, "/tmp/reallive-boot-title-upscaled.bmp");
      const auto titleReady = game.renderStats();
      for (int n = 0; n < 8; ++n)
        capture(engine, game, "/tmp/reallive-boot-title-upscaled.bmp");
      check(game.renderStats().filteredPatterns ==
                    titleReady.filteredPatterns &&
                game.renderStats().filteredSceneFrames ==
                    titleReady.filteredSceneFrames &&
                game.renderStats().filteredDCFrames ==
                    titleReady.filteredDCFrames &&
                game.renderStats().textureUploads == titleReady.textureUploads,
            "The title's REALLIVE logo must remain filtered without repeated "
            "work");
      settings.factor = 1;
      engine.GetUpscaler().configure(settings);
      auto originalTitle =
          capture(engine, game, "/tmp/reallive-boot-title-native.bmp");
      check(!same(*title, *originalTitle),
            "The menu's REALLIVE logo and title images must retain shader "
            "filtering");
    }
  }
  std::cout
      << "REALLIVE and KEY script scenes use cached global Anime4K filtering\n";
}
int main(int argc, char **argv) {
  try {
    for (int style : {4, 5}) {
      int revealed = 0;
      std::array<bool, 64> pattern{};
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
          pattern[y * 8 + x] =
              RealLive::transitionRevealsNewPixel(style, 0.5, x, y);
          revealed += pattern[y * 8 + x];
          check(!RealLive::transitionRevealsNewPixel(style, 0, x, y),
                "REALLIVE dither transition must start on the old frame");
          check(RealLive::transitionRevealsNewPixel(style, 1, x, y),
                "REALLIVE dither transition must end on the new frame");
        }
      check(revealed == 32,
            "REALLIVE dither transition must reveal half the tile");
      if (style == 4) {
        std::array<bool, 64> diagonal{};
        for (int y = 0; y < 8; ++y)
          for (int x = 0; x < 8; ++x)
            diagonal[y * 8 + x] =
                RealLive::transitionRevealsNewPixel(5, 0.5, x, y);
        check(pattern != diagonal,
              "REALLIVE diagonal dither must differ from orthogonal dither");
      }
    }
    if (argc < 2 || !argv[1][0])
      return 77;
    auto assets = std::make_shared<RealLive::RealLiveAssets>(argv[1]);
    if (argc > 4 && std::string(argv[2]) == "--inspect-scene") {
      RLMachine machine;
      auto archive = assets->scripts();
      const auto &scenario = archive.Scenario(std::stoi(argv[3]));
      size_t index = 0;
      const size_t first = std::stoull(argv[4]);
      const size_t last = argc > 5 ? std::stoull(argv[5]) : first + 30;
      for (const auto &element : scenario) {
        if (index >= first && index <= last) {
          std::cout << index << ' ';
          element->PrintSourceRepresentation(&machine, std::cout);
          if (const auto *command =
                  dynamic_cast<const libreallive::CommandElement *>(
                      element.get()))
            std::cout << " [" << command->modtype() << ':' << command->module()
                      << ':' << command->opcode() << '/' << command->overload()
                      << "]";
          std::cout << '\n';
        }
        ++index;
      }
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--cursor-check") {
      RealLive::RealLiveRuntime game(assets);
      game.SetState(GameState::Menu);
      EngineOptions options;
      options.assets = assets;
      options.persistSettings = false;
      Engine engine(game, options);
      const int selected = assets->settings().integer("MOUSE_CURSOR", -1);
      std::ostringstream key;
      key << "MOUSE_CURSOR." << std::setw(3) << std::setfill('0') << selected
          << ".NAME";
      const auto name = assets->settings().text(key.str());
      if (selected >= 0 && !name.empty() && !assets->graphic(name).empty()) {
        check(SDL_GetCursor() != SDL_GetDefaultCursor(),
              "Configured RealLive mouse cursor was not installed");
        auto atlas = assets->loadSurface(assets->graphic(name));
        auto frame = RealLive::mouseCursorFrame(atlas.get());
        check(frame.surface && frame.surface->w == 32 && frame.surface->h == 32,
              "RealLive mouse cursor must exclude atlas border and mask");
        Uint8 r, g, b, a;
        check(SDL_ReadSurfacePixel(frame.surface.get(), 0, 0, &r, &g, &b, &a) &&
                  a == 0 &&
                  SDL_ReadSurfacePixel(frame.surface.get(), 20, 15, &r, &g, &b,
                                       &a) &&
                  a == 255,
              "RealLive cursor mask must hide the border but keep the image");
      }
      game.shutdown();
      check(SDL_GetCursor() == SDL_GetDefaultCursor(),
            "RealLive cursor was not restored on shutdown");
      std::cout << "REALLIVE cursor "
                << (name.empty() ? "system default" : name) << " checked\n";
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--boot-probe") {
      RealLive::RealLiveRuntime game(assets);
      game.SetState(GameState::Menu);
      EngineOptions options;
      options.assets = assets;
      options.persistSettings = false;
      Engine engine(game, options);
      struct Guard {
        RealLive::RealLiveRuntime &game;
        ~Guard() { game.shutdown(); }
      } guard{game};
      SDL_SetRenderVSync(&engine.GetRenderer(), 0);
      const auto start = SDL_GetTicks();
      std::string previous;
      bool clicked = false;
      while (SDL_GetTicks() - start < (argc > 4 ? 60000 : 30000)) {
        if (argc > 4 && !clicked && SDL_GetTicks() - start > 27000) {
          SDL_Event event{};
          event.type = SDL_EVENT_MOUSE_MOTION;
          event.motion.x = std::stof(argv[3]);
          event.motion.y = std::stof(argv[4]);
          game.onEvent(event);
          event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
          event.button.button = SDL_BUTTON_LEFT;
          event.button.x = std::stof(argv[3]);
          event.button.y = std::stof(argv[4]);
          game.onEvent(event);
          engine.Iterate();
          event.type = SDL_EVENT_MOUSE_BUTTON_UP;
          game.onEvent(event);
          clicked = true;
        }
        engine.Iterate();
        check(game.diagnostic().empty(), game.diagnostic().c_str());
        if (game.debugNode() != previous) {
          previous = game.debugNode();
          std::cout << SDL_GetTicks() - start << " " << previous << '\n';
        }
        SDL_Delay(16);
      }
      capture(engine, game, "build/reallive-boot-probe.bmp");
      std::cout << "Boot probe completed at " << game.debugNode() << '\n';
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--story-probe") {
      RealLive::RealLiveRuntime game(assets);
      game.SetState(GameState::Menu);
      EngineOptions options;
      options.assets = assets;
      options.persistSettings = false;
      Engine engine(game, options);
      struct Guard {
        RealLive::RealLiveRuntime &game;
        ~Guard() { game.shutdown(); }
      } guard{game};
      SDL_SetRenderVSync(&engine.GetRenderer(), 0);
      const int seconds = argc > 3 ? std::stoi(argv[3]) : 45;
      if (argc > 4)
        game.debugJumpToNode(argv[4]);
      const auto start = SDL_GetTicks();
      std::string previous;
      int transitions = 0;
      int presses = 0;
      bool failed = false;
      auto press = [&](bool down) {
        SDL_Event event{};
        event.type =
            down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.key = SDLK_RETURN;
        event.key.scancode = SDL_SCANCODE_RETURN;
        game.onEvent(event);
      };
      while (SDL_GetTicks() - start < Uint64(seconds) * 1000) {
        if (SDL_GetTicks() - start > 1000 && (transitions + presses) % 2 == 0) {
          press(true);
          engine.Iterate();
          press(false);
          ++presses;
        }
        engine.Iterate();
        if (!game.diagnostic().empty()) {
          std::cout << "STORY ERROR " << game.debugNode() << ": "
                    << game.diagnostic() << std::endl;
          failed = true;
          break;
        }
        if (game.debugNode() != previous) {
          previous = game.debugNode();
          ++transitions;
          std::cout << SDL_GetTicks() - start << " " << previous << '\n';
        }
        SDL_Delay(16);
      }
      capture(engine, game, "build/reallive-story-probe.bmp");
      std::cout << "Story probe " << (failed ? "FAILED" : "completed")
                << " at " << game.debugNode() << " after " << transitions
                << " nodes and " << presses << " advances\n";
      return failed ? 1 : 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--direct-iso") {
      check(isISOImage(assets->directory()) &&
                std::filesystem::equivalent(assets->directory(), argv[1]) &&
                assets->localPath("Seen.txt").empty() &&
                assets->localPath("Gameexe.ini").empty(),
            "REALLIVE ISO assets must retain the image, never extracted local "
            "files");
      auto archive = assets->scripts();
      check(!archive.ScenarioNumbers().empty() &&
                archive.Scenario(8005).begin() != archive.Scenario(8005).end(),
            "Read original REALLIVE scenarios directly into memory");
      const auto voices = assets->list("KOE/");
      RealLive::VoiceIndex index(voices);
      check(index.size() == 24900,
            "Index every original voice through ISO paths");
      const auto voice = index.find(std::stoi(
          std::filesystem::path(voices.front()).stem().string().substr(1)));
      check(bool(voice) && assets->localPath(voice->generic_string()).empty(),
            "Voices must retain logical ISO identifiers");
      const auto audio = assets->loadAudio(voice->generic_string());
      check(audio.spec.freq > 0 && !audio.data.empty(),
            "Decode an original voice directly from ISO bytes");
      const auto picture = assets->loadSurface(assets->graphic("OP_A05"));
      check(picture && picture->w > 0,
            "Decode the opening logo directly from ISO bytes");
      const auto overlay = std::filesystem::temp_directory_path() /
                           ("reallive-direct-overlay-" +
                            std::to_string(SDL_GetPerformanceCounter()));
      std::filesystem::create_directory(overlay);
      struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
          std::error_code error;
          std::filesystem::remove_all(path, error);
        }
      } cleanup{overlay};
      const auto seen = assets->read("Seen.txt");
      const auto read32 = [&](size_t at) {
        return Uint32(seen.at(at)) | Uint32(seen.at(at + 1)) << 8 |
               Uint32(seen.at(at + 2)) << 16 | Uint32(seen.at(at + 3)) << 24;
      };
      const auto offset = read32(9032 * 8), size = read32(9032 * 8 + 4);
      check(offset <= seen.size() && size <= seen.size() - offset,
            "Locate the original scenario override fixture");
      {
        std::ofstream output(overlay / "Seen9032.txt", std::ios::binary);
        output.write(reinterpret_cast<const char *>(seen.data() + offset),
                     size);
        check(bool(output), "Write a single-script overlay fixture");
      }
      assets->setOverlay(overlay);
      auto patched = assets->scripts();
      check(std::distance(patched.Scenario(9032).begin(),
                          patched.Scenario(9032).end()) ==
                    std::distance(archive.Scenario(9032).begin(),
                                  archive.Scenario(9032).end()) &&
                !assets->localPath("Seen9032.txt").empty() &&
                assets->localPath("Seen.txt").empty(),
            "Loose-script overlays must override a direct ISO archive without "
            "extraction");
      {
        std::ofstream output(overlay / "Seen.txt", std::ios::binary);
        const std::string emptyTOC(80000, '\0');
        output.write(emptyTOC.data(), emptyTOC.size());
        check(bool(output),
              "Write an empty archive overlay to isolate loose-script lookup");
      }
      assets->setOverlay(overlay);
      auto isolated = assets->scripts();
      check(isolated.ScenarioNumbers() == std::vector<int>{9032} &&
                isolated.Scenario(9032).begin() !=
                    isolated.Scenario(9032).end(),
            "The loose-script overlay must supply the scenario missing from "
            "the archive");
      assets->setOverlay({});
      auto original = assets->scripts();
      check(original.ScenarioNumbers().size() ==
                    archive.ScenarioNumbers().size() &&
                assets->localPath("Seen.txt").empty(),
            "Disabling the patch must return to the original ISO without "
            "hidden source overlays");
      std::cout
          << "REALLIVE direct ISO scripts, voice, image and overlay passed\n";
      return 0;
    }
    if (argc > 2 &&
        (std::string(argv[2]) == "--8005-background-upscaling" ||
         std::string(argv[2]) == "--8005-background-upscaling-opengl")) {
      auto archive = assets->scripts();
      const auto elements8005 = scenario8005ElementCount(archive);
      if (elements8005 < kMin8005ScenarioElements) {
        std::cout
            << "REALLIVE 8005 background upscaling skipped: scenario 8005 "
               "has "
            << elements8005 << " elements, original regression needs >= "
            << kMin8005ScenarioElements << "\n";
        return 77;
      }
      verify8005BackgroundUpscaling(std::move(assets),
                                    std::string(argv[2]) ==
                                        "--8005-background-upscaling-opengl");
      return 0;
    }
    if (argc > 2 && (std::string(argv[2]) == "--texture-performance" ||
                     std::string(argv[2]) == "--8005-walk" ||
                     std::string(argv[2]) == "--8005-walk-opengl")) {
      auto archive = assets->scripts();
      const auto elements8005 = scenario8005ElementCount(archive);
      if (elements8005 < kMin8005ScenarioElements) {
        std::cout << "REALLIVE 8005 walking skipped: scenario 8005 has "
                  << elements8005 << " elements, original regression needs >= "
                  << kMin8005ScenarioElements << "\n";
        return 77;
      }
      verify8005Walk(std::move(assets),
                     std::string(argv[2]) == "--8005-walk-opengl",
                     std::string(argv[2]) == "--texture-performance");
      return 0;
    }
    if (argc > 2 && (std::string(argv[2]) == "--8005-capture" ||
                     std::string(argv[2]) == "--8005-capture-upscaled")) {
      auto archive = assets->scripts();
      const auto elements8005 = scenario8005ElementCount(archive);
      if (elements8005 < kMin8005ScenarioElements) {
        std::cout << "REALLIVE 8005 capture skipped: scenario 8005 has "
                  << elements8005 << " elements, original regression needs >= "
                  << kMin8005ScenarioElements << "\n";
        return 77;
      }
      RealLive::RealLiveRuntime intro(assets);
      intro.SetState(GameState::Menu);
      EngineOptions options;
      options.assets = assets;
      options.persistSettings = false;
      Engine engine(intro, options);
      struct Guard {
        RealLive::RealLiveRuntime &game;
        ~Guard() { game.shutdown(); }
      } guard{intro};
      if (std::string(argv[2]) == "--8005-capture-upscaled") {
        auto settings = engine.GetUpscaler().getSettings();
        settings.backend = UpscaleBackend::Linear;
        settings.factor = 2;
        engine.GetUpscaler().configure(settings);
      }
      SDL_SetRenderVSync(&engine.GetRenderer(), 0);
      engine.Iterate();
      intro.debugJumpToNode(find8005Entry(assets->scripts()));
      const auto entryStart = SDL_GetTicks();
      while (intro.debugNode().find("8005:") != 0 &&
             SDL_GetTicks() - entryStart < 10000) {
        engine.Iterate();
        check(intro.diagnostic().empty(), intro.diagnostic().c_str());
        SDL_Delay(1);
      }
      check(intro.debugNode().find("8005:") == 0,
            "REALLIVE 8005 capture must enter through its original caller");
      const auto captureAt = [&](std::size_t target, const char *path) {
        while (intro.debugNode().find("8005:") == 0 &&
               std::stoull(intro.debugNode().substr(
                   intro.debugNode().find(':') + 1)) < target) {
          check(intro.debugStep(),
                ("REALLIVE VM stopped before 8005:" + std::to_string(target))
                    .c_str());
          check(intro.diagnostic().empty(), intro.diagnostic().c_str());
        }
        check(intro.debugNode() == "8005:" + std::to_string(target),
              ("REALLIVE capture did not reach 8005:" + std::to_string(target))
                  .c_str());
        SDL_Delay(450);
        auto frame = capture(engine, intro, path);
        Uint8 r, g, b, a;
        check(SDL_ReadSurfacePixel(frame.get(), 50, 570, &r, &g, &b, &a),
              "REALLIVE 8005 capture pixel could not be read");
        check(r + g + b > 450, "REALLIVE 8005 transition must restore the full "
                               "scene, not a strip");
      };
      captureAt(344, "/tmp/reallive-8005-344.bmp");
      captureAt(490, "/tmp/reallive-8005-490.bmp");
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--script-text-upscaling") {
      verifyScriptTextUpscaling(std::move(assets));
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--boot-upscaling") {
      verifyBootUpscaling(std::move(assets));
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--title-compat") {
      verifyCompatibilityNode(std::move(assets), "9032:9");
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--8005-compat") {
      verify8005Call(std::move(assets));
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--8005-all-calls") {
      verify8005Commands(std::move(assets));
      return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "--check-all-opcodes") {
      verifyAllOpcodes(std::move(assets));
      return 0;
    }
    // SEEN8613 loads MSG_01 without blending, then reveals rows with maskCopy.
    // The intermediate copy must keep the mask, including zero-alpha RGB.
    {
      auto image = assets->loadSurface(assets->graphic("MSG_01"));
      SurfacePtr buffer(
          SDL_CreateSurface(image->w, image->h, SDL_PIXELFORMAT_RGBA32));
      SurfacePtr row(SDL_CreateSurface(500, 30, SDL_PIXELFORMAT_RGBA32));
      BlitSurfaceOpacity(*image, *buffer, nullptr, nullptr, 255, false);
      for (int opacity : {0, 128, 255}) {
        SDL_FillSurfaceRect(row.get(), nullptr,
                            SDL_MapSurfaceRGBA(row.get(), 255, 255, 255, 255));
        SDL_Rect source{70, 0, 500, 30};
        BlitSurfaceOpacity(*buffer, *row, &source, nullptr, opacity, true);
        int white = 0, ink = 0;
        for (int y = 0; y < 30; ++y)
          for (int x = 0; x < 500; ++x) {
            Uint8 r, g, b, a;
            SDL_ReadSurfacePixel(row.get(), x, y, &r, &g, &b, &a);
            white += r > 245 && g > 245 && b > 245;
            ink += r < 200 && g < 200 && b < 200;
          }
        check(white > 10000, "REALLIVE intro mask must preserve the white "
                             "background throughout the fade");
        if (opacity == 255)
          check(ink > 20, "REALLIVE intro row must contain readable text");
      }
    }
    RealLive::RealLiveRuntime game(assets);
    auto sources = game.translationSources();
    std::size_t translationCount = 0;
    for (const auto &[route, texts] : sources)
      translationCount += texts.size();
    check(sources.size() == 96 && translationCount > 40000,
          "REALLIVE translation corpus was not exported");
    check(std::find(sources.at(170).begin(), sources.at(170).end(),
                    "Eu tava deslizando pelo vento...") !=
              sources.at(170).end(),
          "REALLIVE translation route/source changed");
    const auto dictionaryPath = std::filesystem::temp_directory_path() /
                                ("reallive-translation-test-" +
                                 std::to_string(SDL_GetTicksNS()) + ".json");
    Translation exported;
    exported.dump(dictionaryPath.string(), sources);
    Translation loaded;
    loaded.load(dictionaryPath.string());
    std::filesystem::remove(dictionaryPath);
    if (argc > 2 && std::string(argv[2]) == "--corpus-only") {
      std::cout << "Translation routes=" << sources.size()
                << " entries=" << translationCount << '\n';
      return 0;
    }
    game.SetState(GameState::Menu);
    EngineOptions o;
    o.assets = assets;
    o.persistSettings = false;
    const bool gpuTest = argc > 2 && std::string(argv[2]) == "--ui-opengl";
    if (gpuTest) {
      o.rendererOverride = "opengl";
      o.settings.upscaling = {
          UpscaleBackend::MPV, UpscalePreset::Anime4KModeCFast, 2, {}};
    }
    Engine engine(game, o);
    struct Guard {
      RealLive::RealLiveRuntime &g;
      ~Guard() { g.shutdown(); }
    } guard{game};
    SDL_SetRenderVSync(&engine.GetRenderer(), 0);
    if (argc > 2 && std::string(argv[2]) == "--intro-capture") {
      game.debugJumpToNode(argc > 3 ? argv[3] : "8993:97");
      for (int n = 0; n < 450; ++n) {
        engine.Iterate();
        if (n % 30 == 0) {
          const auto name = "/tmp/reallive-intro-" + std::to_string(n) + ".bmp";
          capture(engine, game, name.c_str());
          std::cout << n << " " << game.debugNode() << std::endl;
        }
        SDL_Delay(16);
      }
      return 0;
    }

    // A native refresh presents a complete script frame before executing
    // the next loop iteration, rather than stopping at an arbitrary VM budget.
    game.debugJumpToNode("9032:859");
    game.update();
    check(game.debugNode() == "9032:860",
          "REALLIVE refresh did not yield at the frame boundary");
    game.debugJumpToNode("9037:118");
    game.update();
    SDL_Event typed{};
    typed.type = SDL_EVENT_TEXT_INPUT;
    typed.text.text = "REALLIVE";
    game.onEvent(typed);
    capture(engine, game, "/tmp/reallive-name-fields.bmp");
    SurfacePtr fields(SDL_LoadBMP("/tmp/reallive-name-fields.bmp"));
    check(bool(fields), "Name field capture failed");
    int dark = 0, white = 0;
    for (int y = 201; y < 253; ++y)
      for (int x = 388; x < 592; ++x) {
        Uint8 r, g, b, a;
        SDL_ReadSurfacePixel(fields.get(), x, y, &r, &g, &b, &a);
        dark += r < 80 && g < 80 && b < 80;
        white += r > 240 && g > 240 && b > 240;
      }
    check(dark > 20 && white > 8000,
          "Name fields must render dark text on a white background");
    // Returning to the title must discard every object and edit control
    // belonging to the previous scene before running the title script.
    auto &returnMenu = GetInGameMenuRuntime(engine);
    game.SetState(GameState::Gameplay);
    // REALLIVE registers the shared scenes for gameplay by reusing the menu
    // scenes; mirror that here and keep the menu list intact for the return to
    // title.
    if (game.GetScenes(GameState::Gameplay).empty())
      for (auto *scene : game.GetScenes(GameState::Menu))
        game.AddScene(scene, GameState::Gameplay);
    game.debugJumpToNode("170:246");
    game.update();
    check(engine.GetAudio().channel("BGM")->activeStreams() > 0,
          "Scene music starts before testing return to title");
    // Start opens the pause menu, South cancels it (even with dialogue on
    // screen), and the D-pad plus East reach "Main Menu".
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_START, true));
    check(returnMenu.IsOpen(), "Gamepad Start must open the pause menu");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_START, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(!returnMenu.IsOpen(),
          "Gamepad South must cancel the pause menu while dialogue is visible");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_START, true));
    check(returnMenu.IsOpen(), "Gamepad Start must reopen the pause menu");
    for (int i = 0; i < 5; ++i) {
      game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
      game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    }
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, true));
    check(game.GetState() == GameState::Menu && game.debugNode() == "9032:0",
          "Gamepad East must activate Main Menu and reset the script");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, false));
    check(
        engine.GetAudio().channel("BGM")->activeStreams() == 0,
        "Returning to title stops scene music before the native NAMI ambience");
    capture(engine, game, "/tmp/reallive-clean-title.bmp");
    SurfacePtr clean(SDL_LoadBMP("/tmp/reallive-clean-title.bmp"));
    for (int y = 0; y < clean->h; y += 16)
      for (int x = 0; x < clean->w; x += 16) {
        Uint8 r, g, b, a;
        SDL_ReadSurfacePixel(clean.get(), x, y, &r, &g, &b, &a);
        check(r == 0 && g == 0 && b == 0,
              "Previous scene leaked into the title canvas");
      }
    game.debugJumpToNode("9032:911");
    game.update();
    VNSaveMenu *loadMenu = nullptr;
    for (auto *scene : game.GetActiveScenes())
      if (auto *menu = dynamic_cast<VNSaveMenu *>(scene))
        loadMenu = menu;
    check(game.diagnostic().empty() && loadMenu && loadMenu->isOpen(),
          "REALLIVE menu_load at 9032:911 must open the load menu");
    const auto loadNode = game.debugNode();
    game.update();
    check(game.debugNode() == loadNode,
          "REALLIVE load menu must pause script execution");
    loadMenu->close();
    if (gpuTest || (argc > 2 && std::string(argv[2]) == "--ui-only")) {
      std::cout
          << "REALLIVE name-field contrast and refresh frame boundary passed\n";
      return 0;
    }
    game.debugJumpToNode("9030");
    // Run the original boot path, including its timed logo frames.
    auto start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 30000) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      if (game.debugNode().find("9032:") == 0 && SDL_GetTicks() - start > 15000)
        break;
      SDL_Delay(1);
    }
    check(game.debugNode().find("9032:") == 0,
          "Original startup did not reach title");
    capture(engine, game, "/tmp/reallive-title.bmp");
    const auto titleReady = SDL_GetTicks();
    while (game.debugNode() != "9032:860" &&
           SDL_GetTicks() - titleReady < 15000) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      SDL_Delay(1);
    }
    check(game.debugNode() == "9032:860",
          "Original title input loop must be ready");
    // The title script only polls mouse registers. The runtime maps the D-pad
    // to a virtual cursor over the native hitboxes and turns Enter/East into a
    // press/release pair, so the title is playable without a pointer.
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    // Entry 1 is Load. Activating it with the gamepad proves the mapped East
    // button reaches the same native press/release path.
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, true));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, false));
    VNSaveMenu *titleLoad = nullptr;
    for (int n = 0; n < 600 && !titleLoad; ++n) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      for (auto *scene : game.GetActiveScenes())
        if (auto *menu = dynamic_cast<VNSaveMenu *>(scene))
          if (menu->isOpen())
            titleLoad = menu;
    }
    check(titleLoad,
          "Gamepad East must activate the REALLIVE title Load entry");
    titleLoad->close();
    for (int n = 0; n < 600 && game.debugNode() != "9032:860"; ++n) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
    }
    check(game.debugNode() == "9032:860",
          "REALLIVE title must return to its input loop after closing Load");
    SDL_Event titleUp{};
    titleUp.type = SDL_EVENT_KEY_DOWN;
    titleUp.key.key = SDLK_UP;
    titleUp.key.down = true;
    game.onEvent(titleUp);
    SDL_Event titleConfirm{};
    titleConfirm.type = SDL_EVENT_KEY_DOWN;
    titleConfirm.key.key = SDLK_RETURN;
    titleConfirm.key.down = true;
    game.onEvent(titleConfirm);
    for (int n = 0; n < 600 && game.debugNode() == "9032:860"; ++n) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
    }
    check(
        game.debugNode() != "9032:860",
        "Keyboard Enter must activate the original REALLIVE title button after "
        "the gamepad moved its virtual cursor");
    std::cout << "REALLIVE title keyboard/gamepad activation passed at "
              << game.debugNode() << '\n';

    // Execute the native date/title setup, then skip its long animation so
    // this test exercises the save UI rather than waiting through the intro.
    game.debugJumpToNode("170:0");
    game.update();
    check(std::string(SDL_GetWindowTitle(&engine.GetWindow()))
                  .find("17 de Julho (Seg)") != std::string::npos,
          "The native script must set the save date title");
    game.debugJumpToNode("170:16");
    start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 6000 &&
           (!text(game) || !text(game)->isVisible())) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      SDL_Delay(1);
    }
    auto *t = text(game);
    check(t && t->isVisible(), "Dialogue did not open");
    check(t->getText().find("Eu tava deslizando pelo vento...") !=
              std::string::npos,
          "Original Portuguese text was not preserved");
    auto nativeButton = [&](float x, float y) {
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.which = SDL_TOUCH_MOUSEID;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = x;
      event.button.y = y;
      check(t->handleEvent(event),
            "REALLIVE native control accepts finger-down");
      event.type = SDL_EVENT_MOUSE_BUTTON_UP;
      check(t->handleEvent(event), "REALLIVE native control accepts finger-up");
    };
    nativeButton(622, 364);
    check(GetVNBacklogRuntime(engine).isVisible(),
          "REALLIVE original back button opens the backlog");
    GetVNBacklogRuntime(engine).close();
    auto &backlog = GetVNBacklogRuntime(engine);
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    check(backlog.isVisible(),
          "Gamepad left shoulder must open the REALLIVE history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_BACK, true));
    check(!backlog.isVisible(), "Gamepad Back must close the REALLIVE history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_BACK, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    check(backlog.isVisible(), "Gamepad left shoulder must reopen the history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(!backlog.isVisible(),
          "Gamepad South must close the REALLIVE history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, false));
    nativeButton(600, 352);
    SDL_Event reveal{};
    reveal.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    reveal.button.button = SDL_BUTTON_LEFT;
    reveal.button.x = reveal.button.y = 75;
    const auto hiddenNode = game.debugNode();
    game.onEvent(reveal);
    check(game.debugNode() == hiddenNode,
          "Restoring hidden REALLIVE text must not advance the story");
    nativeButton(622, 402);
    t->completeDialogue();
    const auto node = game.debugNode(), line = t->getText();
    auto path = std::filesystem::temp_directory_path() /
                ("reallive-test-" + std::to_string(SDL_GetTicksNS()) + ".vn");
    const auto namedDirectory = path.string() + "-names";
    VNSaveMenu namedSave(engine);
    game.AddScene(&namedSave);
    namedSave.open(namedDirectory, false, "17 de Julho (Seg)");
    SDL_Event confirmName{};
    confirmName.type = SDL_EVENT_KEY_DOWN;
    confirmName.key.key = SDLK_RETURN;
    namedSave.onEvent(confirmName);
    TextInputDialog *nameDialog = nullptr;
    for (auto *scene : game.GetActiveScenes())
      if (auto *dialog = dynamic_cast<TextInputDialog *>(scene))
        nameDialog = dialog;
    check(nameDialog, "New save must show the editable novel suggestion");
    nameDialog->onEvent(confirmName);
    check(!namedSave.isOpen() && std::filesystem::is_regular_file(
                                     std::filesystem::path(namedDirectory) /
                                     "17 de Julho (Seg).vn"),
          "Accepting the suggestion must create a named REALLIVE save");
    game.RemoveScene(&namedSave);
    std::filesystem::remove_all(namedDirectory);
    game.saveGame(path.string());
    check(VNSave::read(path.string()).gameData.find("17 de Julho (Seg)") !=
              std::string::npos,
          "REALLIVE must save the script-provided date title");
    auto legacy = VNSave::read(path.string());
    const auto titlePosition = legacy.gameData.rfind("\"17 de Julho (Seg)\"");
    check(titlePosition != std::string::npos,
          "REALLIVE date must be serialized");
    legacy.gameData.erase(titlePosition);
    legacy.gameData.replace(0, std::string("REALLIVE-PRESENTATION-5").size(),
                            "REALLIVE-PRESENTATION-4");
    legacy.write(path.string());
    game.debugJumpToNode("9032");
    game.loadGame(path.string());
    game.saveGame(path.string());
    check(VNSave::read(path.string()).gameData.find("17 de Julho (Seg)") !=
              std::string::npos,
          "Older saves recover the date from the original scenario title");
    check(game.debugNode() == node && text(game)->getText() == line,
          "Save restored wrong script or dialogue");
    std::filesystem::remove(path);
    capture(engine, game, "/tmp/reallive-story.bmp");
    auto &menu = GetInGameMenuRuntime(engine);
    menu.open();
    auto paused = game.debugNode();
    for (int n = 0; n < 10; ++n)
      engine.Iterate();
    check(game.debugNode() == paused, "Pause menu advanced story");
    auto touch = [&](Uint32 type, int id) {
      SDL_Event event{};
      event.type = type;
      event.tfinger.touchID = 1;
      event.tfinger.fingerID = id;
      event.tfinger.x = 0.5f + id * 0.02f;
      event.tfinger.y = 0.5f;
      engine.Event(&event);
    };
    touch(SDL_EVENT_FINGER_DOWN, 1);
    touch(SDL_EVENT_FINGER_DOWN, 2);
    touch(SDL_EVENT_FINGER_UP, 1);
    touch(SDL_EVENT_FINGER_UP, 2);
    check(!menu.IsOpen(), "Two-finger touch did not close REALLIVE pause menu");
    const auto progressStart = SDL_GetTicks();
    std::string lastNode = game.debugNode();
    int progress = 0;
    for (int n = 0;
         n < 1200 && SDL_GetTicks() - progressStart < 40000 && progress < 500;
         ++n) {
      if (text(game))
        text(game)->completeDialogue();
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = 75;
      event.button.y = 75;
      game.onEvent(event);
      event.type = SDL_EVENT_MOUSE_BUTTON_UP;
      game.onEvent(event);
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      if (game.debugNode() != lastNode) {
        lastNode = game.debugNode();
        ++progress;
      }
      SDL_Delay(10);
    }
    std::cout << "Story progress transitions=" << progress << " at "
              << game.debugNode() << '\n';
    check(game.debugNode() != node, "Restored dialogue could not advance");
    // Reproduce the reported live block and exercise multi-image transitions.
    for (const auto &entry :
         {"170:2441", "173:14", "163:1373", "702:25244", "9037:118"}) {
      game.debugJumpToNode(entry);
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      if (game.debugNode() == entry)
        throw std::runtime_error(
            std::string("Regression command did not execute at ") + entry);
    }

    std::cout << "Original startup/title, Portuguese dialogue, save/load and "
                 "pause passed at "
              << game.debugNode() << '\n';
  } catch (std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
