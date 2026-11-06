#include <Kotonoha/core/VisualTransition.hpp>

#include <cmath>

namespace Kotonoha {

bool VisualTransition::copy(RendererRef renderer, SDL_Texture &source,
                            TexturePtr *destination) {
  if (!destination)
    return false;
  float width = 0, height = 0;
  if (!SDL_GetTextureSize(&source, &width, &height) || width < 1 || height < 1)
    return false;
  TexturePtr next;
  SDL_Texture *target = destination->get();
  float previousWidth = 0, previousHeight = 0;
  if (!target || !SDL_GetTextureSize(target, &previousWidth, &previousHeight) ||
      static_cast<int>(std::lround(previousWidth)) !=
          static_cast<int>(std::lround(width)) ||
      static_cast<int>(std::lround(previousHeight)) !=
          static_cast<int>(std::lround(height))) {
    next.reset(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_TARGET,
                                 static_cast<int>(std::lround(width)),
                                 static_cast<int>(std::lround(height))));
    target = next.get();
    if (!target || !SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR))
      return false;
  }
  SDL_Texture *previous = SDL_GetRenderTarget(&renderer);
  const bool copied = SDL_SetRenderTarget(&renderer, target) &&
                      SDL_SetRenderLogicalPresentation(
                          &renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
                      SDL_SetRenderViewport(&renderer, nullptr) &&
                      SDL_SetRenderClipRect(&renderer, nullptr) &&
                      SDL_SetRenderScale(&renderer, 1, 1) &&
                      SDL_RenderTexture(&renderer, &source, nullptr, nullptr);
  const bool restored = SDL_SetRenderTarget(&renderer, previous);
  if (!copied || !restored)
    return false;
  if (next)
    *destination = std::move(next);
  return true;
}

bool VisualTransition::begin(std::string reason, std::string outgoingOwner) {
  if (active())
    return outgoingValid();
  ++generation_;
  framesHeld_ = 0;
  incomingDrawable_ = false;
  reportedEmpty_ = false;
  reason_ = std::move(reason);
  outgoingOwner_ = std::move(outgoingOwner);
  incomingOwner_.clear();
  outgoing_ = std::move(lastPresented_);
  state_ = State::OutgoingCaptured;
  return outgoingValid();
}

void VisualTransition::arm(std::string incomingOwner) {
  if (!active())
    begin("scene-change", {});
  incomingOwner_ = std::move(incomingOwner);
  state_ = State::PreparingIncoming;
}

void VisualTransition::cancel() {
  if (outgoing_)
    lastPresented_ = std::move(outgoing_);
  state_ = State::Idle;
  reason_.clear();
  incomingOwner_.clear();
  framesHeld_ = 0;
  incomingDrawable_ = false;
  reportedEmpty_ = false;
}

void VisualTransition::reset() {
  cancel();
  lastPresented_.reset();
  outgoingOwner_.clear();
}

bool VisualTransition::compose(RendererRef renderer, SDL_Texture &novelTarget,
                               bool drawable, bool boundary,
                               const std::string &owner) {
  if (boundary && !active()) {
    // The final alpha-zero choice composition is still in the owned target.
    // Snapshot it before any Present or scene teardown; if its primary image
    // expired already, retain the previous drawable frame instead.
    if (drawable && !copy(renderer, novelTarget, &lastPresented_))
      return false;
    begin("terminal-frame", owner);
  }
  // Only the armed owner may publish readiness. A late drawable result from a
  // retired scene must not release the outgoing frame for a newer scene.
  if (state_ == State::PreparingIncoming && drawable &&
      incomingOwner_ == owner) {
    state_ = State::IncomingDrawable;
    if (!copy(renderer, novelTarget, &lastPresented_))
      return false;
    incomingDrawable_ = true;
    outgoing_.reset();
    state_ = State::Idle;
    return true;
  }
  if (active()) {
    ++framesHeld_;
#ifdef KOTONOHA_DEV_CONTROL
    if (!outgoing_ && !drawable && !reportedEmpty_) {
      reportedEmpty_ = true;
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "DEV invariant: transition %llu (%s) has no outgoing or "
                   "incoming drawable frame",
                   static_cast<unsigned long long>(generation_),
                   reason_.c_str());
    }
#endif
    if (outgoing_) {
      // Replace the authored clear/early fade on the owned Novel target. The
      // backbuffer is never sampled, including after SDL_RenderPresent.
      if (!SDL_SetRenderTarget(&renderer, &novelTarget) ||
          !SDL_SetRenderLogicalPresentation(
              &renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) ||
          !SDL_SetRenderViewport(&renderer, nullptr) ||
          !SDL_SetRenderClipRect(&renderer, nullptr) ||
          !SDL_SetRenderScale(&renderer, 1, 1) ||
          !SDL_RenderTexture(&renderer, outgoing_.get(), nullptr, nullptr)) {
#ifdef KOTONOHA_DEV_CONTROL
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "DEV invariant: outgoing Novel frame could not replace an "
                     "empty transition frame: %s",
                     SDL_GetError());
#endif
        return false;
      }
    }
    return true;
  }
  return !drawable || copy(renderer, novelTarget, &lastPresented_);
}

} // namespace Kotonoha
