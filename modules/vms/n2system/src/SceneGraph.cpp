// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/SceneGraph.hpp>
#include <algorithm>
#include <cmath>

namespace N2System {

void Entity::update(float dt) {
  if (isMoving && moveDuration > 0.0f) {
    moveElapsed += dt;
    float t = std::min(1.0f, moveElapsed / moveDuration);
    x = startX + (targetX - startX) * t;
    y = startY + (targetY - startY) * t;
    if (t >= 1.0f) {
      x = targetX;
      y = targetY;
      isMoving = false;
    }
  }

  if (isFading && opacityDuration > 0.0f) {
    opacityElapsed += dt;
    float t = std::min(1.0f, opacityElapsed / opacityDuration);
    opacity = startOpacity + (targetOpacity - startOpacity) * t;
    if (t >= 1.0f) {
      opacity = targetOpacity;
      isFading = false;
    }
  }

  if (isScaling && scaleDuration > 0.0f) {
    scaleElapsed += dt;
    float t = std::min(1.0f, scaleElapsed / scaleDuration);
    scaleX = startScaleX + (targetScaleX - startScaleX) * t;
    scaleY = startScaleY + (targetScaleY - startScaleY) * t;
    if (t >= 1.0f) {
      scaleX = targetScaleX;
      scaleY = targetScaleY;
      isScaling = false;
    }
  }

  if (isRotating && rotateDuration > 0.0f) {
    rotateElapsed += dt;
    float t = std::min(1.0f, rotateElapsed / rotateDuration);
    angle = startAngle + (targetAngle - startAngle) * t;
    if (t >= 1.0f) {
      angle = targetAngle;
      isRotating = false;
    }
  }

  if (isShaking && shakeDuration > 0.0f) {
    shakeElapsed += dt;
    if (shakeElapsed >= shakeDuration) {
      shakeOffsetX = 0.0f;
      shakeOffsetY = 0.0f;
      isShaking = false;
    } else {
      const float phase = shakeElapsed * 45.0f;
      const float decay = 1.0f - (shakeElapsed / shakeDuration);
      shakeOffsetX = std::sin(phase) * shakeAmpX * decay;
      shakeOffsetY = std::cos(phase * 1.3f) * shakeAmpY * decay;
    }
  }

  if (isTransitioning && transitionDuration > 0.0f) {
    transitionElapsed += dt;
    float t = std::min(1.0f, transitionElapsed / transitionDuration);
    opacity = transitionStart + (transitionEnd - transitionStart) * t;
    if (t >= 1.0f) {
      opacity = transitionEnd;
      isTransitioning = false;
    }
  }
}

bool SceneGraph::wildcardMatch(std::string_view text,
                               std::string_view pattern) {
  std::size_t ti = 0, pi = 0;
  std::size_t starIdx = std::string_view::npos;
  std::size_t matchIdx = 0;
  while (ti < text.size()) {
    if (pi < pattern.size() &&
        (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++ti;
      ++pi;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      starIdx = pi++;
      matchIdx = ti;
    } else if (starIdx != std::string_view::npos) {
      pi = starIdx + 1;
      ti = ++matchIdx;
    } else {
      return false;
    }
  }
  while (pi < pattern.size() && pattern[pi] == '*')
    ++pi;
  return pi == pattern.size();
}

Entity *SceneGraph::createEntity(std::string_view name, EntityType type) {
  std::string sName(name);
  auto &entity = entities_[sName];
  entity.name = sName;
  entity.type = type;
  if (entity.creationOrder == 0)
    entity.creationOrder = nextCreationOrder_++;
  rebuildRenderOrder();
  return &entity;
}

Entity *SceneGraph::findEntity(std::string_view name) {
  if (name.empty())
    return nullptr;
  std::string key(name);
  if (key.front() == '@') {
    const std::string_view aliasQuery(key.data() + 1, key.size() - 1);
    for (auto &[k, e] : entities_) {
      if (!e.alias.empty() && e.alias == aliasQuery)
        return &e;
    }
    auto it = entities_.find(std::string(aliasQuery));
    if (it != entities_.end())
      return &it->second;
    return nullptr;
  }

  auto it = entities_.find(key);
  if (it != entities_.end())
    return &it->second;

  // Search by alias
  for (auto &[k, e] : entities_) {
    if (!e.alias.empty() && e.alias == key)
      return &e;
  }
  return nullptr;
}

const Entity *SceneGraph::findEntity(std::string_view name) const {
  if (name.empty())
    return nullptr;
  std::string key(name);
  if (key.front() == '@') {
    const std::string_view aliasQuery(key.data() + 1, key.size() - 1);
    for (const auto &[k, e] : entities_) {
      if (!e.alias.empty() && e.alias == aliasQuery)
        return &e;
    }
    auto it = entities_.find(std::string(aliasQuery));
    if (it != entities_.end())
      return &it->second;
    return nullptr;
  }

  auto it = entities_.find(key);
  if (it != entities_.end())
    return &it->second;

  for (const auto &[k, e] : entities_) {
    if (!e.alias.empty() && e.alias == key)
      return &e;
  }
  return nullptr;
}

std::vector<Entity *> SceneGraph::queryEntities(std::string_view pattern) {
  std::vector<Entity *> matches;
  if (pattern.empty())
    return matches;

  const bool aliasMode = pattern.front() == '@';
  const std::string_view body = aliasMode ? pattern.substr(1) : pattern;
  const bool hasWildcard = body.find('*') != std::string_view::npos ||
                           body.find('?') != std::string_view::npos;

  if (!hasWildcard) {
    if (auto *e = findEntity(pattern))
      matches.push_back(e);
    return matches;
  }

  for (auto &[k, e] : entities_) {
    if (aliasMode) {
      if (!e.alias.empty() && wildcardMatch(e.alias, body))
        matches.push_back(&e);
    } else {
      if (wildcardMatch(e.name, body) ||
          (!e.alias.empty() && wildcardMatch(e.alias, body))) {
        matches.push_back(&e);
      }
    }
  }
  return matches;
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

std::size_t SceneGraph::destroyMatching(std::string_view pattern,
                                        bool respectLock) {
  auto matches = queryEntities(pattern);
  std::vector<std::string> toDelete;
  toDelete.reserve(matches.size());
  for (auto *e : matches) {
    if (respectLock && e->locked)
      continue;
    toDelete.push_back(e->name);
  }
  for (const auto &name : toDelete)
    destroyEntity(name);
  return toDelete.size();
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

bool SceneGraph::setPriority(std::string_view entityName, float z) {
  auto *e = findEntity(entityName);
  if (!e)
    return false;
  e->z = z;
  rebuildRenderOrder();
  return true;
}

void SceneGraph::moveEntity(std::string_view name, float targetX, float targetY,
                            float durationMs) {
  for (auto *e : queryEntities(name)) {
    if (durationMs <= 0.0f) {
      e->x = targetX;
      e->y = targetY;
      e->isMoving = false;
    } else {
      e->startX = e->x;
      e->startY = e->y;
      e->targetX = targetX;
      e->targetY = targetY;
      e->moveDuration = durationMs / 1000.0f;
      e->moveElapsed = 0.0f;
      e->isMoving = true;
    }
  }
}

void SceneGraph::fadeEntity(std::string_view name, float targetOpacity,
                            float durationMs) {
  for (auto *e : queryEntities(name)) {
    if (durationMs <= 0.0f) {
      e->opacity = targetOpacity;
      e->isFading = false;
    } else {
      e->startOpacity = e->opacity;
      e->targetOpacity = targetOpacity;
      e->opacityDuration = durationMs / 1000.0f;
      e->opacityElapsed = 0.0f;
      e->isFading = true;
    }
  }
}

void SceneGraph::zoomEntity(std::string_view name, float targetScaleX,
                            float targetScaleY, float durationMs) {
  for (auto *e : queryEntities(name)) {
    if (durationMs <= 0.0f) {
      e->scaleX = targetScaleX;
      e->scaleY = targetScaleY;
      e->isScaling = false;
    } else {
      e->startScaleX = e->scaleX;
      e->startScaleY = e->scaleY;
      e->targetScaleX = targetScaleX;
      e->targetScaleY = targetScaleY;
      e->scaleDuration = durationMs / 1000.0f;
      e->scaleElapsed = 0.0f;
      e->isScaling = true;
    }
  }
}

void SceneGraph::rotateEntity(std::string_view name, float targetAngle,
                              float durationMs) {
  for (auto *e : queryEntities(name)) {
    if (durationMs <= 0.0f) {
      e->angle = targetAngle;
      e->isRotating = false;
    } else {
      e->startAngle = e->angle;
      e->targetAngle = targetAngle;
      e->rotateDuration = durationMs / 1000.0f;
      e->rotateElapsed = 0.0f;
      e->isRotating = true;
    }
  }
}

void SceneGraph::shakeEntity(std::string_view name, float ampX, float ampY,
                             float durationMs) {
  for (auto *e : queryEntities(name)) {
    if (durationMs <= 0.0f) {
      e->shakeOffsetX = 0.0f;
      e->shakeOffsetY = 0.0f;
      e->isShaking = false;
    } else {
      e->shakeAmpX = ampX;
      e->shakeAmpY = ampY;
      e->shakeDuration = durationMs / 1000.0f;
      e->shakeElapsed = 0.0f;
      e->isShaking = true;
    }
  }
}

void SceneGraph::transitionEntity(std::string_view name, float startAlpha,
                                  float endAlpha, float durationMs,
                                  std::string_view maskPath) {
  for (auto *e : queryEntities(name)) {
    e->transitionMaskPath = std::string(maskPath);
    if (durationMs <= 0.0f) {
      e->opacity = endAlpha;
      e->isTransitioning = false;
    } else {
      e->transitionStart = startAlpha;
      e->transitionEnd = endAlpha;
      e->opacity = startAlpha;
      e->transitionDuration = durationMs / 1000.0f;
      e->transitionElapsed = 0.0f;
      e->isTransitioning = true;
    }
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
              if (ea->creationOrder != eb->creationOrder)
                return ea->creationOrder < eb->creationOrder;
              return ea->name < eb->name;
            });
}

} // namespace N2System
