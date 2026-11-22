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
    {
      std::vector<std::uint8_t> rgba(4 * 4 * 4, 255);
      check(!LucaSystem::hasNonOpaquePixels(rgba),
            "Fully opaque CZ art may use background upscaling");
      rgba[3] = 0;
      check(LucaSystem::hasNonOpaquePixels(rgba),
            "Transparent CZ4 overlays must bypass image upscalers");
      rgba[3] = 255;
      check(LucaSystem::rasterCoversPointWithOpacity(
                rgba, 4, 4, 0, 0, 1, 1, 2, 2, 1.0f),
            "Opaque background covers the attached HUD point");
      rgba[(2 * 4 + 2) * 4 + 3] = 0;
      check(!LucaSystem::rasterCoversPointWithOpacity(
                rgba, 4, 4, 0, 0, 1, 1, 2, 2, 1.0f),
            "Transparent CZ4 holes do not justify calendar overlay");
      check(!LucaSystem::rasterCoversPointWithOpacity(
                rgba, 4, 4, 40, 40, 1, 1, 2, 2, 1.0f),
            "Calendar must not float outside a cropped background");
      check(!LucaSystem::rasterCoversPointWithOpacity(
                rgba, 4, 4, 0, 0, 1, 1, 1, 1, 0.2f),
            "Darkened/transparent background suppresses attached HUD");
    }
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
    // seen170 regression: the exterior is visible before choices. After
    // INIT, a transparent foreground in native group(0,0) must not
    // reveal the black framebuffer through its holes.
    struct AirSlot {
      std::uint8_t group = 0, slot = 0;
      std::uint32_t assetId = 0;
    };
    std::map<std::uint16_t, AirSlot> original{{0, {0, 0, 100}}};
    std::map<std::uint16_t, AirSlot> cutout{{0, {0, 0, 200}}};
    const auto isBackdrop = [](const AirSlot &obj) {
      return obj.assetId == 100 || obj.assetId == 300;
    };
    check(LucaSystem::retainAirBackdrop(cutout, original, isBackdrop) &&
          cutout.at(LucaSystem::AirPersistentBackdropKey).assetId == 100 &&
          LucaSystem::orderedAirLayerKeys(cutout) ==
              std::vector<std::uint16_t>({
                  LucaSystem::AirPersistentBackdropKey, 0}),
          "seen170 transparent foreground retains old outdoor background");
    std::map<std::uint16_t, AirSlot> foregroundOnly{{0x0201, {2, 1, 210}}};
    check(LucaSystem::retainAirBackdrop(foregroundOnly, cutout, isBackdrop) &&
          foregroundOnly.count(LucaSystem::AirPersistentBackdropKey) == 1,
          "background persists even when INIT clears group-0 staging");
    std::map<std::uint16_t, AirSlot> newBackground{{0, {0, 0, 300}}};
    newBackground[LucaSystem::AirPersistentBackdropKey] = {0xfe, 0, 100};
    check(!LucaSystem::retainAirBackdrop(newBackground, foregroundOnly,
                                         isBackdrop) &&
          !newBackground.count(LucaSystem::AirPersistentBackdropKey),
          "a full-screen opaque background replaces old backdrop");
    struct PixelImage {
      int width = 4, height = 2;
      int cropX = 0, cropY = 0, anchorX = 0, anchorY = 0;
      std::vector<std::uint8_t> rgba = std::vector<std::uint8_t>(32, 255);
    };
    struct PixelLayer {
      std::uint8_t group = 0, slot = 0;
      bool visible = true;
      float alpha = 1, posX = 0, posY = 0;
      float scaleX = 1, scaleY = 1, rotation = 0;
      float targetOffsetX = 0, targetOffsetY = 0;
    };
    PixelLayer bgPosition;
    PixelImage backgroundPixels;
    check(LucaSystem::isFullAirBackdrop(bgPosition, backgroundPixels, 4, 2),
          "full opaque authored scene qualifies as background");
    backgroundPixels.rgba[3] = 0;
    check(!LucaSystem::isFullAirBackdrop(bgPosition, backgroundPixels, 4, 2),
          "alpha-cutout image cannot overwrite persistent background");
    bgPosition.posX = 10;
    backgroundPixels.rgba[3] = 255;
    check(!LucaSystem::isFullAirBackdrop(bgPosition, backgroundPixels, 4, 2),
          "offscreen sprites do not qualify as scene backdrops");
    // A CZ4 face can have a different local anchor from its full-body
    // portrait. Both must nevertheless share the full canvas origin and
    // rotation pivot, including during MOVE and at non-unit scales.
    struct VisualPose {
      float posX = 640, posY = 360;
      float scaleX = 0.5f, scaleY = 0.5f;
    };
    struct CropInfo {
      int width = 0, height = 0;
      int cropX = 0, cropY = 0;
      int anchorX = 0, anchorY = 0;
    };
    const VisualPose pose{};
    const CropInfo fullPortrait{384, 640, 200, 40, 640, 360};
    const CropInfo faceOnly{200, 144, 290, 80, 10, 20};
    const auto bodyRect = LucaSystem::airCharacterCropPlacement(
        pose, fullPortrait, fullPortrait, 12, -8, 0, 0);
    const auto faceRect = LucaSystem::airCharacterCropPlacement(
        pose, fullPortrait, faceOnly, 12, -8, 0, 0);
    check(close(bodyRect.x, 432) && close(bodyRect.y, 192) &&
              close(faceRect.x, 477) && close(faceRect.y, 212) &&
              close(bodyRect.pivotX, 220) && close(faceRect.pivotX, 175) &&
              close(bodyRect.x + bodyRect.pivotX,
                    faceRect.x + faceRect.pivotX) &&
              close(bodyRect.y + bodyRect.pivotY,
                    faceRect.y + faceRect.pivotY) &&
              close(faceRect.w, 100) && close(faceRect.h, 72),
          "face crop uses portrait anchor/pivot rather than local face anchor");
    const auto wrongFaceRect = LucaSystem::airCharacterCropPlacement(
        pose, faceOnly, faceOnly, 12, -8, 0, 0);
    check(!close(wrongFaceRect.x, faceRect.x),
          "using a face's local header anchor causes visible misalignment");
    // A new IMAGELOAD asset in the same slot must not reset alpha,
    // scaling, movement or rotation, even with explicit X/Y.
    struct SpriteProperties {
      float posX = 100, posY = 200, alpha = 1;
      float startOffsetX = 0, startOffsetY = 0;
      float targetOffsetX = 0, targetOffsetY = 0;
      std::uint64_t moveStart = 0, moveDuration = 0;
      float scaleX = 1, scaleY = 1, rotation = 0;
      std::uint8_t r = 255, g = 255, b = 255;
      bool visible = true;
    };
    SpriteProperties oldPose{};
    oldPose.posX = 440; oldPose.scaleX = 0.65f;
    oldPose.alpha = 0.4f; oldPose.rotation = 19;
    oldPose.targetOffsetX = 75; oldPose.moveDuration = 900;
    oldPose.g = 120;
    SpriteProperties newPose{};
    newPose.posX = 95;
    LucaSystem::inheritAirPortraitVisuals(newPose, oldPose, false);
    check(close(newPose.posX, 95) && close(newPose.alpha, 0.4f) &&
              close(newPose.scaleX, 0.65f) && close(newPose.rotation, 19) &&
              newPose.moveDuration == 900 && newPose.g == 120,
          "explicit image replacement retains pose visual properties");
    LucaSystem::AirStageResetTracker initTracker;
    check(initTracker.allowsVisibleFallback(2, 1), "default slot inheritance");
    initTracker.clearSlot(2, 1);
    check(!initTracker.allowsVisibleFallback(2, 1) &&
              initTracker.allowsVisibleFallback(2, 2),
          "INIT single slot must not resurrect old pose");
    initTracker.clearGroup(2);
    check(!initTracker.allowsVisibleFallback(2, 2) &&
              initTracker.allowsVisibleFallback(0, 0),
          "INIT character group blocks previous pose inheritance");
    initTracker.clearAll();
    check(!initTracker.allowsVisibleFallback(0, 0),
          "global INIT blocks all stale sprite transforms");
    initTracker.reset();
    check(initTracker.allowsVisibleFallback(2, 1),
          "DRAW restores normal inheritance for next sprite staging cycle");
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
