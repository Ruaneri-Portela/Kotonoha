#include <Kotonoha/Prefetch/Prefetch.hpp>
#include <Kotonoha/core/Runtime.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace Kotonoha::Prefetch;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "PrefetchTests FAILED: " << message << '\n';
    std::exit(2);
  }
}

bool Contains(const Plan &plan, const NodeId &id,
              std::size_t *index = nullptr) {
  for (std::size_t i = 0; i < plan.nodes.size(); ++i) {
    if (plan.nodes[i] == id) {
      if (index)
        *index = i;
      return true;
    }
  }
  return false;
}

// Test double: records what the scheduler dispatches without any HTTP.
class RecordingBackend final : public Backend {
public:
  bool enabled() const noexcept override { return true; }
  void beginDispatch() noexcept override { ++begins; }
  void endDispatch() noexcept override { ++ends; }
  void prefetch(const NodeId &id) override { ids.push_back(id); }

  std::vector<NodeId> ids;
  int begins = 0;
  int ends = 0;
};

void testWeightsOrderingDedupAndBound() {
  Scheduler::Config config;
  config.maxNodes = 3;
  Scheduler scheduler(config);
  scheduler.registerHint("root", "low", 1.0f);
  scheduler.registerHint("root", "high", 5.0f);
  scheduler.registerHint("root", "mid", 3.0f);
  scheduler.registerHint("root", "high", 0.5f); // duplicate: keep max weight
  scheduler.observe("root");

  const Plan plan = scheduler.predict();
  Require(plan.size() == 3, "plan bounded to maxNodes");
  Require(plan.nodes[0] == "high" && plan.nodes[1] == "mid" &&
              plan.nodes[2] == "low",
          "weights descending, duplicate folded");

  Scheduler ties;
  ties.registerHint("root", "z", 2.0f);
  ties.registerHint("root", "a", 2.0f);
  ties.observe("root");
  const Plan ordered = ties.predict();
  Require(ordered.nodes[0] == "a" && ordered.nodes[1] == "z",
          "equal weights break ties lexicographically");
  Require(ties.predict().nodes == ordered.nodes,
          "predict() is deterministic across calls");
}

void testTierPriorityAndLearning() {
  Scheduler scheduler;
  scheduler.observe("A");
  scheduler.observe("B"); // learns A -> B
  scheduler.observe("A"); // now current is A again

  Plan plan = scheduler.predict();
  Require(!plan.empty() && plan.nodes[0] == "B",
          "learned transition outranks plain recency");

  scheduler.registerHint("A", "H", 0.01f);
  plan = scheduler.predict();
  Require(!plan.empty() && plan.nodes[0] == "H" && plan.nodes[1] == "B",
          "registered hints always outrank learned/recency candidates");
}

void testSiblingHeuristic() {
  Scheduler scheduler;
  scheduler.registerKnown("scripts/story/a.ks");
  scheduler.registerKnown("scripts/story/b.ks");
  scheduler.registerKnown("scripts/other/c.ks");
  scheduler.observe("scripts/story/current.ks");

  const Plan plan = scheduler.predict();
  std::size_t a = 0, b = 0;
  Require(Contains(plan, "scripts/story/a.ks", &a), "sibling a included");
  Require(Contains(plan, "scripts/story/b.ks", &b), "sibling b included");
  Require(!Contains(plan, "scripts/other/c.ks"),
          "node from another directory excluded");
  Require(a < b, "siblings ordered deterministically");
}

void testSequentialHeuristic() {
  Scheduler scheduler;
  scheduler.observe("scenes/scene07");
  const Plan plan = scheduler.predict();
  Require(!plan.empty() && plan.nodes[0] == "scenes/scene08",
          "next sequential id first, zero padding preserved");
  Require(Contains(plan, "scenes/scene09"), "sequential +2 included");
  Require(Contains(plan, "scenes/scene06"), "sequential -1 included");
}

void testNativeBehaviorIsNoOp() {
  // On native builds the only backend is genuinely inert: disabled, no HTTP.
  WasmBackend wasm;
  Require(!wasm.enabled(), "WASM backend disabled on native builds");
  wasm.beginDispatch();
  wasm.prefetch("any-node");
  wasm.endDispatch();
  Require(wasm.issued() == 0, "native backend issues no requests");

  Scheduler scheduler;
  scheduler.setBackend(&wasm);
  scheduler.registerHint("root", "next", 1.0f);
  scheduler.observe("root");
  scheduler.prefetch(); // compiled to a no-op on native

  // The generic dispatch path is still testable with a recording backend.
  RecordingBackend recorder;
  scheduler.setBackend(&recorder);
  scheduler.dispatch();
  Require(recorder.ids.size() == 1 && recorder.ids[0] == "next",
          "dispatch forwards the predicted plan to the backend");
  Require(recorder.begins == 1 && recorder.ends == 1,
          "dispatch brackets the burst with lifecycle hooks");
}

