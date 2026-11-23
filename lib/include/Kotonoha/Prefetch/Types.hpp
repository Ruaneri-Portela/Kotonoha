#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha::Prefetch {

// Opaque logical identifier: a script path, scene label or asset id. The core
// never interprets its contents beyond neutral string operations.
using NodeId = std::string;

// Higher weight means "more likely". Registered hints use this directly.
using Weight = float;

struct Hint {
  NodeId next;
  Weight weight = 1.0f;
};

struct Plan {
  std::vector<NodeId> nodes;
  bool empty() const noexcept { return nodes.empty(); }
  std::size_t size() const noexcept { return nodes.size(); }
};

constexpr std::size_t kDefaultMaxNodes = 8;
constexpr std::size_t kDefaultRecencyCapacity = 16;
constexpr std::uint64_t kDefaultRangeBytes = 4096;
// FetchAssetProvider::prefetchRange() rejects ranges above 1 MiB.
constexpr std::uint64_t kMaxRangeBytes = 1u << 20;
// Mirrors the in-flight cap inside FetchAssetProvider::pump() (one origin).
constexpr std::size_t kMaxConcurrentPerOrigin = 4;

} // namespace Kotonoha::Prefetch
