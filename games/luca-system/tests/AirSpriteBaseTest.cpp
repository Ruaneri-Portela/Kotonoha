// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// seen170 sprite regression: an AIR expression CZ4 is only a face crop of the
// displayed portrait. INIT(2,ff) + IMAGELOAD(face) must keep the portrait's
// pixels underneath even though the slot's transforms were cleared.
#include <Kotonoha/media/TextureAlpha.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/SceneLayers.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

struct Slot {
  std::uint32_t assetId = 0;
  float alpha = 1.0f;
};

void syntheticTest() {
  const std::uint16_t key = (std::uint16_t(2) << 8);
  const std::map<std::uint16_t, Slot> staged;
  const std::map<std::uint16_t, Slot> visible{{key, {10413, 1.0f}}};
  LucaSystem::AirStageResetTracker clears;
  clears.clearGroup(2);

  require(!clears.allowsVisibleFallback(2, 0),
          "INIT must still block inheriting a cleared slot's transform");
  const auto *base = LucaSystem::airExpressionPixelBase(staged, visible, key);
  require(base && base->assetId == 10413,
          "INIT(2,ff) must not remove the displayed portrait's pixels that a "
          "following expression crop is composed over");

  const std::map<std::uint16_t, Slot> restaged{{key, {10459, 0.5f}}};
  base = LucaSystem::airExpressionPixelBase(restaged, visible, key);
  require(base && base->assetId == 10459,
          "a freshly staged portrait takes precedence over the visible one");

  const std::map<std::uint16_t, Slot> empty;
  require(LucaSystem::airExpressionPixelBase(empty, empty, key) == nullptr,
          "an orphan expression has no portrait base");
  clears.reset();
  require(clears.allowsVisibleFallback(2, 0),
          "DRAW restores transform inheritance for the next staging cycle");

  // Real AIR dimensions: cgmz21p (10401) is a face crop of cgmz20 (10413).
  require(
      LucaSystem::isCharacterFragment(384, 640, 1016, 720, 184, 144, 1016, 720),
      "the seen170 face image is a crop of its 1016x720 portrait canvas");
  std::map<std::uint16_t, int> layers{{0x0000, 1}, {key, 2}, {0x0300, 3}};
  require(LucaSystem::orderedAirLayerKeys(layers) ==
              std::vector<std::uint16_t>({0x0000, key, 0x0300}),
          "expression stays directly above its portrait after restoration");
}

void retailTest(const std::string &directory) {
  Kotonoha::Providers::LucaSystemAssets assets(directory, {});
  const auto body = LucaSystem::DecodeCzImage(assets.readImageAsset(10413));
  const auto face = LucaSystem::DecodeCzImage(assets.readImageAsset(10401));
  const auto opaque = [](const LucaSystem::CzImage &image) {
    std::size_t clear = 0, solid = 0;
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
      if (image.rgba[i] == 0)
        ++clear;
      else if (image.rgba[i] == 255)
        ++solid;
    }
    return std::pair{clear, solid};
  };
  const auto [bodyClear, bodySolid] = opaque(body);
  const auto [faceClear, faceSolid] = opaque(face);
  require(bodyClear > 0 && bodySolid > 0 && faceClear > 0 && faceSolid > 0,
          "CZ4 alpha reconstruction keeps both the silhouette and the crop");
  require(LucaSystem::isCharacterFragment(
              body.width, body.height, body.canvasWidth, body.canvasHeight,
              face.width, face.height, face.canvasWidth, face.canvasHeight),
          "retail face and portrait share the same character canvas");

  // Straight-alpha edge preparation must never alter alpha or visible RGB.
  auto pixels = body.rgba;
  const auto before = pixels;
  Kotonoha::prepareStraightAlphaForLinearFiltering(pixels.data(), body.width,
                                                   body.height, body.width * 4);
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    require(pixels[i + 3] == before[i + 3],
            "linear-filter preparation preserves alpha");
    if (before[i + 3] != 0)
      require(pixels[i] == before[i] && pixels[i + 1] == before[i + 1] &&
                  pixels[i + 2] == before[i + 2],
              "linear-filter preparation preserves visible RGB");
  }
}

} // namespace

int main(int argc, char **argv) {
  // This binary backs both the synthetic SpriteBase test and the retail-only
  // registration. Only --require-retail turns missing data into CTest Skipped.
  const bool requireRetail =
      argc > 1 && std::string(argv[1]) == "--require-retail";
  try {
    syntheticTest();
    std::string directory = !requireRetail && argc > 1 ? argv[1] : "";
    if (directory.empty()) {
      if (const char *env = std::getenv("LUCA_SYSTEM_TEST_DATA"))
        directory = env;
    }
    if (!directory.empty() && std::filesystem::exists(directory))
      retailTest(directory);
    else if (requireRetail) {
      std::cerr << "[SKIP] LUCA_SYSTEM_TEST_DATA not provided or invalid\n";
      return 77;
    } else
      std::cout << "skip - LUCA_SYSTEM_TEST_DATA not provided\n";
    std::cout << "AIR sprite base regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "AIR sprite base regressions failed: " << error.what() << '\n';
    return 1;
  }
}
