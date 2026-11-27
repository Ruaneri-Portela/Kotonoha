#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace Kotonoha {

// A speculative MediaPlayer::PreparedInput owns a State whose destructor
// marks its cancellation token true. Never share that token between two
// different School Days media jobs: dropping one unused prepared voice/video
// must not cancel every later PlayMovie/PlaySE in the current ORS scene.
//
// The owning worker serializes Allocate/CancelAll under its queue mutex. The
// atomic tokens themselves may be read/written by the asynchronous decoders.
class SchoolDaysMediaCancellationPool final {
public:
  using Token = std::shared_ptr<std::atomic_bool>;

  Token Allocate() {
    if (tokens_.size() >= 64u) {
      tokens_.erase(
          std::remove_if(tokens_.begin(), tokens_.end(),
                         [](const auto& token) { return token.expired(); }),
          tokens_.end());
    }
    auto token = std::make_shared<std::atomic_bool>(false);
    tokens_.push_back(token);
    return token;
  }

  void CancelAll() noexcept {
    for (auto& weak : tokens_)
      if (auto token = weak.lock())
        token->store(true, std::memory_order_release);
    tokens_.clear();
  }

private:
  std::vector<std::weak_ptr<std::atomic_bool>> tokens_;
};

} // namespace Kotonoha
