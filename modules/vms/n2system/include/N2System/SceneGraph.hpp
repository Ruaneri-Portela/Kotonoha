// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

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
  Choice
};

struct Entity {
  std::string name;
  std::string alias;
  EntityType type = EntityType::Container;

  // Spatial & visual properties
  float x = 0.0f, y = 0.0f, z = 0.0f;
  float anchorX = 0.0f, anchorY = 0.0f;
  float scaleX = 1.0f, scaleY = 1.0f;
  float angle = 0.0f;
  float opacity = 1.0f;
  float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
  bool visible = true;

  std::string parent;
  std::vector<std::string> children;

  // Type-specific properties
  std::string imagePath;
  uint32_t solidColor = 0; // ARGB

  std::string text;
  std::string speaker;
  int fontSize = 24;
  std::size_t visibleCharCount = 0;

  std::vector<std::string> choiceOptions;
  int selectedIndex = -1;

  // Animations (tweening over time in seconds)
  float targetX = 0.0f, targetY = 0.0f;
  float moveDuration = 0.0f, moveElapsed = 0.0f;
  bool isMoving = false;

  float targetOpacity = 1.0f;
  float opacityDuration = 0.0f, opacityElapsed = 0.0f;
  bool isFading = false;

  float targetScaleX = 1.0f, targetScaleY = 1.0f;
  float scaleDuration = 0.0f, scaleElapsed = 0.0f;
  bool isScaling = false;

  void update(float dt);
};

class SceneGraph {
public:
  SceneGraph() = default;

  Entity *createEntity(std::string_view name, EntityType type = EntityType::Container);
  Entity *findEntity(std::string_view name);
  const Entity *findEntity(std::string_view name) const;
  bool destroyEntity(std::string_view name);
  void clear();

  bool setParent(std::string_view childName, std::string_view parentName);
  bool setAlias(std::string_view entityName, std::string_view alias);

  void moveEntity(std::string_view name, float targetX, float targetY, float durationMs = 0.0f);
  void fadeEntity(std::string_view name, float targetOpacity, float durationMs = 0.0f);
  void zoomEntity(std::string_view name, float targetScaleX, float targetScaleY, float durationMs = 0.0f);

  void update(float dtSeconds);

  const std::vector<std::string> &renderOrder() const { return renderOrder_; }
  const std::unordered_map<std::string, Entity> &entities() const { return entities_; }

private:
  void rebuildRenderOrder();

  std::unordered_map<std::string, Entity> entities_;
  std::vector<std::string> renderOrder_;
};

} // namespace N2System
