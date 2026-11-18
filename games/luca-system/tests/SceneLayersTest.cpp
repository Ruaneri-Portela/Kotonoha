// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/SceneLayers.hpp>
#include <cmath>
#include <iostream>
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
