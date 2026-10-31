#pragma once

#include <Kotonoha/core/Scene.hpp>

#include <vector>

namespace Kotonoha {

// Fades the whole scene frame to/from a solid color. In: color -> clear,
// Out: clear -> color. Events follow either an external timeline
// (setPosition, e.g. ORS ticks / 24) or the scene's own clock (play()).
class FadeScene : public Scene {
public:
  enum class Direction { In, Out };
  struct Event {
    double start = 0, end = 0; // seconds
    SDL_Color color{0, 0, 0, 255};
    Direction direction = Direction::Out;
    // Keep the final opaque color after an Out fade until the next event.
    bool hold = false;
  };

  explicit FadeScene(Engine &engine, bool designSpace = false)
      : Scene(engine), design(designSpace) {}

  void add(const Event &event);
  void clear();
  // Timeline mode: the position in seconds of the owning timeline.
  void setPosition(double seconds);
  double position() const;
  // Self-clocked fade starting now; switches to the scene's own clock.
  void play(Direction direction, double seconds, SDL_Color color = {0, 0, 0, 255},
            bool hold = false);

  // Coverage 0 (clear) .. 1 (opaque color) of one event at a position.
  static float alphaAt(const Event &event, double position);
  // Coverage and color of the most recent event affecting the position.
  float alpha() const;
  SDL_Color color() const;
  // True while a fade is in progress (not counting a held color).
  bool active() const;
  bool finished() const { return !active(); }

  bool designSpace() const override { return design; }
  void render(RendererRef renderer, float scale) override;

private:
  std::vector<Event> events;
  double timelinePosition = 0;
  Uint64 clockStart = 0;
  bool selfClock = false, design;
  const Event *current() const;
};

} // namespace Kotonoha
