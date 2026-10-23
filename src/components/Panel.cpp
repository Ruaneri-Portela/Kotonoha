#include <Kotonoha/components/Panel.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Kotonoha {

namespace {

SDL_FColor Mix(SDL_Color a, SDL_Color b, float t) {
  auto lerp = [t](Uint8 x, Uint8 y) { return (x + (y - x) * t) / 255.0f; };
  return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}

// Rounded-rectangle outline points, clockwise from the top-left corner arc.
std::vector<SDL_FPoint> Outline(const SDL_FRect &r, float radius) {
  constexpr int kSteps = 6;
  const float k = std::min({radius, r.w * 0.5f, r.h * 0.5f});
  const float cx[4] = {r.x + k, r.x + r.w - k, r.x + r.w - k, r.x + k};
  const float cy[4] = {r.y + k, r.y + k, r.y + r.h - k, r.y + r.h - k};
  std::vector<SDL_FPoint> pts;
  for (int corner = 0; corner < 4; ++corner) {
    const float start = (2 + corner) * 1.5707963f;
    for (int i = 0; i <= kSteps; ++i) {
      const float a = start + 1.5707963f * i / kSteps;
      pts.push_back({cx[corner] + std::cos(a) * k, cy[corner] + std::sin(a) * k});
    }
  }
  return pts;
}

} // namespace

void DrawPanel(SDL_Renderer *renderer, const SDL_FRect &rect, const PanelStyle &style) {
  if (renderer == nullptr || rect.w <= 0 || rect.h <= 0) return;
  SDL_BlendMode old = SDL_BLENDMODE_NONE;
  SDL_GetRenderDrawBlendMode(renderer, &old);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  const auto pts = Outline(rect, style.radius);
  std::vector<SDL_Vertex> verts;
  verts.reserve(pts.size() + 1);
  for (const SDL_FPoint &p : pts) {
    const float t = std::clamp((p.y - rect.y) / rect.h, 0.0f, 1.0f);
    verts.push_back({p, Mix(style.top, style.bottom, t), {0, 0}});
  }
  // Fan triangulation around the first vertex is valid for a convex outline.
  std::vector<int> idx;
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    idx.push_back(0);
    idx.push_back(int(i));
    idx.push_back(int(i + 1));
  }
  SDL_RenderGeometry(renderer, nullptr, verts.data(), int(verts.size()), idx.data(), int(idx.size()));

  if (style.borderWidth > 0 && style.border.a > 0) {
    SDL_SetRenderDrawColor(renderer, style.border.r, style.border.g, style.border.b, style.border.a);
    std::vector<SDL_FPoint> loop = pts;
    loop.push_back(pts.front());
    SDL_RenderLines(renderer, loop.data(), int(loop.size()));
  }
  SDL_SetRenderDrawBlendMode(renderer, old);
}

void DrawDim(SDL_Renderer *renderer, const SDL_FRect &rect, Uint8 alpha) {
  SDL_BlendMode old = SDL_BLENDMODE_NONE;
  Uint8 r, g, b, a;
  SDL_GetRenderDrawBlendMode(renderer, &old);
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
  SDL_RenderFillRect(renderer, &rect);
  SDL_SetRenderDrawBlendMode(renderer, old);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

} // namespace Kotonoha
