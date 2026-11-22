// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <N2System/AnimationTiming.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace N2System {

enum class EntityType {
  Container,
  Sprite,
  Text,
  Choice,
  Color,
  Movie,
  Window,
  ScrollBar
};

struct Entity {
  std::string name;
  std::string alias;
  EntityType type = EntityType::Container;

  // Spatial & visual properties
  uint64_t creationOrder = 0;
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float width = 0.0f, height = 0.0f;
  float anchorX = 0.0f, anchorY = 0.0f;
  bool alignCenterX = false, alignRightX = false;
  bool alignCenterY = false, alignBottomY = false;
  float scaleX = 1.0f, scaleY = 1.0f;
  float angle = 0.0f;
  float opacity = 1.0f;
  float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
  bool visible = true;
  bool locked = false;
  bool smoothing = false;
  bool monochrome = false;
  uint8_t boxBlurPasses = 0; // 0, 8 or 16; prepared once per GPU texture.
  float scrollValue = 50.0f; // 0..100 for ScrollBar entities
  bool scrollHorizontal = true;

  // Clip rectangle for CreateClipTexture / SetMovieClipRect
  bool hasClipRect = false;
  float clipX = 0.0f, clipY = 0.0f, clipW = 0.0f, clipH = 0.0f;

  std::string parent;
  std::vector<std::string> children;

  // Type-specific properties
  std::string imagePath;
  uint32_t solidColor = 0; // ARGB

  std::string moviePath;
  bool movieLoop = false;
  bool moviePlaying = false;

  std::string text;
  std::string speaker;
  int fontSize = 24;
  std::size_t visibleCharCount = 0;

  std::vector<std::string> choiceOptions;
  int selectedIndex = -1;
  std::string focusUp, focusDown, focusLeft, focusRight;

  // Animations (tweening over time in seconds)
  float startX = 0.0f, startY = 0.0f;
  float targetX = 0.0f, targetY = 0.0f;
  float moveDuration = 0.0f, moveElapsed = 0.0f;
  EaseFunction moveEase = EaseFunction::Linear;
  bool isBezierMoving = false;
  float bezierX1 = 0, bezierY1 = 0, bezierX2 = 0, bezierY2 = 0;
  bool isMoving = false;

  float startOpacity = 1.0f;
  float targetOpacity = 1.0f;
  float opacityDuration = 0.0f, opacityElapsed = 0.0f;
  EaseFunction opacityEase = EaseFunction::Linear;
  bool isFading = false;

  float startScaleX = 1.0f, startScaleY = 1.0f;
  float targetScaleX = 1.0f, targetScaleY = 1.0f;
  float scaleDuration = 0.0f, scaleElapsed = 0.0f;
  EaseFunction scaleEase = EaseFunction::Linear;
  bool isScaling = false;

  float startAngle = 0.0f;
  float targetAngle = 0.0f;
  float rotateDuration = 0.0f, rotateElapsed = 0.0f;
  EaseFunction rotateEase = EaseFunction::Linear;
  bool isRotating = false;

  float shakeAmpX = 0.0f, shakeAmpY = 0.0f;
  float shakeDuration = 0.0f, shakeElapsed = 0.0f;
  float shakeOffsetX = 0.0f, shakeOffsetY = 0.0f;
  bool isShaking = false;

  std::string transitionMaskPath;
  float transitionStart = 0.0f, transitionEnd = 1.0f;
  float transitionDuration = 0.0f, transitionElapsed = 0.0f;
  EaseFunction transitionEase = EaseFunction::Linear;
  bool isTransitioning = false;

  bool isAnimating() const {
    return isMoving || isFading || isScaling || isRotating || isShaking ||
           isTransitioning;
  }

  void update(float dt);
};

class SceneGraph {
public:
  SceneGraph() = default;

  Entity *createEntity(std::string_view name,
                       EntityType type = EntityType::Container);
  Entity *findEntity(std::string_view name);
  const Entity *findEntity(std::string_view name) const;
  std::vector<Entity *> queryEntities(std::string_view pattern);
  bool destroyEntity(std::string_view name);
  std::size_t destroyMatching(std::string_view pattern,
                              bool respectLock = true);
  void clear();

  bool setParent(std::string_view childName, std::string_view parentName);
  bool setAlias(std::string_view entityName, std::string_view alias);
  bool setPriority(std::string_view entityName, float z);
  void setMonochrome(std::string_view query, bool enabled);
  void setBoxBlur(std::string_view query, unsigned passes);

  void moveEntity(std::string_view name, float targetX, float targetY,
                  float durationMs = 0.0f,
                  EaseFunction ease = EaseFunction::Linear);
  void bezierMoveEntity(std::string_view name, float control1X, float control1Y,
                        float control2X, float control2Y, float targetX,
                        float targetY, float durationMs,
                        EaseFunction ease = EaseFunction::Linear);
  void fadeEntity(std::string_view name, float targetOpacity,
                  float durationMs = 0.0f,
                  EaseFunction ease = EaseFunction::Linear);
  void zoomEntity(std::string_view name, float targetScaleX, float targetScaleY,
                  float durationMs = 0.0f,
                  EaseFunction ease = EaseFunction::Linear);
  void rotateEntity(std::string_view name, float targetAngle,
                    float durationMs = 0.0f,
                    EaseFunction ease = EaseFunction::Linear);
  void shakeEntity(std::string_view name, float ampX, float ampY,
                   float durationMs = 0.0f);
  void transitionEntity(std::string_view name, float startAlpha, float endAlpha,
                        float durationMs, std::string_view maskPath = "",
                        EaseFunction ease = EaseFunction::Linear);

  void update(float dtSeconds);

  const std::vector<std::string> &renderOrder() const { return renderOrder_; }
  const std::unordered_map<std::string, Entity> &entities() const {
    return entities_;
  }

  static bool wildcardMatch(std::string_view text, std::string_view pattern);

private:
  void rebuildRenderOrder();

  std::unordered_map<std::string, Entity> entities_;
  // NitroSharp World preserves insertion order for wildcard queries.
  // Keep a separate order from z-sorted renderOrder_ to avoid re-sorting
  // every query during script execution.
  std::vector<std::string> insertionOrder_;
  std::vector<std::string> renderOrder_;
  uint64_t nextCreationOrder_ = 1;
};

} // namespace N2System
