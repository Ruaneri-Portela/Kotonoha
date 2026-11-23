#pragma once

#include <Kotonoha/Prefetch/Backend.hpp>
#include <Kotonoha/Prefetch/Types.hpp>

#include <cstddef>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Kotonoha::Prefetch {

// INTEGRATION GUIDANCE (for game ports / VM presentation layers):
//
// 1. Once per port init, own a Scheduler and optionally bind a backend:
//      static Kotonoha::Prefetch::WasmBackend backend;
//      Kotonoha::Prefetch::Scheduler scheduler;
//      scheduler.setBackend(&backend);
//
// 2. Whenever the VM enters a new script/scene, report the opaque id:
//      scheduler.observe(vm.currentNodeId());
//      scheduler.prefetch();   // compiles to a no-op on native builds
//    The VM stays presentation-free: it only publishes NodeId strings, so
//    every engine VM can use the same core with zero per-engine code here.
//
// 3. Feed data-driven hints from the game's own manifest (never hardcoded in
//    the core): scheduler.registerHint("scripts/day01", "scripts/day02", 3.0f);
//
// 4. Optional: scheduler.registerKnown(id) enables the sibling heuristic.
//
// A concrete, dependency-free example lives in
// lib/tests/Prefetch/PrefetchTests.cpp.
class Scheduler {
public:
  struct Config {
    std::size_t maxNodes = kDefaultMaxNodes;
    std::size_t recencyCapacity = kDefaultRecencyCapacity;
    bool learnFromObservation = true;
    bool siblingsEnabled = true;
    bool sequentialEnabled = true;
  };

  Scheduler();
  explicit Scheduler(Config config);
  ~Scheduler() = default;

  Scheduler(const Scheduler &) = default;
  Scheduler &operator=(const Scheduler &) = default;
  Scheduler(Scheduler &&) noexcept = default;
  Scheduler &operator=(Scheduler &&) noexcept = default;

  void setBackend(Backend *backend) noexcept { backend_ = backend; }
  Backend *backend() const noexcept { return backend_; }

  void registerHint(NodeId from, NodeId to, Weight weight = 1.0f);
  void registerHints(NodeId from, std::initializer_list<Hint> hints);
  void registerKnown(NodeId id);

  // Records the current node, learns the previous -> current transition and
  // updates recency. Cheap: called once per scene change, not per frame.
  void observe(NodeId current);

  // Deterministic: hints > learned > recency > siblings > sequential; ties
  // are broken by weight then lexicographic id. Results are deduped/bounded.
  [[nodiscard]] Plan predict() const;

  // Dispatches the current plan to the bound backend. On native builds the
  // body is compiled out: zero HTTP work, zero behavioural change.
  void prefetch();

  // Always-available explicit dispatch for custom backends and tests.
  void dispatch();

  void observeAndPrefetch(NodeId current) {
    observe(std::move(current));
    prefetch();
  }

  const NodeId &current() const noexcept { return current_; }
  bool hasCurrent() const noexcept { return hasCurrent_; }

  void clear();

private:
  struct Candidate {
    NodeId id;
    unsigned tier = 0;
    Weight weight = 0.0f;
  };

  Config config_;
  Backend *backend_ = nullptr;
  NodeId current_;
  bool hasCurrent_ = false;
  std::unordered_map<NodeId, std::vector<Hint>> hints_;
  std::unordered_map<NodeId, std::vector<Hint>> learned_;
  std::vector<NodeId> known_;
  std::unordered_set<NodeId> knownSet_;
  std::vector<NodeId> recent_;
  // Reused across predict() calls; only the returned Plan allocates.
  mutable std::vector<Candidate> scratch_;
  mutable std::unordered_map<NodeId, std::size_t> index_;

  void addLearned(const NodeId &from, const NodeId &to);
  void addCandidate(const NodeId &id, unsigned tier, Weight weight) const;
};

} // namespace Kotonoha::Prefetch
