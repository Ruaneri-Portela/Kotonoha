// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/SceneGraph.hpp>
#include <N2System/EntityQuery.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace N2System {

void Entity::update(float dt) {
  if (isMoving && moveDuration > 0.0f) {
    moveElapsed += dt;
    const float t = easeFactor(moveElapsed / moveDuration, moveEase);
    if (isBezierMoving) {
      const float inv = 1.0f - t;
      x = inv * inv * inv * startX + 3.0f * inv * inv * t * bezierX1 +
          3.0f * inv * t * t * bezierX2 + t * t * t * targetX;
      y = inv * inv * inv * startY + 3.0f * inv * inv * t * bezierY1 +
          3.0f * inv * t * t * bezierY2 + t * t * t * targetY;
    } else {
      x = startX + (targetX - startX) * t;
      y = startY + (targetY - startY) * t;
    }
    if (t >= 1.0f) {
      x = targetX;
      y = targetY;
      isMoving = false;
      isBezierMoving = false;
    }
  }

  if (isFading && opacityDuration > 0.0f) {
    opacityElapsed += dt;
    const float t = easeFactor(opacityElapsed / opacityDuration, opacityEase);
    opacity = startOpacity + (targetOpacity - startOpacity) * t;
    if (t >= 1.0f) {
      opacity = targetOpacity;
      isFading = false;
    }
  }

  if (isScaling && scaleDuration > 0.0f) {
    scaleElapsed += dt;
    const float t = easeFactor(scaleElapsed / scaleDuration, scaleEase);
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
    const float t = easeFactor(rotateElapsed / rotateDuration, rotateEase);
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
    const float t = easeFactor(transitionElapsed / transitionDuration, transitionEase);
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
  const std::string sName(name);
  // NitroSharp.World.AddEntity replaces an existing child with a fresh
  // instance. Reusing the old object kept stale opacity, transforms, aliases
  // and animation clocks after CreateTexture/CreateText/CreateMovie.
  if (entities_.find(sName) != entities_.end())
    destroyEntity(sName);
  auto &entity = entities_[sName];
  entity.name = sName;
  entity.type = type;
  entity.creationOrder = nextCreationOrder_++;
  insertionOrder_.push_back(sName);
  rebuildRenderOrder();
  return &entity;
}

Entity *SceneGraph::findEntity(std::string_view name) {
  // The const implementation performs all reads, so direct callers and
  // renderer access use identical and deterministic path resolution.
  return const_cast<Entity *>(
      static_cast<const SceneGraph *>(this)->findEntity(name));
}

const Entity *SceneGraph::findEntity(std::string_view name) const {
  if (name.empty())
    return nullptr;
  const std::string key(name);
  // Keep the exact identifiers used by the legacy NSB port, including
  // slash-qualified objects that were created before their parents.
  if (name.front() != '@') {
    const auto exact = entities_.find(key);
    if (exact != entities_.end())
      return &exact->second;
  }

  const auto byAlias = [this](std::string_view alias) -> const Entity * {
    const Entity *best = nullptr;
    for (const auto &[key, entity] : entities_) {
      if (entity.alias == alias &&
          (!best || entity.creationOrder < best->creationOrder))
        best = &entity;
    }
    return best;
  };
  if (name.front() == '@' && name.find('/') == std::string_view::npos) {
    if (auto *entity = byAlias(name.substr(1)))
      return entity;
    auto legacy = entities_.find(std::string(name.substr(1)));
    return legacy == entities_.end() ? nullptr : &legacy->second;
  }

  // NitroSharp EntityPath supports exact components and per-part @aliases;
  // unlike a query, it cannot contain "*" or scopes "<" and "<@".
  const auto path = EntityPath::tryParse(name);
  if (path && path->parts().size() > 1) {
    const Entity *parent = nullptr;
    for (const auto &part : path->parts()) {
      const Entity *unique = nullptr;
      for (const auto &[key, entity] : entities_) {
        bool matches = false;
        if (part.isAlias) {
          matches = entity.alias == part.value;
        } else {
          const auto slash = entity.name.rfind('/');
          const std::string_view local =
              slash == std::string::npos
                  ? std::string_view(entity.name)
                  : std::string_view(entity.name).substr(slash + 1);
          const std::string_view entityParent =
              !entity.parent.empty()
                  ? std::string_view(entity.parent)
                  : (slash == std::string::npos
                         ? std::string_view{}
                         : std::string_view(entity.name).substr(0, slash));
          matches = local == part.value &&
                    entityParent ==
                        (parent ? std::string_view(parent->name)
                                : std::string_view{});
        }
        if (matches) {
          // A path is a single entity, not a multi-target query. NitroSharp
          // rejects ambiguous intermediate path components.
          if (unique)
            return nullptr;
          unique = &entity;
        }
      }
      if (!unique)
        return nullptr;
      parent = unique;
    }
    return parent;
  }

  // Older native scripts occasionally address a registered alias without
  // '@'. Preserve that lookup only for a single unqualified identifier.
  if (name.find('/') == std::string_view::npos)
    return byAlias(name);
  return nullptr;
}

std::vector<Entity *> SceneGraph::queryEntities(std::string_view expression) {
  const auto parsed = EntityQuery::tryParse(expression);
  if (!parsed)
    return {};

  // Preserve direct absolute entity identifiers used by existing NSB ports
  // (some assets create "parent/child" before creating the parent entity).
  // Scoped queries and wildcard queries still obey NitroSharp's grammar.
  bool plainPath = true;
  for (const auto &part : parsed->parts()) {
    if (part.scope != EntityQueryScope::Current || part.hasWildcard())
      plainPath = false;
  }
  if (plainPath) {
    auto exact = entities_.find(std::string(expression));
    if (exact != entities_.end())
      return {&exact->second};
    // Compatibility: older native scripts sometimes use a bare alias with
    // no '@'. NitroSharp requires '@', but retain this exact-name fallback.
    if (parsed->parts().size() == 1 && expression.find('>') ==
                                                std::string_view::npos)
      if (auto *legacy = findEntity(expression))
        return {legacy};
  }

  // Keep query order deterministic: entity creation order, not hash-map order.
  // NitroSharp's World.Query enumerates children in their insertion order.
  std::vector<Entity *> ordered;
  ordered.reserve(insertionOrder_.size());
  for (const auto &name : insertionOrder_) {
    const auto it = entities_.find(name);
    if (it != entities_.end())
      ordered.push_back(&it->second);
  }

  // Support both explicit parent links and legacy absolute slash-delimited
  // names. A wildcard matches one component; it never consumes a slash.
  const auto parentName = [](const Entity &entity) -> std::string_view {
    if (!entity.parent.empty())
      return entity.parent;
    const auto slash = entity.name.rfind('/');
    if (slash != std::string::npos)
      return std::string_view(entity.name).substr(0, slash);
    return {};
  };
  const auto localName = [](const Entity &entity) -> std::string_view {
    const auto slash = entity.name.rfind('/');
    if (slash == std::string::npos)
      return entity.name;
    return std::string_view(entity.name).substr(slash + 1);
  };
  // Some existing compiled NSB scripts create slash-qualified sprites
  // without materializing intermediate container entities. Keep these names
  // queryable, but match '*'' within each slash-separated component only.
  const auto legacyQualifiedMatches = [&]() {
    std::vector<Entity *> result;
    const auto &parts = parsed->parts();
    if (parts.size() < 2)
      return result;
    for (const auto &part : parts)
      if (part.scope != EntityQueryScope::Current)
        return result;
    for (auto *candidate : ordered) {
      auto remaining = std::string_view(candidate->name);
      bool matched = true;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto slash = remaining.find('/');
        const auto component =
            slash == std::string_view::npos ? remaining
                                            : remaining.substr(0, slash);
        if (!EntityQuery::matchPattern(component, parts[i].pattern) ||
            (i + 1 == parts.size()) !=
                (slash == std::string_view::npos)) {
          matched = false;
          break;
        }
        if (slash != std::string_view::npos)
          remaining.remove_prefix(slash + 1);
      }
      if (matched)
        result.push_back(candidate);
    }
    return result;
  };
  std::vector<Entity *> current;
  for (const auto &part : parsed->parts()) {
    std::vector<Entity *> next;
    const bool aliasScope = part.scope == EntityQueryScope::CurrentAliases ||
                            part.scope == EntityQueryScope::AllAliases;
    const bool rootScope = current.empty() ||
                           part.scope == EntityQueryScope::MainThread;
    // Querying a nested current scope after a failed match must stay empty.
    if (!aliasScope && !rootScope && current.empty())
      break;
    const auto add = [&next](Entity *candidate) {
      if (std::find(next.begin(), next.end(), candidate) == next.end())
        next.push_back(candidate);
    };
    if (aliasScope) {
      for (auto *candidate : ordered)
        if (!candidate->alias.empty() &&
            EntityQuery::matchPattern(candidate->alias, part.pattern))
          add(candidate);
    } else if (rootScope) {
      for (auto *candidate : ordered)
        if (parentName(*candidate).empty() &&
            EntityQuery::matchPattern(localName(*candidate), part.pattern))
          add(candidate);
    } else {
      for (auto *parent : current) {
        for (auto *candidate : ordered)
          if (parentName(*candidate) == parent->name &&
              EntityQuery::matchPattern(localName(*candidate), part.pattern))
            add(candidate);
      }
    }
    current = std::move(next);
    if (current.empty())
      return legacyQualifiedMatches();
  }
  return current;
}

