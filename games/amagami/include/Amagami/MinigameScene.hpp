#pragma once
#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <functional>
#include <vector>

namespace Amagami {

// Experimental isolated timing-bar component. No original Amagami script has
// been mapped to this prototype, and the production runtime does not use it.
class AmagamiMinigameScene : public Kotonoha::Scene {
public:
  enum class Type { TimingBar };

  struct Result {
    bool success;
    float score; // 0.0 to 1.0
  };

  using ResultCallback = std::function<void(Result)>;

  explicit AmagamiMinigameScene(Kotonoha::Engine &engine)
      : Scene(engine), engine_(engine) {}

  void start(Type type, double duration, ResultCallback callback);
  void update() override;
  void update(double delta);
  void render(Kotonoha::RendererRef r, float scale) override;
  bool onEvent(Kotonoha::EventRef event) override { return handleEvent(event); }
  bool handleEvent(Kotonoha::EventRef event) override;
  bool capturesInput() const override { return active_; }
  bool pausesStory() const override { return active_; }

private:
  Kotonoha::Engine &engine_;
  Kotonoha::Clock clock_;
  bool active_ = false;
  Type type_ = Type::TimingBar;
  double duration_ = 0;
  double elapsed_ = 0;
  ResultCallback callback_ = nullptr;

  // Timing Bar state
  float bar_pos_ = 0; // 0.0 to 1.0
  float target_pos_ = 0.7f;
  float target_width_ = 0.1f;
  bool input_processed_ = false;
};

} // namespace Amagami
