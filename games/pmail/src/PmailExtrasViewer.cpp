#include "PmailExtrasViewer.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <Pmail/Port.hpp>
#include <algorithm>
#include <cctype>

namespace Kotonoha::PmailPort {
namespace {
constexpr float kStageWidth = 800.0f;
constexpr float kStageHeight = 600.0f;
constexpr float kCellX[4] = {58.0f, 233.0f, 408.0f, 583.0f};
constexpr float kCellY[3] = {113.0f, 263.0f, 413.0f};
constexpr float kCellWidth = 160.0f;
constexpr float kCellHeight = 120.0f;
constexpr float kTabX[4] = {7.0f, 204.0f, 401.0f, 599.0f};
constexpr float kTabWidth = 193.0f;
constexpr float kMusicRowFirst = 88.0f;
constexpr float kMusicRowHeight = 19.4f;
constexpr int kMusicVisibleRows = 24;

const char *const kCategoryNames[8] = {"ETC",  "HIRO", "MIKA", "SAKU",
                                       "YUZU", "TOHK", "NANA", "RINN"};

std::string LowerAscii(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  return value;
}

std::string TrackLabel(const std::string &id) {
  const auto slash = id.rfind('/');
  std::string label = slash == std::string::npos ? id : id.substr(slash + 1);
  for (auto &c : label)
    c = char(std::toupper(static_cast<unsigned char>(c)));
  return label;
}
} // namespace

PmailExtrasViewer::PmailExtrasViewer(Kotonoha::Engine &engine, PmailGame &game,
                                     Kotonoha::Providers::SnowAssets &assets)
    : engine_(engine), game_(game), assets_(assets) {}

SDL_Texture *PmailExtrasViewer::texture(Kotonoha::RendererRef renderer,
                                        const std::string &file) {
  const std::string key = LowerAscii(file);
  auto found = textures_.find(key);
  if (found != textures_.end())
    return found->second.tex.get();

  Texture entry;
  std::vector<std::string> candidates;
  if (key.find('.') != std::string::npos)
    candidates.push_back(key);
  else {
    candidates = {key + ".cgg", key + ".crgb", key + ".img", key + ".evd"};
  }
  for (const auto &candidate : candidates) {
    try {
      auto surface = assets_.loadSurfaceAsset("graphic/" + candidate);
      if (!surface)
        continue;
      entry.tex.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
      if (entry.tex) {
        SDL_GetTextureSize(entry.tex.get(), &entry.w, &entry.h);
        SDL_SetTextureBlendMode(entry.tex.get(), SDL_BLENDMODE_BLEND);
        break;
      }
    } catch (...) {
    }
  }
  auto &cached = textures_[key];
  cached = std::move(entry);
  return cached.tex.get();
}

SDL_Texture *PmailExtrasViewer::background(Kotonoha::RendererRef renderer) {
  switch (game_.extrasViewerMode()) {
  case PmailGame::ExtrasViewerMode::CgGrid:
    return texture(renderer, "cgmode00.crgb");
  case PmailGame::ExtrasViewerMode::Recollection:
    return texture(renderer, "kaimode00.crgb");
  case PmailGame::ExtrasViewerMode::Music:
    return texture(renderer, "musmode00a.crgb");
  default:
    return nullptr;
  }
}

bool PmailExtrasViewer::loadMap(const std::string &file) const {
  const std::string key = LowerAscii(file);
  if (!map_.empty() && mapName_ == key)
    return true;
  mapName_ = key;
  map_.clear();
  try {
    map_ = assets_.readAsset("graphic/" + key);
  } catch (...) {
    map_.clear();
  }
  if (map_.size() != size_t(kStageWidth) * size_t(kStageHeight))
    map_.clear();
  return !map_.empty();
}

PmailExtrasViewer::Hit PmailExtrasViewer::hitTest(float x, float y) const {
  const auto mode = game_.extrasViewerMode();
  if (mode == PmailGame::ExtrasViewerMode::None)
    return {};
  if (game_.extrasViewerFullView())
    return {HitKind::Exit, 0};

  if (mode == PmailGame::ExtrasViewerMode::Music) {
    if (x >= 37.0f && x <= 412.0f && y >= kMusicRowFirst - 6.0f &&
        y <= kMusicRowFirst + kMusicVisibleRows * kMusicRowHeight) {
      const int row = int((y - kMusicRowFirst) / kMusicRowHeight);
      const int track = game_.extrasViewerTrackFirst() + row + 1;
      if (row >= 0 && row < kMusicVisibleRows && track >= 1 &&
          track <= game_.extrasViewerTrackCount())
        return {HitKind::Track, track};
    }
    if (!loadMap("musmode00.map"))
      return {};
    const int px = int(x), py = int(y);
    if (px < 0 || px >= int(kStageWidth) || py < 0 || py >= int(kStageHeight))
      return {};
    switch (map_[size_t(py) * size_t(kStageWidth) + size_t(px)]) {
    case 25:
      return {HitKind::Exit, 0};
    case 26:
      return {HitKind::NextTrack, 0};
    case 27:
      return {HitKind::Play, 0};
    case 28:
      return {HitKind::Stop, 0};
    case 29:
      return {HitKind::PrevTrack, 0};
    case 30:
      return {HitKind::Repeat, 0};
    default:
      return {};
    }
  }

  if (!loadMap("cgmode00.map"))
    return {};
  const int px = int(x), py = int(y);
  if (px < 0 || px >= int(kStageWidth) || py < 0 || py >= int(kStageHeight))
    return {};
  const int value = map_[size_t(py) * size_t(kStageWidth) + size_t(px)];
  if (value >= 1 && value <= 12)
    return {HitKind::Cell, value};
  if (value == 13)
    return {HitKind::PrevPage, 0};
  if (value == 14)
    return {HitKind::NextPage, 0};
  if (value >= 15 && value <= 18)
    return {HitKind::Category, 4 + value - 15};
  if (value >= 19 && value <= 22)
    return {HitKind::Category, value - 19};
  if (value == 23)
    return {HitKind::Exit, 0};
  return {};
}

void PmailExtrasViewer::renderGrid(Kotonoha::RendererRef renderer,
                                   bool recollection) {
  auto *bg = background(renderer);
  if (bg)
    Kotonoha::RenderUpscaledTexture(&renderer, bg, nullptr, nullptr);

  auto &ui = engine_.GetUI();
  const int category = game_.extrasViewerCategory();
  for (int i = 0; i < 8; ++i) {
    const int row = i / 4;
    const float x = kTabX[i % 4];
    const float y = row == 0 ? 37.0f : 57.0f;
    if (i == category) {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 255, 214, 110, 70);
      const SDL_FRect selected{x + 1, y + 1, kTabWidth - 2, 17};
      SDL_RenderFillRect(&renderer, &selected);
    }
    ui.textSized(renderer, kCategoryNames[i], x + 6, y + 2, kTabWidth - 12,
                 13.0f,
                 i == category ? SDL_Color{255, 226, 140, 255}
                               : SDL_Color{214, 228, 232, 255});
  }

