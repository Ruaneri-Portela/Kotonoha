// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace LucaSystem {
// Lightweight scene state: original image asset IDs plus screen positions,
// opacity, scaling, rotation and priority parameters.
// Rendering belongs to SDL; this stage/commit/transition clock is testable
// without an engine, GPU or original AIR assets.
struct SceneObject {
  std::uint32_t id = 0;
  int x = 0, y = 0;
  float alpha = 1.0f;
  float scaleX = 1.0f, scaleY = 1.0f;
  float rotation = 0.0f;
  int priority = 0;
  bool visible = true;
  std::uint8_t r = 255, g = 255, b = 255;
};
// Width of an object's source rectangle revealed to the left of the
// screen-space wipe boundary. Crop source and destination by this SAME width.
inline float wipeVisibleWidth(float objectLeft, float imageWidth,
                              float revealRight) {
  if (imageWidth <= 0.0f)
    return 0.0f;
  return std::clamp(revealRight - objectLeft, 0.0f, imageWidth);
}
enum class SceneTransition { Crossfade, WipeLeftToRight };
struct SceneBlend {
  float outgoing = 0, incoming = 1, blackout = 0;
  bool wipeLeftToRight = false;
};
class SceneLayers {
public:
  static constexpr std::size_t MaxObjects = 4096;
  const std::vector<SceneObject> &visible() const { return visible_; }
  const std::vector<SceneObject> &outgoing() const { return outgoing_; }
  const std::vector<SceneObject> &pending() const { return pending_; }
  // A transition appends staged objects to the previously visible layer.
  // Rendering that previous layer twice makes transparent pixels too dark.
  std::size_t incomingStart() const { return outgoing_.size(); }
  void stage(SceneObject object) {
    if (visible_.size() + pending_.size() >= MaxObjects)
      throw std::runtime_error("LucaSystem object layer limit exceeded");
    pending_.push_back(object);
  }
  template <typename F>
  bool updateObject(std::uint32_t id, F &&modifier) {
    bool found = false;
    for (auto &obj : visible_) {
      if (obj.id == id) {
        modifier(obj);
        found = true;
      }
    }
    for (auto &obj : pending_) {
      if (obj.id == id) {
        modifier(obj);
        found = true;
      }
    }
    return found;
  }
  void commit(std::uint64_t now, std::uint64_t durationMs,
              SceneTransition transition = SceneTransition::Crossfade) {
    if (pending_.empty()) return;
    const auto duration = std::min<std::uint64_t>(durationMs, 60000);
    if (duration)
      outgoing_ = visible_;
    else
      outgoing_.clear();
    visible_.insert(visible_.end(), pending_.begin(), pending_.end());
    pending_.clear();
    std::stable_sort(visible_.begin(), visible_.end(),
                     [](const SceneObject &a, const SceneObject &b) {
                       return a.priority < b.priority;
                     });
    began_ = now;
    duration_ = duration;
    transition_ = transition;
  }
  void beginBlackout(std::uint64_t now, std::uint64_t durationMs,
                     bool toBlack) {
    blackoutStart_ = now;
    blackoutDuration_ = std::min<std::uint64_t>(durationMs, 60000);
    blackoutToBlack_ = toBlack;
  }
  SceneBlend blend(std::uint64_t now) const {
    SceneBlend result;
    if (duration_) {
      const auto elapsed = now > began_
          ? std::min(now - began_, duration_) : std::uint64_t(0);
      result.incoming = float(elapsed) / float(duration_);
      result.outgoing = 1.0f - result.incoming;
      result.wipeLeftToRight =
          transition_ == SceneTransition::WipeLeftToRight;
    }
    if (blackoutDuration_) {
      const auto elapsed = now > blackoutStart_
          ? std::min(now - blackoutStart_, blackoutDuration_)
          : std::uint64_t(0);
      const float t = float(elapsed) / float(blackoutDuration_);
      result.blackout = blackoutToBlack_ ? t : 1.0f - t;
    }
    return result;
  }
  void reset(const std::vector<SceneObject> &objects = {}) {
    if (objects.size() > MaxObjects)
      throw std::runtime_error("LucaSystem restored object limit exceeded");
    visible_ = objects;
    pending_.clear();
    outgoing_.clear();
    duration_ = blackoutDuration_ = 0;
    transition_ = SceneTransition::Crossfade;
  }
private:
  std::vector<SceneObject> visible_, outgoing_, pending_;
  std::uint64_t began_ = 0, duration_ = 0;
  SceneTransition transition_ = SceneTransition::Crossfade;
  std::uint64_t blackoutStart_ = 0, blackoutDuration_ = 0;
  bool blackoutToBlack_ = false;
};
} // namespace LucaSystem