// Exercises the real cross-port path: Runtime::update() resolves the current
// debug node and publishes one transition through
// publishPredictiveNode() -> Scheduler::observeAndPrefetch().
class RuntimePrefetchProbe final : public Kotonoha::Runtime {
public:
  using Kotonoha::Runtime::Runtime;
  using Kotonoha::Runtime::publishPredictiveNode;
  using Kotonoha::Runtime::registerPrefetchGraph;

  void attach(Backend *backend) { prefetchScheduler_.setBackend(backend); }
  void dispatch() { prefetchScheduler_.dispatch(); }
  std::size_t transitions() const noexcept { return predictiveTransitions_; }
  std::string current() const { return prefetchScheduler_.current(); }
  bool hasCurrent() const noexcept { return prefetchScheduler_.hasCurrent(); }

  std::string debugNode() const override { return node_; }
  void setNode(std::string node) { node_ = std::move(node); }

private:
  std::string node_;
};

void testRuntimeObserveAndPrefetchOnNodeChange() {
  RuntimePrefetchProbe runtime("prefetch", "1", "prefetch");
  RecordingBackend recorder;
  runtime.attach(&recorder);

  // The graph is what KotonohaPreloadCache.js::prefetchGraph() exports:
  // node-A uses assets/a.bin and points (weighted) to node-B, and vice versa.
  runtime.registerPrefetchGraph("N\tnode-A\nA\tassets/a.bin\n"
                                "X\tnode-B\t1.5\n"
                                "N\tnode-B\nA\tassets/b.bin\n"
                                "X\tnode-A\t1\n");
  runtime.SetState(Kotonoha::GameState::Gameplay);

  // A raw debug label that is not a preload.json key is never observed.
  runtime.setNode("8005:1505");
  runtime.update();
  Require(runtime.transitions() == 0 && !runtime.hasCurrent(),
          "unrecognized debug node must not reach the Scheduler");

  runtime.setNode("node-A");
  runtime.update();
  Require(runtime.transitions() == 1,
          "Runtime::update publishes the recognized node transition");
  Require(runtime.hasCurrent() && runtime.current() == "node-A",
          "observeAndPrefetch() observed node-A on the transition");

  runtime.dispatch();
  Require(recorder.ids.size() == 2 && recorder.ids[0] == "assets/b.bin" &&
              recorder.ids[1] == "assets/a.bin",
          "graph hints dispatch only manifest asset ids, weighted by next");

  runtime.update(); // same node: deduped by lastPredictiveNode_
  Require(runtime.transitions() == 1, "unchanged node is not re-observed");

  runtime.setNode("node-B");
  runtime.update();
  Require(runtime.transitions() == 2 && runtime.current() == "node-B",
          "the following node is observed exactly once");
}

// Concrete integration example: a port reports VM nodes and feeds hints from
// its own manifest. No game/VM source file needs to change for this to work.
void testPortIntegrationExample() {
  Scheduler portScheduler;
  RecordingBackend portBackend;
  portScheduler.setBackend(&portBackend);

  // In a real port these would be read from a per-game JSON manifest.
  portScheduler.registerHints("title", {
                                           {"stage1", 4.0f},
                                           {"gallery", 1.0f},
                                       });
  portScheduler.registerKnown("stage1");
  portScheduler.registerKnown("stage2");

  // Called by the port's iterate() when the VM changes scene. Real WASM ports
  // call prefetch(); this native test uses dispatch() to observe the plan.
  portScheduler.observe("title");
  portScheduler.dispatch();

  Require(portBackend.ids.size() == 2 && portBackend.ids[0] == "stage1" &&
              portBackend.ids[1] == "gallery",
          "port integration dispatches the ordered, data-driven plan");
}

} // namespace

int main() {
  testWeightsOrderingDedupAndBound();
  testTierPriorityAndLearning();
  testSiblingHeuristic();
  testSequentialHeuristic();
  testNativeBehaviorIsNoOp();
  testRuntimeObserveAndPrefetchOnNodeChange();
  testPortIntegrationExample();

  std::cout << "PrefetchTests PASS\n";
  return 0;
}