bool SceneGraph::destroyEntity(std::string_view name) {
  Entity *root = findEntity(name);
  if (!root)
    return false;

  // NitroSharp.World.CommitDestroyEntity recursively disposes children.
  // Preserve the same lifecycle for both explicit parent links and the
  // legacy slash-qualified names stored in the Kotonoha scene graph.
  std::vector<std::string> removeNames{root->name};
  for (std::size_t i = 0; i < removeNames.size(); ++i) {
    const std::string ancestor = removeNames[i];
    for (const auto &[key, entity] : entities_) {
      const bool explicitChild = entity.parent == ancestor;
      const bool implicitChild =
          entity.parent.empty() && entity.name.size() > ancestor.size() &&
          entity.name.compare(0, ancestor.size(), ancestor) == 0 &&
          entity.name[ancestor.size()] == '/';
      if ((explicitChild || implicitChild) &&
          std::find(removeNames.begin(), removeNames.end(), key) ==
              removeNames.end())
        removeNames.push_back(key);
    }
  }

  // Drop links held by surviving parents before deleting the descendants.
  for (const auto &deletedName : removeNames) {
    auto it = entities_.find(deletedName);
    if (it == entities_.end() || it->second.parent.empty())
      continue;
    if (auto *parent = findEntity(it->second.parent)) {
      parent->children.erase(
          std::remove(parent->children.begin(), parent->children.end(),
                      deletedName),
          parent->children.end());
    }
  }
  for (const auto &deletedName : removeNames) {
    entities_.erase(deletedName);
    insertionOrder_.erase(
        std::remove(insertionOrder_.begin(), insertionOrder_.end(),
                    deletedName),
        insertionOrder_.end());
  }
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
  insertionOrder_.clear();
  renderOrder_.clear();
  nextCreationOrder_ = 1;
}

