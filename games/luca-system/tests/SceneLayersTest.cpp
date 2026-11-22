// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/SceneLayers.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <vector>

static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static bool close(float a, float b) { return std::fabs(a - b) <= 0.001f; }
int main() {
  try {
    LucaSystem::SceneLayers scene;
    scene.stage({1, 20, -10});
    check(scene.pending().size() == 1 && scene.visible().empty(),
          "IMAGELOAD must stage before DRAW");
    scene.commit(100, 0);
    check(scene.pending().empty() && scene.visible().size() == 1 &&
              scene.visible()[0].x == 20,
          "instant DRAW commits pending visual objects");
    scene.stage({2, 100, 200});
    scene.commit(1000, 200);
    check(scene.outgoing().size() == 1 && scene.visible().size() == 2 &&
              scene.incomingStart() == 1,
          "timed DRAW remembers old objects and separates incoming layer");
    auto atStart = scene.blend(1000);
    auto halfway = scene.blend(1100);
    auto done = scene.blend(1500);
    check(close(atStart.outgoing, 1) && close(atStart.incoming, 0) &&
              close(halfway.outgoing, 0.5f) && close(halfway.incoming, 0.5f) &&
              close(done.outgoing, 0) && close(done.incoming, 1),
          "crossfade interpolates alpha with a stable clock");
    check(close(LucaSystem::wipeVisibleWidth(100, 200, 0), 0) &&
              close(LucaSystem::wipeVisibleWidth(100, 200, 200), 100) &&
              close(LucaSystem::wipeVisibleWidth(100, 200, 500), 200) &&
              close(LucaSystem::wipeVisibleWidth(-50, 150, 50), 100) &&
              close(LucaSystem::wipeVisibleWidth(100, -10, 200), 0),
          "directional wipe crops without resizing or revealing beyond edge");
    check(close(LucaSystem::croppedSpriteOrigin(640, 360, 20, 1, 0, 0),
                300) &&
              close(LucaSystem::croppedSpriteOrigin(640, 360, 20, 0.5f, 0, 0),
                    470) &&
              close(LucaSystem::croppedSpriteOrigin(640, 360, 20, 2, 4, -2),
                    -38) &&
              close(LucaSystem::croppedSpriteOrigin(400, 120, 30, 0.5f, 10, 5),
                    370),
          "scaled CZ4 character crop and anchor retain canvas coordinates");
    check(LucaSystem::isCharacterFragment(384, 640, 1280, 720,
                                          200, 144, 1280, 720) &&
              !LucaSystem::isCharacterFragment(384, 640, 1280, 720,
                                               380, 620, 1280, 720) &&
              !LucaSystem::isCharacterFragment(384, 640, 1280, 720,
                                               200, 144, 640, 480),
          "AIR expression crop overlays a matching portrait canvas");
    check(LucaSystem::airCalendarBadgeName(16) == "jul_16" &&
              LucaSystem::airCalendarBadgeName(31) == "jul_31" &&
              LucaSystem::airCalendarBadgeName(1) == "aug_01" &&
              LucaSystem::airCalendarBadgeName(15) == "aug_15" &&
              LucaSystem::airCalendarBadgeName(0).empty(),
          "AIR calendar follows the July-to-August script day cycle");
    // An outgoing scene must remain cached while crossfading or wiping;
    // release it only after the transition completes.
    struct SlotId { std::uint32_t assetId = 0; };
    std::map<std::uint16_t, SlotId> staged{{1, {10}}, {2, {12}}};
    std::map<std::uint16_t, SlotId> visible{{1, {10}}, {3, {13}}};
    std::map<std::uint16_t, SlotId> outgoing{{1, {10}}, {4, {14}}};
    std::unordered_map<std::uint32_t, int> imageCache{
        {10, 1}, {12, 1}, {13, 1}, {14, 1}, {99, 1}};
    std::unordered_map<std::uint32_t, int> textureCache{
        {10, 1}, {12, 1}, {13, 1}, {14, 1}, {99, 1}};
    const auto evicted = LucaSystem::pruneAirObjectCaches(
        staged, visible, outgoing, imageCache, textureCache);
    check(evicted == 2 && !imageCache.count(99) &&
              !textureCache.count(99) && imageCache.count(14),
          "active and outgoing scene assets survive GPU cache pruning");
    outgoing.clear();
    const auto discarded = LucaSystem::pruneAirObjectCaches(
        staged, visible, outgoing, imageCache, textureCache);
    check(discarded == 2 && !imageCache.count(14) &&
              !textureCache.count(14) && imageCache.count(13) &&
              textureCache.count(12),
          "finished transition releases obsolete CPU and GPU images");
    staged.clear();
    visible.clear();
    LucaSystem::pruneAirObjectCaches(staged, visible, outgoing,
                                     imageCache, textureCache);
    check(imageCache.empty() && textureCache.empty(),
          "returning to title releases retained gameplay images");
    // Repeated MOVE commands must not teleport between keyframes.
    check(close(LucaSystem::airMoveOffset(0, 100, 1000, 1000, 1000), 0) &&
              close(LucaSystem::airMoveOffset(0, 100, 1000, 1000, 1500), 50) &&
              close(LucaSystem::airMoveOffset(0, 100, 1000, 1000, 2500), 100) &&
              close(LucaSystem::airMoveOffset(8, 21, 1000, 0, 1200), 21),
          "MOVE interpolation clamps cleanly to start/end");
    const auto interrupted =
        LucaSystem::airMoveOffset(0.0f, 100.0f, 1000, 1000, 1250);
    check(close(interrupted, 25.0f) &&
              close(LucaSystem::airMoveOffset(interrupted,
                                              interrupted + 60.0f,
                                              1250, 500, 1250),
                    25.0f) &&
              close(LucaSystem::airMoveOffset(interrupted,
                                              interrupted + 60.0f,
                                              1250, 500, 1500),
                    55.0f),
          "interrupted MOVE preserves the onscreen position");
    struct PortraitVisual {
      float posX = 0, posY = 0;
      float startOffsetX = 0, startOffsetY = 0;
      float targetOffsetX = 0, targetOffsetY = 0;
      std::uint64_t moveStart = 0, moveDuration = 0;
      float scaleX = 1, scaleY = 1, rotation = 0, alpha = 1;
      std::uint8_t r = 255, g = 255, b = 255;
      bool visible = true;
    };
    PortraitVisual base{};
    base.posX = 420;
    base.posY = 160;
    base.scaleX = 0.5f;
    base.scaleY = 0.5f;
    base.alpha = 0.65f;
    base.rotation = -13;
    base.startOffsetX = 3;
    base.targetOffsetX = 29;
    base.moveStart = 500;
    base.moveDuration = 1000;
    base.r = 100;
    base.visible = false;
    PortraitVisual fragment{};
    LucaSystem::inheritAirPortraitVisuals(fragment, base, true);
    check(close(fragment.posX, 420) && close(fragment.posY, 160) &&
              close(fragment.scaleX, 0.5f) &&
              close(fragment.rotation, -13) &&
              close(fragment.alpha, 0.65f) &&
              close(fragment.targetOffsetX, 29) &&
              fragment.moveDuration == 1000 && fragment.r == 100 &&
              !fragment.visible,
          "CZ4 expression inherits body scaling, tint, motion and position");
    PortraitVisual explicitlyPlaced{};
    explicitlyPlaced.posX = 10;
    explicitlyPlaced.posY = 20;
    LucaSystem::inheritAirPortraitVisuals(explicitlyPlaced, base, false);
    check(close(explicitlyPlaced.posX, 10) &&
              close(explicitlyPlaced.posY, 20) &&
              close(explicitlyPlaced.scaleX, 0.5f),
          "explicit expression positions remain authored by the script");
    // Draw a face fragment directly above its corresponding body instead of
    // above all portraits. An orphan fragment is intentionally not visible.
    std::map<std::uint16_t, int> layered = {
        {0x0000, 1}, {0x0200, 2}, {0x0201, 3},
        {0x0300, 4}, {0x0301, 5}, {0x0303, 6}, {0x0400, 7}};
    check(LucaSystem::orderedAirLayerKeys(layered) ==
              std::vector<std::uint16_t>(
                  {0x0000, 0x0200, 0x0300, 0x0201, 0x0301, 0x0400}),
          "face fragments preserve character z-order and reject orphans");
    const auto wipeX = LucaSystem::wipeSplitX(1280, 0.375f);
    check(wipeX == 480 && (1280 - wipeX) + wipeX == 1280 &&
              LucaSystem::wipeSplitX(1280, -1.0f) == 0 &&
              LucaSystem::wipeSplitX(1280, 2.0f) == 1280 &&
              LucaSystem::wipeSplitX(1280,
                  std::numeric_limits<float>::quiet_NaN()) == 0 &&
              LucaSystem::wipeSplitX(0, 0.5f) == 0,
          "scene-wide wipe splits non-overlapping clips exactly");
    // Wipe transitions now carry a dedicated mode instead of silently
    // reusing alpha blending. A renderer can clip incoming objects to the
    // reveal edge without distorting their original image coordinates.
    scene.stage({4, 55, 66});
    scene.commit(1500, 200, LucaSystem::SceneTransition::WipeLeftToRight);
    const auto wipeStart = scene.blend(1500);
    const auto wipeHalf = scene.blend(1600);
    const auto wipeEnd = scene.blend(1700);
    check(scene.outgoing().size() == 2 && scene.visible().size() == 3 &&
              scene.incomingStart() == 2,
          "WIPE reuses only newly staged objects, not the outgoing scene");
    check(wipeStart.wipeLeftToRight && wipeHalf.wipeLeftToRight &&
              wipeEnd.wipeLeftToRight && close(wipeStart.incoming, 0) &&
              close(wipeHalf.incoming, 0.5f) && close(wipeEnd.incoming, 1),
          "WIPE exposes a timed, directional reveal extent");
    scene.commit(1800, 0);
    scene.beginBlackout(1500, 200, true);
    check(close(scene.blend(1500).blackout, 0) &&
              close(scene.blend(1600).blackout, 0.5f) &&
              close(scene.blend(1700).blackout, 1),
          "fade-out approaches an opaque screen");
    scene.beginBlackout(1800, 100, false);
    check(close(scene.blend(1800).blackout, 1) &&
              close(scene.blend(1900).blackout, 0),
          "fade-in restores the rendered scene");
    scene.stage({3, 9, 9});
    scene.reset({{77, -15, 33}});
    check(scene.visible().size() == 1 && scene.visible()[0].id == 77 &&
              scene.incomingStart() == 0 && scene.outgoing().empty() &&
              scene.pending().empty() && close(scene.blend(1900).incoming, 1) &&
              close(scene.blend(1900).blackout, 0),
          "save restore resets pending objects and in-flight transitions");
    bool rejected = false;
    try {
      std::vector<LucaSystem::SceneObject> many(
          LucaSystem::SceneLayers::MaxObjects + 1);
      scene.reset(many);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected && scene.visible().size() == 1,
          "oversized restore must preserve existing scene");
    std::cout << "LucaSystem composition tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LucaSystem composition tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
