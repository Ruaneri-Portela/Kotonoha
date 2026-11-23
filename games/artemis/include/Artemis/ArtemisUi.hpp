// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Artemis/ArtemisAssets.hpp>
#include <Artemis/AstParser.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Artemis {

// One entry of the game's own UI layout table (`system/table/list_windows_*.tbl`).
// Coordinates are native Artemis stage coordinates (system.ini: 1600x900).
struct UiItem {
  std::string name;
  std::string logicalFile;
  std::string p1;
  std::string p2;
  SDL_FRect rect{};
  SDL_FRect clip{};
  SDL_FRect overClip{};
  SDL_FRect activeClip{};
  bool hasClip = false;
  bool hasOverClip = false;
  bool hasActiveClip = false;
  bool isButton = false;
};

// Reads the shipped Artemis UI definition instead of letting the engine draw
// generic placeholders. All pictures are still loaded through AssetProvider's
// public API, so overlays and texture caching keep working.
class ArtemisUi {
public:
  explicit ArtemisUi(ArtemisAssets &assets);

  bool load();
  bool loaded() const { return root_ != nullptr; }
  const std::string &tablePath() const { return tablePath_; }
  const std::string &language() const { return language_; }

  const std::vector<UiItem> &titleItems() const { return titleItems_; }
  const std::vector<UiItem> &titleButtons() const { return titleButtons_; }
  std::optional<UiItem> titleItem(const std::string &name) const;
  std::optional<UiItem> advItem(const std::string &name) const;
  // Searches every group of the localized and base UI tables (the base table
  // owns shared widgets such as the choice button artwork).
  std::optional<UiItem> findItem(const std::string &name) const;

  // Resolves the table's logical file ID to the canonical PFS asset ID.
  std::string resolveImage(const UiItem &item) const;
  std::string resolveImage(const std::string &logicalFile) const;
  SDL_Texture &texture(Kotonoha::RendererRef renderer,
                       const std::string &logicalFile);

  static SDL_Texture &draw(Kotonoha::RendererRef renderer, SDL_Texture &source,
                           const SDL_FRect &destination,
                           const SDL_FRect *sourceRect = nullptr,
                           Uint8 alpha = 255);

private:
  const LuaTable *group(const std::string &name) const;
  std::optional<UiItem> makeItem(const std::string &name,
                                 const LuaValue &value) const;

  ArtemisAssets &assets_;
  std::shared_ptr<LuaTable> root_;
  std::shared_ptr<LuaTable> baseRoot_;
  std::string tablePath_;
  std::string language_;
  std::vector<UiItem> titleItems_;
  std::vector<UiItem> titleButtons_;
};

} // namespace Artemis
