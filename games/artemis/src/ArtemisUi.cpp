// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisUi.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

namespace Artemis {
namespace {

bool parseRect(std::string_view text, SDL_FRect &out) {
  if (text.empty())
    return false;
  float values[4]{};
  std::size_t start = 0;
  for (int i = 0; i < 4; ++i) {
    if (start > text.size())
      return false;
    const auto end = text.find(',', start);
    const auto slice = text.substr(start, end == std::string_view::npos
                                              ? std::string_view::npos
                                              : end - start);
    // std::from_chars for floating point needs a newer SDK than the macOS
    // deployment target; strtod is available everywhere and we validate that
    // the whole token was consumed.
    const std::string token(slice);
    char *endPtr = nullptr;
    const double parsed = std::strtod(token.c_str(), &endPtr);
    if (endPtr != token.c_str() + token.size())
      return false;
    values[i] = static_cast<float>(parsed);
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  out = {values[0], values[1], values[2], values[3]};
  return true;
}

// Lua positional elements (including mixed tables such as
// `ok={id=0,"sysse_on"}`) are stored in `array` by AstParser. Keys named with
// digits can also appear in `fields`; accept both shapes so a table variant
// never silently disables the title audio.
std::string firstAudioId(const LuaValue *value) {
  if (!value)
    return {};
  if (value->isString())
    return value->strVal;
  if (!value->isTable())
    return {};
  const LuaTable &table = *value->tableVal;
  if (!table.array.empty())
    return table.array.front().asString();
  for (const auto &[key, field] : table.fields) {
    bool numeric = !key.empty();
    for (char c : key) {
      if (c < '0' || c > '9') {
        numeric = false;
        break;
      }
    }
    if (numeric && field.isString())
      return field.strVal;
  }
  return {};
}

} // namespace

ArtemisUi::ArtemisUi(ArtemisAssets &assets) : assets_(assets) {}

bool ArtemisUi::load() {
  language_ = assets_.uiLanguage();
  tablePath_ = assets_.uiTablePath();
  if (tablePath_.empty())
    return false;

  std::string text;
  try {
    text = assets_.loadTextUtf8(tablePath_, "UTF-8");
  } catch (...) {
    return false;
  }
  root_ = AstParser::parseTopLevel(text);

  // Shared widgets live in `list_windows.tbl` even in localized builds.
  const std::string basePath = "system/table/list_windows.tbl";
  if (basePath != tablePath_) {
    try {
      const std::string baseText = assets_.loadTextUtf8(basePath, "UTF-8");
      if (!baseText.empty())
        baseRoot_ = AstParser::parseTopLevel(baseText);
    } catch (...) {
    }
  }

  const LuaTable *lang = root_->getTable("lang");
  if (!lang)
    return false;

  // The localized table (`ui_title01_en`) is preferred; the generic one is
  // kept as a fallback for games that ship a single language table.
  const LuaTable *title = lang->getTable("ui_title01_" + language_);
  if (!title)
    title = lang->getTable("ui_title01");
  if (!title) {
    for (const auto &[name, value] : lang->fields) {
      if (name.rfind("ui_title01", 0) == 0 && value.isTable()) {
        title = value.tableVal.get();
        break;
      }
    }
  }
  if (!title)
    return false;

  titleItems_.clear();
  titleButtons_.clear();
  for (const auto &[name, value] : title->fields) {
    auto item = makeItem(name, value);
    if (!item)
      continue;
    if (item->isButton)
      titleButtons_.push_back(*item);
    titleItems_.push_back(std::move(*item));
  }
  // The table order is the authored button order; keep it deterministic even
  // when the parser hands back fields in a different container order.
  std::stable_sort(titleButtons_.begin(), titleButtons_.end(),
                   [](const UiItem &a, const UiItem &b) {
                     const auto rank = [](const std::string &name) {
                       if (name == "bt_start")
                         return 0;
                       if (name == "bt_load")
                         return 1;
                       if (name == "bt_cont")
                         return 2;
                       if (name == "bt_select")
                         return 3;
                       if (name == "bt_extra")
                         return 4;
                       if (name == "bt_conf")
                         return 5;
                       if (name == "bt_exit")
                         return 6;
                       return 7;
                     };
                     return rank(a.name) < rank(b.name);
                   });
  return !titleButtons_.empty();
}

const LuaTable *ArtemisUi::group(const std::string &name) const {
  if (!root_)
    return nullptr;
  const LuaTable *lang = root_->getTable("lang");
  if (!lang)
    return nullptr;
  if (const LuaTable *exact = lang->getTable(name))
    return exact;
  for (const auto &[field, value] : lang->fields) {
    if (value.isTable() && field.rfind(name, 0) == 0)
      return value.tableVal.get();
  }
  return nullptr;
}

std::optional<UiItem> ArtemisUi::makeItem(const std::string &name,
                                          const LuaValue &value) const {
  if (!value.isTable())
    return std::nullopt;
  const LuaTable &table = *value.tableVal;
  UiItem item;
  item.name = name;
  item.logicalFile = table.getString("file");
  if (item.logicalFile.empty())
    return std::nullopt;
  item.p1 = table.getString("p1");
  item.p2 = table.getString("p2");
  item.isButton = table.getString("com") == "btn" || !item.p1.empty();
  item.rect = {static_cast<float>(table.getInt("x")),
               static_cast<float>(table.getInt("y")),
               static_cast<float>(table.getInt("w")),
               static_cast<float>(table.getInt("h"))};
  item.hasClip = parseRect(table.getString("clip"), item.clip);
  item.hasOverClip = parseRect(table.getString("clip_a"), item.overClip);
  item.hasActiveClip = parseRect(table.getString("clip_c"), item.activeClip);
  return item;
}

std::optional<UiItem> ArtemisUi::titleItem(const std::string &name) const {
  for (const auto &item : titleItems_) {
    if (item.name == name)
      return item;
  }
  return std::nullopt;
}

std::optional<UiItem> ArtemisUi::advItem(const std::string &name) const {
  const LuaTable *adv = group("ui_adv01");
  if (!adv)
    return std::nullopt;
  if (const LuaValue *value = adv->get(name))
    return makeItem(name, *value);
  return std::nullopt;
}

std::optional<UiItem> ArtemisUi::findItem(const std::string &name) const {
  const auto searchGroup = [&](const LuaTable &group) -> std::optional<UiItem> {
    if (const LuaValue *value = group.get(name)) {
      auto item = makeItem(name, *value);
      if (item)
        return item;
    }
    return std::nullopt;
  };
  for (const std::shared_ptr<LuaTable> *table : {&root_, &baseRoot_}) {
    if (!*table)
      continue;
    if (const LuaTable *lang = (*table)->getTable("lang")) {
      for (const auto &[groupName, groupValue] : lang->fields) {
        (void)groupName;
        if (!groupValue.isTable())
          continue;
        if (auto found = searchGroup(*groupValue.tableVal))
          return found;
      }
    }
    // The shared table keeps widgets such as `mw.select` at top level.
    for (const auto &[groupName, groupValue] : (*table)->fields) {
      if (groupName == "lang" || !groupValue.isTable())
        continue;
      if (auto found = searchGroup(*groupValue.tableVal))
        return found;
    }
  }
  return std::nullopt;
}

std::string ArtemisUi::titleBgmId(const std::string &route) const {
  for (const std::shared_ptr<LuaTable> *table : {&root_, &baseRoot_}) {
    if (!*table)
      continue;
    const LuaTable *init = (*table)->getTable("init");
    if (!init)
      continue;
    if (!route.empty()) {
      const std::string id = firstAudioId(init->get("titlebgm_" + route));
      if (!id.empty())
        return id;
    }
    // Fall back to the keys shipped by every NUKITASHI list_windows.tbl.
    for (const char *key :
         {"titlebgm_title01", "titlebgm_title02", "titlebgm_trial"}) {
      const std::string id = firstAudioId(init->get(key));
      if (!id.empty())
        return id;
    }
  }
  return {};
}

std::string ArtemisUi::clickSeId() const {
  for (const std::shared_ptr<LuaTable> *table : {&root_, &baseRoot_}) {
    if (!*table)
      continue;
    const LuaTable *csv = (*table)->getTable("csv");
    if (!csv)
      continue;
    const LuaTable *sysse = csv->getTable("sysse");
    if (!sysse)
      continue;
    // se_ok() calls sysse("ok"), which plays csv.sysse.ok[1] through
    // sound/sysse/<id>.ogg.
    const std::string id = firstAudioId(sysse->get("ok"));
    if (!id.empty())
      return id;
  }
  return "sysse_on"; // shipped fallback in both retail editions
}

double ArtemisUi::initNumber(const std::string &key, double fallback) const {
  for (const std::shared_ptr<LuaTable> *table : {&root_, &baseRoot_}) {
    if (!*table)
      continue;
    const LuaTable *init = (*table)->getTable("init");
    if (!init)
      continue;
    if (const LuaValue *value = init->get(key); value && value->isNumber())
      return value->numVal;
  }
  return fallback;
}

std::string ArtemisUi::resolveImage(const UiItem &item) const {
  return resolveImage(item.logicalFile);
}

std::string ArtemisUi::resolveImage(const std::string &logicalFile) const {
  return assets_.resolveUiImagePath(logicalFile);
}

SDL_Texture &ArtemisUi::texture(Kotonoha::RendererRef renderer,
                                const std::string &logicalFile) {
  const std::string resolved = resolveImage(logicalFile);
  return assets_.texture(renderer, resolved, 1.0f, nullptr);
}

SDL_Texture &ArtemisUi::draw(Kotonoha::RendererRef renderer, SDL_Texture &source,
                             const SDL_FRect &destination,
                             const SDL_FRect *sourceRect, Uint8 alpha) {
  SDL_SetTextureAlphaMod(&source, alpha);
  SDL_RenderTexture(&renderer, &source, sourceRect, &destination);
  SDL_SetTextureAlphaMod(&source, 255);
  return source;
}

} // namespace Artemis
