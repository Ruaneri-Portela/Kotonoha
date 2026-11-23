// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// seen170 sprite regression: an AIR expression CZ4 is only a face crop of the
// displayed portrait. INIT(2,ff) + IMAGELOAD(face) must keep the portrait's
// pixels underneath even though the slot's transforms were cleared.
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/media/TextureAlpha.hpp>
#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <LucaSystem/SaveState.hpp>
#include <LucaSystem/SceneLayers.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
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

// Replay the exact seen170 window that produced the two visual reports:
// INIT(2,ff) at script index 818 removes the girl's full portrait, and the
// next expression-only IMAGELOAD at index 832 used to float over the school
// background with no body/anchor. The persistent per-slot portrait cache
// must restore cgmz20 beneath cgmz21p, as a cropped face (not an atlas).
void replaySeen170Expression(const std::string &directory) {
  auto assets = std::make_shared<Kotonoha::Providers::LucaSystemAssets>(
      directory, std::filesystem::path{});
  LucaSystem::LucaSystemRuntime runtime(assets);
  struct Cleanup {
    LucaSystem::LucaSystemRuntime &runtime;
    ~Cleanup() { runtime.shutdown(); }
  } cleanup{runtime};

  require(runtime.startScenario("seen170", 796),
          "replay seen170 from the full-portrait load at index 800");
  for (int i = 0; i < 64 && runtime.instructionPosition() < 835; ++i) {
    if (runtime.isDialogueVisible())
      runtime.advanceDialogue();
    else
      runtime.skipWait();
  }
  require(runtime.instructionPosition() == 835,
          "reached the 'Here you go.' expression scene");
  require(runtime.visibleLayerCount() == 2,
          "an expression crop following INIT(2,ff) keeps its portrait base "
          "instead of floating alone over the background");

  const auto file =
      (std::filesystem::temp_directory_path() / "luca-sprite-base-layers.vn")
          .string();
  runtime.saveGame(file);
  const auto raw = Kotonoha::Filesystem::readLocalFile(file);
  const std::string payload(raw.begin(), raw.end());
  const auto state = LucaSystem::decodeSaveState(payload);
  const LucaSystem::SavedVisualLayer *body = nullptr;
  const LucaSystem::SavedVisualLayer *face = nullptr;
  for (const auto &layer : state.visualLayers) {
    if (layer.group == 2 && layer.slot == 0)
      body = &layer;
    if (layer.group == 3 && layer.slot == 0)
      face = &layer;
  }
  require(body && body->assetId == 10413,
          "the restored base is the original 384x640 cgmz20 portrait");
  require(face && face->assetId == 10449,
          "the expression is composited as its own group-3 crop");
  require(body->scaleX == 1.0f && body->scaleY == 1.0f &&
              face->scaleX == 1.0f && face->scaleY == 1.0f,
          "portrait and expression share native 1:1 character scale");
  require(body->posX == 640.0f && body->posY == 720.0f &&
              face->posX == 640.0f && face->posY == 720.0f,
          "expression keeps the script's authored character anchor");

  const auto bodyImage =
      LucaSystem::DecodeCzImage(assets->readImageAsset(body->assetId));
  const auto faceImage =
      LucaSystem::DecodeCzImage(assets->readImageAsset(face->assetId));
  require(LucaSystem::isCharacterFragment(
              bodyImage.width, bodyImage.height, bodyImage.canvasWidth,
              bodyImage.canvasHeight, faceImage.width, faceImage.height,
              faceImage.canvasWidth, faceImage.canvasHeight),
          "the expression is a crop of the portrait canvas");
  const auto placement = LucaSystem::airCharacterCropPlacement(
      *body, bodyImage, faceImage, 0.0f, 0.0f, 0.0f, 0.0f);
  require(placement.w == float(faceImage.width) &&
              placement.h == float(faceImage.height) &&
              placement.w < float(bodyImage.canvasWidth) &&
              placement.h < float(bodyImage.canvasHeight),
          "only the expression frame is drawn, never the whole 1016x720 "
          "character atlas");
  require(placement.x >= body->posX - bodyImage.canvasWidth &&
              placement.x <= body->posX &&
              placement.y >= body->posY - bodyImage.canvasHeight &&
              placement.y <= body->posY,
          "the crop lands on the portrait canvas, not at an unrelated scale");
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
  replaySeen170Expression(directory);
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
