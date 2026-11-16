// Vita SCF diagnostics: explicitly not a playable Vita game.
#include <Amagami/VitaGraphicsAssets.hpp>
#include <Amagami/VitaScripts.hpp>
#include <Amagami/VitaSpriteRenderer.hpp>
#include <Amagami/VitaVmBridge.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void reportScene(const Amagami::Vita::VitaVmBridge &bridge) {
  const auto &view = bridge.view();
  std::cout << "Vita view: scale=" << view.scale << " originY=" << view.originY
            << '\n';
  std::cout << "Vita Sprite objects: " << bridge.sprites().size() << '\n';
  size_t shown = 0;
  for (const auto &[uid, sprite] : bridge.sprites()) {
    if (++shown > 12)
      break;
    std::cout << "  sprite " << uid << " texture=" << sprite.texture
              << " variant=" << sprite.variant << " layer=" << sprite.layer
              << " image=" << (sprite.hasImage ? "decoded" : "none/unknown")
              << " rect=(" << sprite.rect[0] << ", " << sprite.rect[1] << ", "
              << sprite.rect[2] << ", " << sprite.rect[3] << ")\n";
  }
}
void saveSnapshot(
    const std::string &path, const Amagami::Vita::VitaVmBridge &bridge,
    const std::shared_ptr<Kotonoha::Providers::VitaGraphicsAssets> &graphics) {
  Kotonoha::SurfacePtr canvas(
      SDL_CreateSurface(960, 544, SDL_PIXELFORMAT_RGBA32));
  if (!canvas)
    throw std::runtime_error(SDL_GetError());
  {
    Kotonoha::RendererPtr software(SDL_CreateSoftwareRenderer(canvas.get()));
    if (!software)
      throw std::runtime_error(SDL_GetError());
    Amagami::Vita::VitaSpriteRenderer presenter(
        software.get(), [graphics](uint32_t id, uint32_t variant) {
          return graphics ? graphics->uiSurface(id, variant)
                          : Kotonoha::SurfacePtr{};
        });
    presenter.setNamedSource([graphics](const std::string &name) {
      return graphics && graphics->contains(name) ? graphics->loadSurface(name)
                                                  : Kotonoha::SurfacePtr{};
    });
    presenter.setFontSource(
        [graphics](unsigned font, uint16_t code, unsigned palette) {
          return graphics ? graphics->fontGlyph(font, code, palette)
                          : Kotonoha::SurfacePtr{};
        });
    presenter.draw(bridge);
    if (!SDL_RenderPresent(software.get()))
      throw std::runtime_error(SDL_GetError());
  }
  if (!SDL_SaveBMP(canvas.get(), path.c_str()))
    throw std::runtime_error(SDL_GetError());
  std::cout << "Vita scene preview saved: " << path
            << " (960x544 BMP, snapshot only)\n";
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2 || argc > 10) {
    std::cerr << "Usage: AmagamiVitaProbe DECRYPTED_GAME_DIR "
                 "[--boot | --probe CLASS METHOD [--instance]] "
                 "[--ui-elf PLAIN_EBOOT_ELF] [--snapshot PREVIEW.bmp]\n"
                 "Diagnostic only: no playable Vita input or audio.\n";
    return 2;
  }
  try {
    int positional = argc;
    const bool haveSnapshot =
        positional >= 4 && std::string(argv[positional - 2]) == "--snapshot";
    const std::string snapshot =
        haveSnapshot ? std::string(argv[positional - 1]) : std::string{};
    if (haveSnapshot)
      positional -= 2;
    const bool haveElf =
        positional >= 4 && std::string(argv[positional - 2]) == "--ui-elf";
    const std::string elf =
        haveElf ? std::string(argv[positional - 1]) : std::string{};
    if (haveElf)
      positional -= 2;
    if (positional != 2 && positional != 3 && positional != 5 &&
        positional != 6)
      throw std::invalid_argument("Invalid Vita probe argument count");

    Amagami::Vita::VitaScripts source(argv[1]);
    Amagami::Vm vm;
    Amagami::Vita::VitaVmBridge bridge;
    source.installAll(vm);
    vm.scopedPrimitive = [&bridge](const std::string &owner,
                                   const std::string &method, bool classMethod,
                                   const Amagami::VmValue &self,
                                   unsigned number,
                                   const std::vector<Amagami::VmValue> &args) {
      return bridge.primitive(owner, method, classMethod, self, number, args);
    };
    std::shared_ptr<Kotonoha::Providers::VitaGraphicsAssets> graphics;
    if (haveElf) {
      graphics = std::make_shared<Kotonoha::Providers::VitaGraphicsAssets>(
          (std::filesystem::path(argv[1]) / "graph" / "GRAPH.CPK").string(),
          elf);
      // The new Sprite SCF primitives consult the actual CPK/ELF decoder
      // to get a picture's dimensions. Only metadata is retained by the
      // scene bridge, never a second copy of every pixel buffer.
      bridge.setTextureSizeResolver(
          [graphics](uint32_t texture, uint32_t variant)
              -> std::optional<std::pair<uint32_t, uint32_t>> {
            auto surface = graphics->uiSurface(texture, variant);
            if (!surface)
              return std::nullopt;
            return std::make_pair(uint32_t(surface->w), uint32_t(surface->h));
          });
    }
    if (graphics)
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
    std::cout << "Vita SCF VM loaded " << source.systemCount()
              << " system classes and " << source.scenarioCount()
              << " scenario classes\n";
    if (positional == 2) {
      if (!snapshot.empty())
        saveSnapshot(snapshot, bridge, graphics);
      return 0;
    }

    std::string klass = "Application", method = "run";
    bool instance = false;
    if (std::string(argv[2]) == "--boot") {
      if (positional != 3)
        throw std::invalid_argument("--boot takes no extra arguments");
    } else if (std::string(argv[2]) == "--probe") {
      if (positional != 5 && positional != 6)
        throw std::invalid_argument(
            "--probe expects CLASS METHOD [--instance]");
      klass = argv[3];
      method = argv[4];
      if (positional == 6) {
        if (std::string(argv[5]) != "--instance")
          throw std::invalid_argument("Unknown --probe option");
        instance = true;
      }
    } else {
      throw std::invalid_argument("Expected --boot or --probe");
    }
    const auto receiver =
        instance ? vm.instance(klass)
                 : Amagami::VmValue::str(klass, Amagami::VmValue::Class);
    if (!vm.hasClass(klass))
      throw std::runtime_error("No Vita SCF class " + klass);
    vm.call(receiver, method);
    try {
      vm.run(250000); // bounded diagnostic, never an unattended game loop
    } catch (...) {
      reportScene(bridge); // Inspect objects reached before an unknown native.
      if (!snapshot.empty())
        saveSnapshot(snapshot, bridge, graphics);
      throw;
    }
    reportScene(bridge);
    if (!snapshot.empty())
      saveSnapshot(snapshot, bridge, graphics);
    if (!vm.finished()) {
      std::cout << "Vita VM budget exhausted at " << vm.location() << '\n';
      return 3;
    }
    std::cout << "Vita VM returned from " << klass << "." << method
              << " (diagnostic only)\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Vita VM stopped: " << e.what() << '\n';
    return 1;
  }
}
