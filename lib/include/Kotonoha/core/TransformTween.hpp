#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

struct Transform2D {
  float x = 0;
  float y = 0;
  float scaleX = 1;
  float scaleY = 1;
  float rotation = 0;
  float opacity = 1;
};

inline Transform2D InterpolateTransform(const Transform2D &from,
                                        const Transform2D &to, double progress,
                                        int curve = 0) {
  progress = std::clamp(progress, 0.0, 1.0);
  if (curve == 1)
    progress = std::sin(progress * 1.5707963267948966);
  else if (curve == 2)
    progress = 1 - std::cos(progress * 1.5707963267948966);
  const auto interpolate = [progress](float start, float end) {
    return start + (end - start) * static_cast<float>(progress);
  };
  return {interpolate(from.x, to.x),
          interpolate(from.y, to.y),
          interpolate(from.scaleX, to.scaleX),
          interpolate(from.scaleY, to.scaleY),
          interpolate(from.rotation, to.rotation),
          interpolate(from.opacity, to.opacity)};
}

class TransformAnimation {
  Transform2D from_, to_;
  double duration_ = 0, elapsed_ = 0;
  int curve_ = 0;

public:
  TransformAnimation() = default;
  TransformAnimation(Transform2D from, Transform2D to, double duration,
                     int curve = 0)
      : from_(from), to_(to), duration_(duration), curve_(curve) {
    if (!std::isfinite(duration) || duration < 0)
      throw std::invalid_argument("Invalid transform animation duration");
  }
  Transform2D sample(double elapsed) const {
    if (!std::isfinite(elapsed) || elapsed < 0)
      throw std::invalid_argument("Invalid transform animation time");
    return InterpolateTransform(
        from_, to_, duration_ > 0 ? elapsed / duration_ : 1, curve_);
  }
  Transform2D value() const { return sample(elapsed_); }
  Transform2D advance(double delta) {
    if (!std::isfinite(delta) || delta < 0)
      throw std::invalid_argument("Invalid transform animation delta");
    elapsed_ += std::min(delta, duration_ - elapsed_);
    return value();
  }
  double duration() const { return duration_; }
  double remaining() const { return duration_ - elapsed_; }
  bool finished() const { return elapsed_ >= duration_; }
};

} // namespace Kotonoha
