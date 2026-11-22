// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <N2System/SceneGraph.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

namespace N2System {

// A compiled NSB scene can supply its own dialogue window and/or text. The
// generic host overlay is only a fallback; painting its background over the
// script's window produces two overlapping "VN textbox" textures.
struct DialoguePresentation {
  bool nativeFrame = false;
  bool nativeText = false;
};

inline bool isDialogueFrameName(std::string_view value) {
  std::string lower(value);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (const char *hint : {"textwindow", "textbox", "messagewindow",
                           "messagebox", "msgwin", "meswin", "dialogue",
                           "dialogbox", "serifwindow", "talkwindow"}) {
    if (lower.find(hint) != std::string::npos)
      return true;
  }
  return false;
}

// ImageSizeQuery: bool(const Entity&, float& width, float& height).
// Separate from the asset provider so it is testable without retail files.
template <class ImageSizeQuery>
DialoguePresentation inspectDialoguePresentation(const SceneGraph &graph,
                                                  float viewportW,
                                                  float viewportH,
                                                  std::string_view visibleText,
                                                  std::string_view fullText,
                                                  ImageSizeQuery &&imageSize) {
  DialoguePresentation result;
  if (!(viewportW > 0.0f && viewportH > 0.0f))
    return result;

  for (const std::string &id : graph.renderOrder()) {
    const Entity *entity = graph.findEntity(id);
    if (!entity || !entity->visible || !(entity->opacity > 0.01f))
      continue;
    if (entity->type == EntityType::Text) {
      if (!entity->text.empty() &&
          (entity->text == visibleText || entity->text == fullText))
        result.nativeText = true;
      continue;
    }
    if (entity->type != EntityType::Sprite &&
        entity->type != EntityType::Window &&
        entity->type != EntityType::Color)
      continue;
    float width = entity->hasClipRect && entity->clipW > 0.0f
                      ? entity->clipW : entity->width;
    float height = entity->hasClipRect && entity->clipH > 0.0f
                       ? entity->clipH : entity->height;
    if ((width <= 0.0f || height <= 0.0f) &&
        entity->type == EntityType::Sprite)
      imageSize(*entity, width, height);
    width *= std::abs(entity->scaleX);
    height *= std::abs(entity->scaleY);

    const bool nearBottom = entity->y >= viewportH * 0.48f &&
                            entity->y < viewportH;
    const bool panelShape = std::isfinite(width) && std::isfinite(height) &&
                            width >= viewportW * 0.4f &&
                            height >= viewportH * 0.05f &&
                            height <= viewportH * 0.5f;
    const bool explicitDialogueFrame =
        isDialogueFrameName(entity->name) ||
        isDialogueFrameName(entity->imagePath);
    // Strong names permit full-screen UI atlases. Generic window/colour
    // rectangles require an actual bottom-of-screen panel geometry.
    if (explicitDialogueFrame &&
        (panelShape || nearBottom || entity->type == EntityType::Window))
      result.nativeFrame = true;
    else if (panelShape && nearBottom &&
             (entity->type == EntityType::Window ||
              entity->type == EntityType::Color))
      result.nativeFrame = true;
    else if (panelShape && nearBottom &&
             entity->type == EntityType::Sprite &&
             (entity->imagePath.find("system/") != std::string::npos ||
              entity->imagePath.find("ui/") != std::string::npos))
      result.nativeFrame = true;
  }
  return result;
}

} // namespace N2System