bool SceneGraph::setParent(std::string_view childName,
                           std::string_view parentName) {
  Entity *child = findEntity(childName);
  if (!child)
    return false;
  Entity *parent = parentName.empty() ? nullptr : findEntity(parentName);
  if (!parentName.empty() && !parent)
    return false;
  // NitroSharp's world is a tree. A reparent must not make an entity its
  // own ancestor, or traversal and recursive deletion would loop forever.
  std::size_t depth = 0;
  for (const Entity *ancestor = parent; ancestor;) {
    if (ancestor == child || ++depth > entities_.size())
      return false;
    if (ancestor->parent.empty())
      break;
    ancestor = findEntity(ancestor->parent);
  }
  if (child->parent == (parent ? parent->name : std::string{}))
    return true;
  if (!child->parent.empty()) {
    if (auto *oldParent = findEntity(child->parent)) {
      oldParent->children.erase(
          std::remove(oldParent->children.begin(), oldParent->children.end(),
                      child->name),
          oldParent->children.end());
    }
  }
  child->parent = parent ? parent->name : std::string{};
  if (parent && std::find(parent->children.begin(), parent->children.end(),
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
  // Non-finite NSB priorities cannot participate in a strict ordering.
  // The caller may have directly assigned e->z before asking to reorder.
  if (!std::isfinite(z)) {
    if (!std::isfinite(e->z)) {
      e->z = 0.0f;
      rebuildRenderOrder();
    }
    return false;
  }
  e->z = z;
  rebuildRenderOrder();
  return true;
}

void SceneGraph::setMonochrome(std::string_view query, bool enabled) {
  for (auto *e : queryEntities(query))
    e->monochrome = enabled;
}

void SceneGraph::setBoxBlur(std::string_view query, unsigned passes) {
  const uint8_t normalized = passes >= 16 ? 16 : passes >= 8 ? 8 : 0;
  for (auto *e : queryEntities(query))
    e->boxBlurPasses = normalized;
}

void SceneGraph::moveEntity(std::string_view name, float targetX, float targetY,
                            float durationMs, EaseFunction ease) {
  for (auto *e : queryEntities(name)) {
    e->isBezierMoving = false;
    e->moveEase = ease;
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

void SceneGraph::bezierMoveEntity(std::string_view name, float control1X,
                                  float control1Y, float control2X,
                                  float control2Y, float targetX, float targetY,
                                  float durationMs, EaseFunction ease) {
  for (auto *e : queryEntities(name)) {
    e->moveEase = ease;
    e->bezierX1 = control1X;
    e->bezierY1 = control1Y;
    e->bezierX2 = control2X;
    e->bezierY2 = control2Y;
    if (durationMs <= 0.0f) {
      e->x = targetX;
      e->y = targetY;
      e->isMoving = false;
      e->isBezierMoving = false;
    } else {
      e->startX = e->x;
      e->startY = e->y;
      e->targetX = targetX;
      e->targetY = targetY;
      e->moveDuration = durationMs / 1000.0f;
      e->moveElapsed = 0.0f;
      e->isMoving = true;
      e->isBezierMoving = true;
    }
  }
}

void SceneGraph::fadeEntity(std::string_view name, float targetOpacity,
                            float durationMs, EaseFunction ease) {
  for (auto *e : queryEntities(name)) {
    e->opacityEase = ease;
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
                            float targetScaleY, float durationMs,
                            EaseFunction ease) {
  for (auto *e : queryEntities(name)) {
    e->scaleEase = ease;
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
                              float durationMs, EaseFunction ease) {
  for (auto *e : queryEntities(name)) {
    e->rotateEase = ease;
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
                                  std::string_view maskPath,
                                  EaseFunction ease) {
  for (auto *e : queryEntities(name)) {
    e->transitionEase = ease;
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

  // All keys originate in entities_. Do not re-resolve aliases or paths
  // while sorting. Normalize malformed/legacy NaNs to keep the comparison
  // transitive (std::sort requires a strict weak ordering).
  std::sort(renderOrder_.begin(), renderOrder_.end(),
            [this](const std::string &a, const std::string &b) {
              const Entity &ea = entities_.at(a);
              const Entity &eb = entities_.at(b);
              const float za = std::isfinite(ea.z) ? ea.z : 0.0f;
              const float zb = std::isfinite(eb.z) ? eb.z : 0.0f;
              if (za != zb)
                return za < zb;
              if (ea.creationOrder != eb.creationOrder)
                return ea.creationOrder < eb.creationOrder;
              return a < b;
            });
}

} // namespace N2System
