// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_set>

namespace LucaSystem {

// Private bottommost layer retained across INIT/DRAW (V6 serializes it).
inline constexpr std::uint16_t AirPersistentBackdropKey = 0xfe00;


// AIR stores expression CZ4 fragments in (group=3, slot=N), while the
// matching portrait resides in (group=2, slot=N). Drawing all group-3
// slots after group-2 places a rear character's face over front characters.
// Draw each expression immediately above its own portrait instead. Do not
// paint an orphan expression without its portrait.
template <class Layer>
std::vector<std::uint16_t>
orderedAirLayerKeys(const std::map<std::uint16_t, Layer> &slots) {
  std::vector<std::uint16_t> result;
  result.reserve(slots.size());
  if (slots.find(AirPersistentBackdropKey) != slots.end())
    result.push_back(AirPersistentBackdropKey);
  for (const auto &[key, layer] : slots) {
    (void)layer;
    const unsigned group = unsigned(key) >> 8;
    if (group == 3 || key == AirPersistentBackdropKey)
      continue;
    result.push_back(key);
    if (group == 2) {
      const auto expression = std::uint16_t((3u << 8) | (key & 0xffu));
      if (slots.find(expression) != slots.end())
        result.push_back(expression);
    }
  }
  return result;
}


// INIT clears staged images, not necessarily the displayed background.
// Preserve the last opaque full-frame group(0,0) below foreground cutouts.
template <class Slots, class IsOpaqueBackdrop>
bool retainAirBackdrop(Slots &staged, const Slots &visible,
                       IsOpaqueBackdrop &&opaque) {
  constexpr std::uint16_t nativeBackgroundKey = 0;
  const auto next = staged.find(nativeBackgroundKey);
  if (next != staged.end() && opaque(next->second)) {
    staged.erase(AirPersistentBackdropKey);
    return false;
  }
  const auto previousBacking = visible.find(AirPersistentBackdropKey);
  const auto previousNative = visible.find(nativeBackgroundKey);
  const auto *source =
      previousBacking != visible.end() ? &previousBacking->second
      : previousNative != visible.end() && opaque(previousNative->second)
            ? &previousNative->second
            : nullptr;
  if (!source)
    return false;
  auto backing = *source;
  backing.group = 0xfe;
  backing.slot = 0;
  staged[AirPersistentBackdropKey] = backing;
  return true;
}

// An opaque background must cover the framebuffer, unlike cropped CZ art.
// Checking alpha avoids treating a full-canvas window/face overlay as a BG.
template <class Layer, class Image>
bool isFullAirBackdrop(const Layer &layer, const Image &img,
                       int viewportWidth = 1280, int viewportHeight = 720) {
  if (layer.group != 0 || layer.slot != 0 || !layer.visible ||
      !std::isfinite(layer.alpha) || layer.alpha < 0.99f ||
      !std::isfinite(layer.scaleX) || !std::isfinite(layer.scaleY) ||
      layer.scaleX <= 0 || layer.scaleY <= 0 ||
      std::fabs(layer.rotation) > 0.001f ||
      viewportWidth <= 0 || viewportHeight <= 0 ||
      img.width <= 0 || img.height <= 0)
    return false;
  const float left = layer.posX +
      (float(img.cropX) - float(img.anchorX)) * layer.scaleX +
      layer.targetOffsetX;
  const float top = layer.posY +
      (float(img.cropY) - float(img.anchorY)) * layer.scaleY +
      layer.targetOffsetY;
  if (!std::isfinite(left) || !std::isfinite(top) ||
      left > 1 || top > 1 ||
      left + img.width * layer.scaleX < viewportWidth - 1 ||
      top + img.height * layer.scaleY < viewportHeight - 1)
    return false;
  const auto count = std::size_t(img.width) * std::size_t(img.height);
  if (!count || img.rgba.size() < count * 4)
    return false;
  std::size_t transparent = 0;
  const auto maximum = count / 100;
  for (std::size_t i = 0; i < count; ++i)
    if (img.rgba[4 * i + 3] < 250 && ++transparent > maximum)
      return false;
  return true;
}

// Explicit INIT clears form a barrier: a later IMAGELOAD must not read a
// deleted pose from the previous committed scene. DRAW resets the barrier.
struct AirStageResetTracker {
  bool all = false;
  std::set<std::uint8_t> groups;
  std::set<std::uint16_t> keys;
  void reset() { all = false; groups.clear(); keys.clear(); }
  void clearAll() { all = true; groups.clear(); keys.clear(); }
  void clearGroup(std::uint8_t group) { groups.insert(group); }
  void clearSlot(std::uint8_t group, std::uint8_t slot) {
    keys.insert((std::uint16_t(group) << 8) | slot);
  }
  bool allowsVisibleFallback(std::uint8_t group, std::uint8_t slot) const {
    return !all && !groups.count(group) &&
           !keys.count((std::uint16_t(group) << 8) | slot);
  }
};

// If IMAGELOAD cannot decode a resource, leave the previous valid sprite
// in the upcoming scene. But INIT-cleared slots MUST remain cleared.
template <class Slots>
bool keepAirSpriteAfterDecodeFailure(
    Slots &staged, const Slots &visible, const AirStageResetTracker &clears,
    std::uint8_t group, std::uint8_t slot) {
  const auto key = std::uint16_t((std::uint16_t(group) << 8) | slot);
  if (staged.find(key) != staged.end() ||
      !clears.allowsVisibleFallback(group, slot))
    return false;
  const auto old = visible.find(key);
  if (old == visible.end())
    return false;
  staged.emplace(key, old->second);
  return true;
}

// An AIR expression CZ4 stores only a small crop of its portrait's canvas.
// Scripts routinely clear the character group with INIT and then load just
// the face crop for the next line. INIT must still prevent stale *transform*
// inheritance, but the pixels of the portrait currently on screen remain the
// base the crop is authored against. Dropping that base renders floating eyes
// over the background (or nothing at all when the old pose fades out during
// DRAW), which is the seen170 sprite regression.
template <class Slots>
auto airExpressionPixelBase(const Slots &staged, const Slots &visible,
                            std::uint16_t key)
    -> const typename Slots::mapped_type * {
  if (const auto it = staged.find(key); it != staged.end())
    return &it->second;
  if (const auto it = visible.find(key); it != visible.end())
    return &it->second;
  return nullptr;
}

// Evaluate motion at a monotonic story-clock instant. Sampling the current
// position before a second MOVE prevents a visible jump to the old endpoint.
inline float airMoveOffset(float start, float target, std::uint64_t began,
                           std::uint64_t duration, std::uint64_t now) {
  if (duration == 0 || (now >= began && now - began >= duration))
    return target;
  if (now <= began)
    return start;
  const float progress = static_cast<float>(now - began) /
                         static_cast<float>(duration);
  return start + (target - start) * progress;
}

// An AIR expression fragment shares its portrait's canvas transform. Copy
// animation, opacity, color modulation and scaling, but preserve an explicit
// per-expression position from the script when requested.
template <class Layer>
void inheritAirPortraitVisuals(Layer &layer, const Layer &portrait,
                               bool inheritPosition) {
  if (inheritPosition) {
    layer.posX = portrait.posX;
    layer.posY = portrait.posY;
  }
  layer.startOffsetX = portrait.startOffsetX;
  layer.startOffsetY = portrait.startOffsetY;
  layer.targetOffsetX = portrait.targetOffsetX;
  layer.targetOffsetY = portrait.targetOffsetY;
  layer.moveStart = portrait.moveStart;
  layer.moveDuration = portrait.moveDuration;
  layer.scaleX = portrait.scaleX;
  layer.scaleY = portrait.scaleY;
  layer.rotation = portrait.rotation;
  layer.alpha = portrait.alpha;
  layer.r = portrait.r;
  layer.g = portrait.g;
  layer.b = portrait.b;
  layer.visible = portrait.visible;
}

// Split a screen-wide WIPE into non-overlapping pixel-perfect halves.
// The old composition must never be drawn beneath the incoming half:
// otherwise transparent sprites, image fragments and white fades double up.
inline int wipeSplitX(int width, float progress) {
  if (width <= 0)
    return 0;
  const float fraction =
      std::isfinite(progress) ? std::clamp(progress, 0.0f, 1.0f) : 0.0f;
  return std::clamp(static_cast<int>(std::lround(width * fraction)), 0, width);
}


// Keep only images referenced by the staged scene, the committed scene or
// the outgoing snapshot of a still-running transition. This releases both
// decoded RGBA buffers (CPU) and SDL textures (GPU) as chapters advance.
template <class Slots, class Images, class Textures>
std::size_t pruneAirObjectCaches(const Slots &staged, const Slots &visible,
                                 const Slots &outgoing, Images &images,
                                 Textures &textures) {
  std::unordered_set<std::uint32_t> retained;
  retained.reserve(staged.size() + visible.size() + outgoing.size());
  const auto mark = [&](const Slots &slots) {
    for (const auto &[key, layer] : slots) {
      (void)key;
      if (layer.assetId != 0)
        retained.insert(layer.assetId);
    }
  };
  mark(staged);
  mark(visible);
  mark(outgoing);
  const auto removeUnused = [&](auto &cache) {
    for (auto it = cache.begin(); it != cache.end();) {
      if (!retained.count(it->first))
        it = cache.erase(it);
      else
        ++it;
    }
  };
  const auto before = images.size() + textures.size();
  removeUnused(images);
  removeUnused(textures);
  return before - images.size() - textures.size();
}

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

// CZ4 face and body pixels are crops of the same original character canvas.
// Their local crop dimensions and per-file anchor fields can differ. Placing
// and rotating each crop around its *own* rectangle shifts the face away
// from the pose. Derive both rectangles from the full portrait's anchor.
struct AirCharacterCropPlacement {
  float x = 0, y = 0, w = 0, h = 0;
  float pivotX = 0, pivotY = 0; // SDL rotation center relative to destination
};
template <class Pose, class BaseImage, class CropImage>
AirCharacterCropPlacement
airCharacterCropPlacement(const Pose &pose, const BaseImage &base,
                          const CropImage &crop, float offsetX,
                          float offsetY, float shakeX, float shakeY) {
  AirCharacterCropPlacement result;
  result.x = pose.posX +
             (float(crop.cropX) - float(base.anchorX)) * pose.scaleX +
             offsetX + shakeX;
  result.y = pose.posY +
             (float(crop.cropY) - float(base.anchorY)) * pose.scaleY +
             offsetY + shakeY;
  result.w = float(crop.width) * pose.scaleX;
  result.h = float(crop.height) * pose.scaleY;
  result.pivotX = (float(base.anchorX) - float(crop.cropX)) * pose.scaleX;
  result.pivotY = (float(base.anchorY) - float(crop.cropY)) * pose.scaleY;
  return result;
}

// CZ4 character sprites store positions for an untrimmed canvas. The pixel
// payload is cropped; when the script scales the sprite, its crop offset and
// anchor must scale along with that payload or parts appear displaced.
inline float croppedSpriteOrigin(float position, float anchor, float crop,
                                 float scale, float move, float shake) {
  return position + (crop - anchor) * scale + move + shake;
}
// A calendar or other background-attached HUD element needs an actual opaque
// backing pixel in the current scene. A slot ID alone is insufficient: cropped
// CZ4 art can occupy only a small part of the canvas, leaving black/empty space.
inline bool rasterCoversPointWithOpacity(
    const std::vector<std::uint8_t> &rgba, int width, int height,
    float originX, float originY, float scaleX, float scaleY,
    float pointX, float pointY, float opacity) {
  if (width <= 0 || height <= 0 || scaleX <= 0.0f || scaleY <= 0.0f ||
      !std::isfinite(originX) || !std::isfinite(originY) ||
      !std::isfinite(scaleX) || !std::isfinite(scaleY) ||
      !std::isfinite(pointX) || !std::isfinite(pointY) ||
      !std::isfinite(opacity) ||
      rgba.size() != std::size_t(width) * std::size_t(height) * 4)
    return false;
  const float sx = (pointX - originX) / scaleX;
  const float sy = (pointY - originY) / scaleY;
  if (sx < 0.0f || sy < 0.0f || sx >= width || sy >= height)
    return false;
  const auto ix = std::size_t(sx), iy = std::size_t(sy);
  return (float(rgba[(iy * std::size_t(width) + ix) * 4 + 3]) / 255.0f) *
             opacity >=
         0.85f;
}
// Upscaling a texture with an alpha silhouette may damage cropped windows,
 // hair, masks, and other overlays. Check once at decode time, not every frame.
inline bool hasNonOpaquePixels(const std::vector<std::uint8_t> &rgba) {
  for (std::size_t i = 3; i < rgba.size(); i += 4) {
    if (rgba[i] != 255)
      return true;
  }
  return false;
}
// AIR's face/expression CZ4 images retain the full character canvas but have
// a much smaller cropped payload. Keep the last full portrait underneath.
inline bool isCharacterFragment(int baseWidth, int baseHeight,
                                int baseCanvasWidth, int baseCanvasHeight,
                                int nextWidth, int nextHeight,
                                int nextCanvasWidth, int nextCanvasHeight) {
  if (baseWidth <= 0 || baseHeight <= 0 || nextWidth <= 0 || nextHeight <= 0 ||
      baseCanvasWidth != nextCanvasWidth ||
      baseCanvasHeight != nextCanvasHeight)
    return false;
  const auto baseArea = std::uint64_t(baseWidth) * baseHeight;
  const auto nextArea = std::uint64_t(nextWidth) * nextHeight;
  return nextArea * 2 < baseArea &&
         (nextWidth < baseWidth || nextHeight < baseHeight);
}
inline std::string airCalendarBadgeName(int day) {
  if (day < 1 || day > 31)
    return {};
  const std::string month = day >= 16 ? "jul_" : "aug_";
  return month + (day < 10 ? "0" : "") + std::to_string(day);
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
