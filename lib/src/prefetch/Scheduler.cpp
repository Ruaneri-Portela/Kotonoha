#include <Kotonoha/Prefetch/Scheduler.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace Kotonoha::Prefetch {
namespace {

constexpr Weight kMaxLearnedWeight = 32.0f;

// Directory prefix including the trailing slash, or an empty string.
std::string parentPath(const NodeId &id) {
  const auto slash = id.rfind('/');
  return slash == std::string::npos ? std::string{} : id.substr(0, slash + 1);
}

// Recognises "scene07" -> ("scene", 7, 2) for the sequential heuristic.
bool splitTrailingNumber(const NodeId &id, std::string &prefix,
                         std::uint64_t &number, std::size_t &width) {
  std::size_t end = id.size();
  while (end > 0 && id[end - 1] >= '0' && id[end - 1] <= '9')
    --end;
  if (end == id.size())
    return false;
  constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
  number = 0;
  for (std::size_t i = end; i < id.size(); ++i) {
    const auto digit = static_cast<std::uint64_t>(id[i] - '0');
    if (number > (kMax - digit) / 10)
      return false;
    number = number * 10 + digit;
  }
  prefix = id.substr(0, end);
  width = id.size() - end;
  return true;
}

NodeId makeNumbered(const std::string &prefix, std::uint64_t number,
                    std::size_t width) {
  std::string digits = std::to_string(number);
  if (digits.size() < width)
    digits.insert(0, width - digits.size(), '0');
  return prefix + digits;
}

} // namespace

Scheduler::Scheduler() : Scheduler(Config()) {}

Scheduler::Scheduler(Config config) : config_(config) {}

void Scheduler::registerHint(NodeId from, NodeId to, Weight weight) {
  if (from.empty() || to.empty() || from == to)
    return;
  auto &list = hints_[std::move(from)];
  for (auto &hint : list) {
    if (hint.next == to) {
      hint.weight = std::max(hint.weight, weight);
      return;
    }
  }
  list.push_back(Hint{std::move(to), weight});
}

void Scheduler::registerHints(NodeId from, std::initializer_list<Hint> hints) {
  for (const Hint &hint : hints)
    registerHint(from, hint.next, hint.weight);
}

void Scheduler::registerKnown(NodeId id) {
  if (id.empty())
    return;
  if (knownSet_.insert(id).second)
    known_.push_back(std::move(id));
}

void Scheduler::addLearned(const NodeId &from, const NodeId &to) {
  if (from.empty() || to.empty() || from == to)
    return;
  auto &list = learned_[from];
  for (auto &hint : list) {
    if (hint.next == to) {
      hint.weight = std::min(kMaxLearnedWeight, hint.weight + 1.0f);
      return;
    }
  }
  list.push_back(Hint{to, 1.0f});
}

void Scheduler::observe(NodeId current) {
  if (current.empty())
    return;
  if (hasCurrent_ && config_.learnFromObservation)
    addLearned(current_, current);
  // Visited nodes implicitly feed the sibling heuristic.
  registerKnown(current);

  const auto found = std::find(recent_.begin(), recent_.end(), current);
  if (found != recent_.end())
    recent_.erase(found);
  recent_.insert(recent_.begin(), current);
  if (recent_.size() > config_.recencyCapacity)
    recent_.pop_back();

  current_ = std::move(current);
  hasCurrent_ = true;
}

void Scheduler::addCandidate(const NodeId &id, unsigned tier,
                             Weight weight) const {
  if (id.empty() || id == current_)
    return;
  const auto found = index_.find(id);
  if (found != index_.end()) {
    Candidate &existing = scratch_[found->second];
    if (tier < existing.tier ||
        (tier == existing.tier && weight > existing.weight)) {
      existing.tier = tier;
      existing.weight = weight;
    }
    return;
  }
  scratch_.push_back(Candidate{id, tier, weight});
  index_.emplace(scratch_.back().id, scratch_.size() - 1);
}

Plan Scheduler::predict() const {
  Plan plan;
  if (!hasCurrent_ || !config_.maxNodes)
    return plan;

  scratch_.clear();
  index_.clear();
  plan.nodes.reserve(config_.maxNodes);

  // Tier 0: registered, data-driven hints (most authoritative).
  const auto registered = hints_.find(current_);
  if (registered != hints_.end())
    for (const Hint &hint : registered->second)
      addCandidate(hint.next, 0, hint.weight);

  // Tier 1: transitions learned from the actual observation order.
  const auto learned = learned_.find(current_);
  if (learned != learned_.end())
    for (const Hint &hint : learned->second)
      addCandidate(hint.next, 1, hint.weight);

  // Tier 2: recently visited nodes, newest first, decaying weight.
  for (std::size_t i = 0; i < recent_.size(); ++i) {
    const Weight weight = 1.0f / (1.0f + static_cast<Weight>(i));
    addCandidate(recent_[i], 2, weight);
  }

  // Tier 3: sibling nodes in the same directory.
  if (config_.siblingsEnabled) {
    const std::string dir = parentPath(current_);
    if (!dir.empty())
      for (const NodeId &id : known_)
        if (id.compare(0, dir.size(), dir) == 0)
          addCandidate(id, 3, 1.0f);
  }

  // Tier 4: sequential numeric ids ("scene07" -> "scene08/09/06").
  if (config_.sequentialEnabled) {
    std::string prefix;
    std::uint64_t number = 0;
    std::size_t width = 0;
    if (splitTrailingNumber(current_, prefix, number, width)) {
      constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
      if (number < kMax)
        addCandidate(makeNumbered(prefix, number + 1, width), 4, 1.0f);
      if (number <= kMax - 2)
        addCandidate(makeNumbered(prefix, number + 2, width), 4, 0.5f);
      if (number > 0)
        addCandidate(makeNumbered(prefix, number - 1, width), 4, 0.25f);
    }
  }

  std::sort(scratch_.begin(), scratch_.end(),
            [](const Candidate &a, const Candidate &b) {
              if (a.tier != b.tier)
                return a.tier < b.tier;
              if (a.weight != b.weight)
                return a.weight > b.weight;
              return a.id < b.id;
            });

  const auto count = std::min(config_.maxNodes, scratch_.size());
  for (std::size_t i = 0; i < count; ++i)
    plan.nodes.push_back(scratch_[i].id);
  return plan;
}

void Scheduler::dispatch() {
  if (!backend_ || !backend_->enabled() || !hasCurrent_)
    return;
  const Plan plan = predict();
  if (plan.empty())
    return;
  backend_->beginDispatch();
  for (const NodeId &id : plan.nodes)
    backend_->prefetch(id);
  backend_->endDispatch();
}

void Scheduler::prefetch() {
#if defined(__EMSCRIPTEN__)
  dispatch();
#else
  // Native builds: intentionally empty. Prefetching is a Web-only concern.
#endif
}

void Scheduler::clear() {
  current_.clear();
  hasCurrent_ = false;
  hints_.clear();
  learned_.clear();
  known_.clear();
  knownSet_.clear();
  recent_.clear();
  scratch_.clear();
  index_.clear();
}

} // namespace Kotonoha::Prefetch