  const auto cells = game_.extrasViewerCells();
  const int selected = game_.extrasViewerSlot();
  for (int slot = 0; slot < int(cells.size()) && slot < 12; ++slot) {
    if (cells[size_t(slot)].empty())
      continue;
    const float x = kCellX[slot % 4];
    const float y = kCellY[slot / 4];
    if (auto *tex = texture(renderer, cells[size_t(slot)])) {
      float w = 0, h = 0;
      SDL_GetTextureSize(tex, &w, &h);
      if (w > 0 && h > 0) {
        const float scale = std::min(kCellWidth / w, kCellHeight / h);
        const SDL_FRect dst{x + (kCellWidth - w * scale) * 0.5f,
                            y + (kCellHeight - h * scale) * 0.5f, w * scale,
                            h * scale};
        Kotonoha::RenderUpscaledTexture(&renderer, tex, nullptr, &dst);
      }
    }
    if (selected == slot + 1) {
      SDL_SetRenderDrawColor(&renderer, 255, 214, 110, 255);
      const SDL_FRect border{x, y, kCellWidth, kCellHeight};
      SDL_RenderRect(&renderer, &border);
    }
  }

  if (game_.extrasViewerPageCount() > 1) {
    SDL_SetRenderDrawColor(&renderer, 235, 240, 240, 255);
    const SDL_FRect left{13, 287, 12, 103};
    const SDL_FRect right{776, 287, 12, 103};
    SDL_RenderRect(&renderer, &left);
    SDL_RenderRect(&renderer, &right);
    ui.textSized(renderer, "<", 14, 326, 12, 18.0f, {235, 240, 240, 255});
    ui.textSized(renderer, ">", 777, 326, 12, 18.0f, {235, 240, 240, 255});
  }
  ui.textSized(renderer,
               std::to_string(game_.extrasViewerPage() + 1) + "/" +
                   std::to_string(game_.extrasViewerPageCount()),
               666, 10, 40, 14.0f, {255, 80, 70, 255});
  (void)recollection;
}

