// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Minimum provable parity gate for a REALLIVE retail edition. One executable
// serves every edition: the source is either an ISO image or an installed
// directory, supplied by the configure-time gate (or its runtime env alias).
// It exercises the same format path as the game: read-only VFS mount,
// Seen.txt scenario archive, a real G00 decode and a short engine boot.
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <RealLive/Images.hpp>
#include <RealLive/RealLiveAssets.hpp>
#include <RealLive/Script.hpp>
#include <RealLiveRuntime.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::string lower(std::string value) {
  for (char &c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}
bool endsWith(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::string source = argc > 1 ? argv[1] : "";
    const char *envName = argc > 2 ? argv[2] : "";
    if (source.empty() && envName && *envName)
      if (const char *env = SDL_getenv(envName))
        source = env;
    if (source.empty() || !std::filesystem::exists(source)) {
      std::cerr << "[SKIP] retail edition source not provided or invalid\n";
      return 77;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
      std::cerr << "[SKIP] SDL initialization: " << SDL_GetError() << '\n';
      return 77;
    }
    auto mounted = Kotonoha::Filesystem::Filesystem::mount(source);
    require(Kotonoha::Filesystem::findRequiredFilesPrefix(
                *mounted, {"Seen.txt", "Gameexe.ini"})
                .has_value(),
            "mount exposes Seen.txt and Gameexe.ini");

    auto assets =
        std::make_shared<Kotonoha::Providers::RealLiveAssets>(mounted);
    // Seen.txt is read through the provider so #REGNAME overrides stay intact.
    auto scripts = assets->scripts();
    const auto scenarios = scripts.ScenarioNumbers();
    require(!scenarios.empty(), "Seen.txt lists at least one scenario");
    require(scripts.ValidateAll() > 0, "scenario bytecode parses");

    // The provider's prefix filter is case-sensitive; editions ship both
    // "g00/" and "G00/", so filter the full public asset list ourselves.
    auto images = assets->list();
    images.erase(std::remove_if(images.begin(), images.end(),
                                [](const std::string &id) {
                                  const auto name = lower(id);
                                  return !endsWith(name, ".g00") ||
                                         name.find("g00/") != 0;
                                }),
                 images.end());
    std::sort(images.begin(), images.end());
    std::size_t attempted = 0;
    std::size_t decoded = 0;
    std::string chosen;
    std::uint32_t width = 0, height = 0;
    for (const auto &id : images) {
      if (attempted++ >= 32)
        continue;
      const auto bytes = assets->readAsset(id);
      if (bytes.empty())
        continue;
      try {
        const auto image = RealLive::decodeG00(bytes);
        require(image.width && image.height &&
                    image.rgba.size() ==
                        std::size_t(image.width) * image.height * 4,
                "G00 decodes to a complete RGBA raster");
        ++decoded;
        if (chosen.empty()) {
          chosen = id;
          width = image.width;
          height = image.height;
        }
        break;
      } catch (const std::exception &error) {
        std::cerr << "[note] " << id << ": " << error.what() << '\n';
      }
    }
    if (decoded == 0)
      std::cerr << "[note] no G00 decoded from " << images.size()
                << " listed candidates\n";
    require(decoded > 0, "at least one real retail G00 decodes");

    // Boot the actual runtime for a few frames; a missing/locked title must
    // surface as a diagnostic instead of silently passing the format checks.
    RealLive::RealLiveRuntime game(assets);
    game.SetState(Kotonoha::GameState::Menu);
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    Kotonoha::Engine engine(game, options);
    struct Guard {
      RealLive::RealLiveRuntime &game;
      ~Guard() { game.shutdown(); }
    } guard{game};
    for (int frame = 0; frame < 3; ++frame)
      engine.Iterate();
    require(game.diagnostic().empty(), "engine boot has no diagnostic");

    std::cout << "REALLIVE retail edition PASS: " << source << '\n'
              << "  scenarios=" << scenarios.size()
              << " g00_candidates=" << images.size() << " decoded=" << decoded
              << " first_g00=" << chosen << " (" << width << "x" << height
              << ")\n";
    SDL_Quit();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "REALLIVE retail edition FAIL: " << error.what() << '\n';
    SDL_Quit();
    return 1;
  }
}
