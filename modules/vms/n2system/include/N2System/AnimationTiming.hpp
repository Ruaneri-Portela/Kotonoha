// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
// Behavioural parity with NitroSharp.Rebuild.Animation.GetFactor and
// NitroSharp.NsScript.Primitives.NsEaseFunction (C++17).
#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

namespace N2System {

enum class EaseFunction : int {
  Linear = 0, QuadraticIn = 1, CubicIn = 2, QuarticIn = 3,
  QuadraticOut = 4, CubicOut = 5, QuarticOut = 6,
  SineIn = 7, SineOut = 8, SineInOut = 9, SineOutIn = 10
};

inline float easeFactor(float progress,
                        EaseFunction function = EaseFunction::Linear) {
  const float t = std::clamp(progress, 0.0f, 1.0f);
  constexpr float pi = 3.14159265358979323846f;
  switch (function) {
  case EaseFunction::QuadraticIn: return t * t;
  case EaseFunction::CubicIn: return t * t * t;
  case EaseFunction::QuarticIn: return t * t * t * t;
  case EaseFunction::QuadraticOut: return 1.0f - (1.0f - t) * (1.0f - t);
  case EaseFunction::CubicOut: return 1.0f - std::pow(1.0f - t, 3.0f);
  case EaseFunction::QuarticOut: return 1.0f - std::pow(1.0f - t, 4.0f);
  case EaseFunction::SineIn: return 1.0f - std::cos(t * pi * 0.5f);
  case EaseFunction::SineOut: return std::sin(t * pi * 0.5f);
  case EaseFunction::SineInOut: return 0.5f * (1.0f - std::cos(t * pi));
  case EaseFunction::SineOutIn:
    return std::acos(std::clamp(1.0f - t * 2.0f, -1.0f, 1.0f)) / pi;
  default: return t;
  }
}

inline EaseFunction parseEaseFunction(std::string_view value) {
  // NSB tempo arguments can be numeric enum values or spelling variants.
  int numeric = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                       numeric);
  if (parsed.ec == std::errc{} &&
      parsed.ptr == value.data() + value.size() &&
      numeric >= 0 && numeric <= 10)
    return static_cast<EaseFunction>(numeric);
  std::string name;
  name.reserve(value.size());
  for (const char ch : value) {
    if (ch >= 'A' && ch <= 'Z')
      name.push_back(static_cast<char>(ch - 'A' + 'a'));
    else if (ch != '-' && ch != '_' && ch != ' ')
      name.push_back(ch);
  }
  if (name == "quadraticeasein" || name == "easeinquad")
    return EaseFunction::QuadraticIn;
  if (name == "cubiceasein" || name == "easeincubic")
    return EaseFunction::CubicIn;
  if (name == "quarticeasein") return EaseFunction::QuarticIn;
  if (name == "quadraticeaseout" || name == "easeoutquad")
    return EaseFunction::QuadraticOut;
  if (name == "cubiceaseout" || name == "easeoutcubic")
    return EaseFunction::CubicOut;
  if (name == "quarticeaseout") return EaseFunction::QuarticOut;
  if (name == "sineeasein") return EaseFunction::SineIn;
  if (name == "sineeaseout") return EaseFunction::SineOut;
  if (name == "sineeaseinout") return EaseFunction::SineInOut;
  if (name == "sineeaseoutin") return EaseFunction::SineOutIn;
  return EaseFunction::Linear;
}

} // namespace N2System
