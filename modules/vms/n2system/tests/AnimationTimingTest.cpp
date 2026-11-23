// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Reference: NitroSharp.Rebuild.Animation.GetFactor and spline interpolation.
#include <N2System/AnimationTiming.hpp>
#include <N2System/SceneGraph.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
bool near(float a, float b, float eps = 0.002f) {
  return std::fabs(a - b) < eps;
}
} // namespace

int main() {
  using namespace N2System;
  constexpr EaseFunction functions[] = {
      EaseFunction::Linear,       EaseFunction::QuadraticIn,
      EaseFunction::CubicIn,      EaseFunction::QuarticIn,
      EaseFunction::QuadraticOut, EaseFunction::CubicOut,
      EaseFunction::QuarticOut,   EaseFunction::SineIn,
      EaseFunction::SineOut,      EaseFunction::SineInOut,
      EaseFunction::SineOutIn};
  for (const auto curve : functions) {
    check(near(easeFactor(0, curve), 0), "Ease must start at zero");
    check(near(easeFactor(1, curve), 1), "Ease must finish at one");
    float prev = -1;
    for (int i = 0; i <= 100; ++i) {
      const float t = easeFactor(float(i) / 100, curve);
      check(std::isfinite(t) && t >= prev, "Ease must be monotonic and finite");
      prev = t;
    }
  }
  check(near(easeFactor(0.5f, EaseFunction::QuadraticIn), 0.25f),
        "Quadratic-in uses NitroSharp curve");
  check(near(easeFactor(0.5f, EaseFunction::QuadraticOut), 0.75f),
        "Quadratic-out uses NitroSharp curve");
  check(parseEaseFunction("7") == EaseFunction::SineIn &&
            parseEaseFunction("SineEaseInOut") == EaseFunction::SineInOut &&
            parseEaseFunction("CubicEaseOut") == EaseFunction::CubicOut,
        "Named and numbered NSB tempo arguments map to NitroSharp");
  SceneGraph graph;
  auto *e = graph.createEntity("sprite", EntityType::Sprite);
  check(e != nullptr, "Create sprite");
  graph.moveEntity("sprite", 100, 200, 1000, EaseFunction::QuadraticIn);
  graph.fadeEntity("sprite", 0, 1000, EaseFunction::QuadraticOut);
  graph.zoomEntity("sprite", 2, 4, 1000, EaseFunction::SineInOut);
  graph.rotateEntity("sprite", 180, 1000, EaseFunction::Linear);
  graph.update(0.5f);
  e = graph.findEntity("sprite");
  check(near(e->x, 25) && near(e->y, 50), "Move interpolates using easing");
  check(near(e->opacity, 0.25f), "Fade interpolates using easing");
  check(near(e->scaleX, 1.5f) && near(e->scaleY, 2.5f),
        "Zoom interpolates using easing");
  check(near(e->angle, 90), "Rotate interpolates over elapsed time");
  graph.update(0.5f);
  check(near(e->x, 100) && near(e->opacity, 0) && near(e->scaleX, 2) &&
            !e->isAnimating(),
        "All tween end states are exact and waits may complete");

  e->x = 0;
  e->y = 0;
  graph.bezierMoveEntity("sprite", 0, 100, 100, 100, 100, 0, 1000);
  graph.update(0.5f);
  check(near(e->x, 50) && near(e->y, 75), "Cubic Bezier interpolation");
  graph.update(0.5f);
  check(near(e->x, 100) && near(e->y, 0) && !e->isBezierMoving,
        "Cubic Bezier completion returns to normal interpolation");

  graph.transitionEntity("sprite", 0, 1, 1000, "mask",
                         EaseFunction::QuadraticIn);
  graph.update(0.5f);
  check(near(e->opacity, 0.25f) && e->transitionMaskPath == "mask",
        "Transition easing and mask retained");
  std::cout << "NitroSharp easing and Bezier tests passed\n";
}