void PmailExtrasViewer::renderMusic(Kotonoha::RendererRef renderer) {
  auto *bg = background(renderer);
  if (bg)
    Kotonoha::RenderUpscaledTexture(&renderer, bg, nullptr, nullptr);

  auto &ui = engine_.GetUI();
  const auto cells = game_.extrasViewerCells();
  const int first = game_.extrasViewerTrackFirst();
  const int selected = game_.extrasViewerTrack();
  for (int row = 0; row < int(cells.size()) && row < kMusicVisibleRows; ++row) {
    const int track = first + row + 1;
    const float y = kMusicRowFirst + row * kMusicRowHeight;
    if (track == selected) {
      SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(&renderer, 255, 214, 110, 60);
      const SDL_FRect highlight{39, y - 2, 371, 18};
      SDL_RenderFillRect(&renderer, &highlight);
    }
    ui.textSized(renderer, TrackLabel(cells[size_t(row)]), 44, y, 360, 13.0f,
                 track == selected ? SDL_Color{255, 226, 140, 255}
                                   : SDL_Color{214, 228, 232, 255});
  }
  if (selected >= 1 && selected <= game_.extrasViewerTrackCount())
    ui.textSized(renderer, TrackLabel(cells[size_t(selected - first - 1)]), 444,
                 94, 168, 14.0f, {255, 226, 140, 255});
  if (game_.extrasViewerRepeat()) {
    SDL_SetRenderDrawColor(&renderer, 40, 190, 90, 255);
    const SDL_FRect repeat{666, 143, 14, 14};
    SDL_RenderFillRect(&renderer, &repeat);
  }
}

void PmailExtrasViewer::renderFullView(Kotonoha::RendererRef renderer) {
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  const SDL_FRect full{0, 0, kStageWidth, kStageHeight};
  SDL_RenderFillRect(&renderer, &full);
  const auto cells = game_.extrasViewerCells();
  const int slot = game_.extrasViewerSlot();
  if (slot >= 1 && slot <= int(cells.size()) &&
      !cells[size_t(slot - 1)].empty()) {
    if (auto *tex = texture(renderer, cells[size_t(slot - 1)])) {
      float w = 0, h = 0;
      SDL_GetTextureSize(tex, &w, &h);
      if (w > 0 && h > 0) {
        const SDL_FRect area{24, 76, 752, 512};
        const float scale = std::min(area.w / w, area.h / h);
        const SDL_FRect dst{area.x + (area.w - w * scale) * 0.5f,
                            area.y + (area.h - h * scale) * 0.5f, w * scale,
                            h * scale};
        Kotonoha::RenderUpscaledTexture(&renderer, tex, nullptr, &dst);
      }
    }
  }
  engine_.GetUI().textSized(renderer, "ESC: back", 24, 40, 200, 14.0f,
                            {255, 226, 140, 255});
}

void PmailExtrasViewer::render(Kotonoha::RendererRef renderer) {
  switch (game_.extrasViewerMode()) {
  case PmailGame::ExtrasViewerMode::CgGrid:
    if (game_.extrasViewerFullView())
      renderFullView(renderer);
    else
      renderGrid(renderer, false);
    break;
  case PmailGame::ExtrasViewerMode::Recollection:
    renderGrid(renderer, true);
    break;
  case PmailGame::ExtrasViewerMode::Music:
    renderMusic(renderer);
    break;
  default:
    break;
  }
}

} // namespace Kotonoha::PmailPort
