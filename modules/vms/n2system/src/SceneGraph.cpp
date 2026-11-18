// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/SceneGraph.hpp>
#include <algorithm>
#include <cmath>

namespace N2System {

void Entity::update(float dt) {
  if (isMoving && moveDuration > 0.0f) {
    moveElapsed += dt;
    float t = std::min(1.0f, moveElapsed / moveDuration);
    x = x + (targetX - x) * t;
    y = y + (targetY - y) * t;
    if (t >= 1.0f) {
      x = targetX;
      y = targetY;
      isMoving = false;
    }
  }

  if (isFading && opacityDuration > 0.0f) {
    opacityElapsed += dt;
    float t = std::min(1.0f, opacityElapsed / opacityDuration);
    opacity = opacity + (targetOpacity - opacity) * t;
    if (t >= 1.0f) {
      opacity = targetOpacity;
      isFading = false;
    }
  }

  if (isScaling && scaleDuration > 0.0f) {
    scaleElapsed += dt;
    float t = std::min(1.0f, scaleElapsed / scaleDuration);
    scaleX = scaleX + (targetScaleX - scaleX) * t;
    scaleY = scaleY + (targetScaleY - scaleY) * t;
    if (t >= 1.0f) {
      scaleX = targetScaleX;
      scaleY = targetScaleY;
      isScaling = false;
    }
  }
}

Entity *SceneGraph::createEntity(std::string_view name, EntityType type) {
  std::string sName(name);
  auto &entity = entities_[sName];
  entity.name = sName;
  entity.type = type;
  rebuildRenderOrder();
  return &entity;
}

Entity *SceneGraph::findEntity(std::string_view name) {
  std::string key(name);
  auto it = entities_.find(key);
  if (it != entities_.end())
    return &it->second;

  // Search by alias
  for (auto &[k, e] : entities_) {
    if (e.alias == key)
      return &e;
  }
  return nullptr;
}

const Entity *SceneGraph::findEntity(std::string_view name) const {
  std::string key(name);
  auto it = entities_.find(key);
  if (it != entities_.end())
    return &it->second;

  for (const auto &[k, e] : entities_) {
    if (e.alias == key)
      return &e;
  }
  return nullptr;
}

bool SceneGraph::destroyEntity(std::string_view name) {
  Entity *e = findEntity(name);
  if (!e)
    return false;
  std::string actualName = e->name;

  // Remove from parent
  if (!e->parent.empty()) {
    auto *p = findEntity(e->parent);
    if (p) {
      p->children.erase(
          std::remove(p->children.begin(), p->children.end(), actualName),
          p->children.end());
    }
  }

  // Remove children or unparent them
  for (const auto &childName : e->children) {
    auto *c = findEntity(childName);
    if (c)
      c->parent.clear();
  }

  entities_.erase(actualName);
  rebuildRenderOrder();
  return true;
}

void SceneGraph::clear() {
  entities_.clear();
  renderOrder_.clear();
}

bool SceneGraph::setParent(std::string_view childName,
                           std::string_view parentName) {
  auto *child = findEntity(childName);
  if (!child)
    return false;

  if (parentName.empty()) {
    if (!child->parent.empty()) {
      auto *oldParent = findEntity(child->parent);
      if (oldParent) {
        oldParent->children.erase(std::remove(oldParent->children.begin(),
                                              oldParent->children.end(),
                                              child->name),
                                  oldParent->children.end());
      }
    }
    child->parent.clear();
    return true;
  }

  auto *parent = findEntity(parentName);
  if (!parent)
    return false;

  child->parent = parent->name;
  if (std::find(parent->children.begin(), parent->children.end(),
                child->name) == parent->children.end())
    parent->children.push_back(child->name);

  rebuildRenderOrder();
  return true;
}

bool SceneGraph::setAlias(std::string_view entityName, std::string_view alias) {
  auto *e = findEntity(entityName);
  if (!e)
    return false;
  e->alias = std::string(alias);
  return true;
}

void SceneGraph::moveEntity(std::string_view name, float targetX, float targetY,
                            float durationMs) {
  auto *e = findEntity(name);
  if (!e)
    return;
  if (durationMs <= 0.0f) {
    e->x = targetX;
    e->y = targetY;
    e->isMoving = false;
  } else {
    e->targetX = targetX;
    e->targetY = targetY;
    e->moveDuration = durationMs / 1000.0f;
    e->moveElapsed = 0.0f;
    e->isMoving = true;
  }
}

void SceneGraph::fadeEntity(std::string_view name, float targetOpacity,
                            float durationMs) {
  auto *e = findEntity(name);
  if (!e)
    return;
  if (durationMs <= 0.0f) {
    e->opacity = targetOpacity;
    e->isFading = false;
  } else {
    e->targetOpacity = targetOpacity;
    e->opacityDuration = durationMs / 1000.0f;
    e->opacityElapsed = 0.0f;
    e->isFading = true;
  }
}

void SceneGraph::zoomEntity(std::string_view name, float targetScaleX,
                            float targetScaleY, float durationMs) {
  auto *e = findEntity(name);
  if (!e)
    return;
  if (durationMs <= 0.0f) {
    e->scaleX = targetScaleX;
    e->scaleY = targetScaleY;
    e->isScaling = false;
  } else {
    e->targetScaleX = targetScaleX;
    e->targetScaleY = targetScaleY;
    e->scaleDuration = durationMs / 1000.0f;
    e->scaleElapsed = 0.0f;
    e->isScaling = true;
  }
}

void SceneGraph::update(float dtSeconds) {
  for (auto &[name, entity] : entities_) {
    entity.update(dtSeconds);
  }
}

void SceneGraph::rebuildRenderOrder() {
  renderOrder_.clear();
  renderOrder_.reserve(entities_.size());
  for (const auto &[name, entity] : entities_) {
    renderOrder_.push_back(name);
  }

  std::sort(renderOrder_.begin(), renderOrder_.end(),
            [this](const std::string &a, const std::string &b) {
              const auto *ea = findEntity(a);
              const auto *eb = findEntity(b);
              if (!ea || !eb)
                return false;
              if (ea->z != eb->z)
                return ea->z < eb->z;
              return ea->name < eb->name;
            });
}

} // namespace N2System
