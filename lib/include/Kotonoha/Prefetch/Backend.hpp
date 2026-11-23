#pragma once

#include <Kotonoha/Prefetch/Types.hpp>

namespace Kotonoha::Prefetch {

// Transport abstraction. Implementations are called from the game's main
// thread; the concrete web backend only enqueues work on the browser's
// Fetch/IndexedDB callback turn and never suspends Asyncify.
class Backend {
public:
  virtual ~Backend() = default;

  // Only true when the build/runtime can actually issue prefetch requests.
  virtual bool enabled() const noexcept = 0;

  // Called once around a dispatch burst so backends can bound bursts.
  virtual void beginDispatch() noexcept {}
  virtual void endDispatch() noexcept {}

  virtual void prefetch(const NodeId &id) = 0;
};

} // namespace Kotonoha::Prefetch
