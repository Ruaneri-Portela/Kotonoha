#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <cstdint>
#include <string>

namespace Kotonoha {

// Owns the last drawable Novel raster independently of SDL's swapchain.
// All methods run on the renderer thread. A transition keeps its outgoing
// texture until the incoming owner actually draws a primary visual.
class VisualTransition {
public:
  enum class State { Idle, OutgoingCaptured, PreparingIncoming, IncomingDrawable };

  bool begin(std::string reason, std::string outgoingOwner);
  void arm(std::string incomingOwner);
  void cancel();
  void reset();
  bool compose(RendererRef renderer, SDL_Texture &novelTarget, bool drawable,
               bool boundary, const std::string &owner);

  bool active() const { return state_ != State::Idle; }
  bool outgoingValid() const { return outgoing_ != nullptr; }
  bool hasLastPresented() const { return lastPresented_ != nullptr; }
  bool incomingArmed() const { return state_ == State::PreparingIncoming; }
  bool incomingDrawable() const { return incomingDrawable_; }
  State state() const { return state_; }
  uint64_t generation() const { return generation_; }
  uint64_t framesHeld() const { return framesHeld_; }
  const std::string &reason() const { return reason_; }
  const std::string &outgoingOwner() const { return outgoingOwner_; }
  const std::string &incomingOwner() const { return incomingOwner_; }

private:
  static bool copy(RendererRef renderer, SDL_Texture &source, TexturePtr *destination);
  TexturePtr lastPresented_;
  TexturePtr outgoing_;
  State state_ = State::Idle;
  uint64_t generation_ = 0;
  uint64_t framesHeld_ = 0;
  bool incomingDrawable_ = false;
  bool reportedEmpty_ = false;
  std::string reason_, outgoingOwner_, incomingOwner_;
};

} // namespace Kotonoha
