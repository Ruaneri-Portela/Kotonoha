// Experimental live Vita adapter: original SCF instructions run inside the
// Kotonoha Amagami VM. This is NOT a finished Vita game runtime.
#include <Amagami/VitaGraphicsAssets.hpp>
#include <Amagami/VitaInput.hpp>
#include <Amagami/VitaScripts.hpp>
#include <Amagami/VitaSpriteRenderer.hpp>
#include <Amagami/VitaVmBridge.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
class SDLRuntime {
public:
  SDLRuntime() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
      throw std::runtime_error(std::string("SDL3 init: ") + SDL_GetError());
  }
  ~SDLRuntime() { SDL_Quit(); }
  SDLRuntime(const SDLRuntime &) = delete;
  SDLRuntime &operator=(const SDLRuntime &) = delete;
};
struct Options {
  std::filesystem::path root;
  // The original Vita entry point is GameMain.scriptMain.
  std::string elf, klass = "GameMain", method = "scriptMain";
  unsigned maxFrames = 18000;
};
Options parseArgs(int argc, char **argv) {
  if (argc < 3)
    throw std::invalid_argument(
        "Usage: AmagamiVitaLive DECRYPTED_VITA_DIR PLAIN_EBOOT_ELF "
        "[--entry CLASS METHOD] [--frames 1..36000]\n"
        "A diagnostic Vita Sprite renderer, NOT a playable port.");
  Options options;
  options.root = argv[1];
  options.elf = argv[2];
  for (int i = 3; i < argc;) {
    const std::string flag = argv[i++];
    if (flag == "--entry" && i + 1 < argc) {
      options.klass = argv[i++];
      options.method = argv[i++];
    } else if (flag == "--frames" && i < argc) {
      const std::string value = argv[i++];
      std::size_t read = 0;
      const unsigned long parsed = std::stoul(value, &read);
      if (read != value.size() || parsed < 1 || parsed > 36000)
        throw std::invalid_argument("Invalid Vita diagnostic frame limit");
      options.maxFrames = unsigned(parsed);
    } else {
      throw std::invalid_argument("Unknown Vita diagnostic option: " + flag);
    }
  }
  if (options.klass.empty() || options.method.empty() ||
      options.klass.size() > 256 || options.method.size() > 256)
    throw std::invalid_argument("Invalid Vita SCF entry name");
  return options;
}
} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = parseArgs(argc, argv);
    const auto graphics =
        std::make_shared<Kotonoha::Providers::VitaGraphicsAssets>(
            (options.root / "graph" / "GRAPH.CPK").string(), options.elf);
    Amagami::Vita::VitaScripts scripts(options.root);
    Amagami::Vm vm;
    // Keep scenario classes unloaded until their K2X_Script.loadClass call
    // or an explicit SCF class literal. Loading every scenario up front
    // incorrectly makes Class.findClass report MahjongMain during dialogue.
    scripts.installSystem(vm);
    vm.resolveClass =
        [&scripts](const std::string &name) -> std::optional<Amagami::Script> {
      if (!scripts.hasScenario(name))
        return std::nullopt;
      return scripts.scenario(name);
    };
    if (!vm.loadClass(options.klass))
      throw std::runtime_error("Missing Vita SCF entry class: " +
                               options.klass);

    Amagami::Vita::VitaVmBridge bridge;
    bridge.setClassLoader(
        [&vm](const std::string &name) { return vm.loadClass(name); });
    bridge.setTextureSizeResolver(
        [graphics](uint32_t id, uint32_t variant)
            -> std::optional<std::pair<uint32_t, uint32_t>> {
          auto surface = graphics->uiSurface(id, variant);
          if (!surface)
            return std::nullopt;
          return std::make_pair(uint32_t(surface->w), uint32_t(surface->h));
        });
    bridge.setNamedPictureSizeResolver(
        [graphics](const std::string &name)
            -> std::optional<std::pair<uint32_t, uint32_t>> {
          if (!graphics->contains(name))
            return std::nullopt;
          auto surface = graphics->loadSurface(name);
          if (!surface)
            return std::nullopt;
          return std::make_pair(uint32_t(surface->w), uint32_t(surface->h));
        });
    vm.scopedPrimitive = [&bridge, &scripts,
                          &vm](const std::string &owner,
                               const std::string &method, bool classMethod,
                               const Amagami::VmValue &self, unsigned code,
                               const std::vector<Amagami::VmValue> &args) {
      // Scenario scripts can still test PS2 button bits (not SceCtrl).
      // The caller one frame below the native is their declaring SCF class.
      const bool ps2Layout = scripts.hasScenario(vm.callerClass());
      return bridge.primitive(owner, method, classMethod, self, code, args,
                              ps2Layout);
    };
    // A cooperative SCF yield every 1000 instructions prevents a script loop
    // from monopolizing the window's SDL event pump.
    vm.shouldYield = [] { return true; };
    vm.call(vm.instance(options.klass), options.method);

    SDLRuntime sdl;
    SDL_Window *rawWindow = nullptr;
    SDL_Renderer *rawRenderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Kotonoha / Amagami Vita (experimental)",
                                     960, 544, SDL_WINDOW_RESIZABLE, &rawWindow,
                                     &rawRenderer))
      throw std::runtime_error(std::string("SDL3 window: ") + SDL_GetError());
    Kotonoha::WindowPtr window(rawWindow);
    Kotonoha::RendererPtr renderer(rawRenderer);
    if (!SDL_SetRenderLogicalPresentation(renderer.get(), 960, 544,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX))
      throw std::runtime_error(SDL_GetError());
    Amagami::Vita::VitaSpriteRenderer presenter(
        renderer.get(), [graphics](uint32_t id, uint32_t variant) {
          return graphics->uiSurface(id, variant);
        });
    presenter.setNamedSource([graphics](const std::string &name) {
      return graphics->contains(name) ? graphics->loadSurface(name)
                                      : Kotonoha::SurfacePtr{};
    });
    presenter.setFontSource(
        [graphics](unsigned font, uint16_t code, unsigned palette) {
          return graphics ? graphics->fontGlyph(font, code, palette)
                          : Kotonoha::SurfacePtr{};
        });
    Amagami::Vita::VitaInput input;
    Kotonoha::GamepadPtr gamepad;
    int count = 0;
    Kotonoha::SDLArrayPtr<SDL_JoystickID> devices(SDL_GetGamepads(&count));
    if (devices && count > 0)
      gamepad.reset(SDL_OpenGamepad(devices.get()[0]));
    bool running = true, halted = false;
    std::string haltedAt, haltedReason;
    // SDL_Delay(16) was approximately 62.5 fps and drifted from the Vita's
    // 60-frame script clock. Pace presentations against a monotonic timer,
    // without accelerating the script after a slow frame.
    constexpr Uint64 framePeriodNs = 1000000000ULL / 60ULL;
    Uint64 nextFrameNs = SDL_GetTicksNS();

    for (unsigned frame = 0; running && frame < options.maxFrames; ++frame) {
      SDL_Event e{};
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
          running = false;
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad)
          gamepad.reset(SDL_OpenGamepad(e.gdevice.which));
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad &&
            SDL_GetGamepadID(gamepad.get()) == e.gdevice.which)
          gamepad.reset();
        input.handle(e);
      }
      if (!running)
        break;
      bridge.inputFrame(input.buttons(), input.analog());
      if (!vm.finished() && !halted) {
        if (vm.paused())
          vm.resume();
        try {
          vm.run(1000);
        } catch (const std::exception &e) {
          halted = true;
          haltedAt = vm.location();
          haltedReason = e.what();
          const std::string title =
              "Amagami Vita stopped at " + haltedAt.substr(0, 70);
          SDL_SetWindowTitle(window.get(), title.c_str());
          std::cerr << "Unimplemented Vita SCF at " << haltedAt << ": "
                    << haltedReason << '\n';
        }
      }
      bridge.tick(1);
      presenter.draw(bridge);
      if (!SDL_RenderPresent(renderer.get()))
        throw std::runtime_error(SDL_GetError());
      nextFrameNs += framePeriodNs;
      const Uint64 nowNs = SDL_GetTicksNS();
      if (nextFrameNs > nowNs)
        SDL_DelayPrecise(nextFrameNs - nowNs);
      else if (nowNs - nextFrameNs > framePeriodNs * 2)
        nextFrameNs = nowNs; // Never accumulate unbounded catch-up debt.
    }
    std::cout << "Vita SDL3 diagnostic: " << bridge.sprites().size()
              << " sprite(s), SCF "
              << (halted          ? "halted"
                  : vm.finished() ? "finished"
                                  : "interrupted")
              << ", texture cache " << presenter.cachedTextures() << '\n';
    return halted ? 3 : 0;
  } catch (const std::exception &e) {
    std::cerr << "Amagami Vita live diagnostic: " << e.what() << '\n';
    return 1;
  }
}
