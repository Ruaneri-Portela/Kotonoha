#pragma once
#include <utility>
namespace Kotonoha {
// Value-owning adapter for compatibility structs with an existing cleanup API.
// get() borrows the value; release() explicitly transfers ownership.
template <class T, void (*Cleanup)(T *)> class Resource {
  T value{};

public:
  Resource() = default;
  explicit Resource(T owned) : value(owned) {}
  ~Resource() { Cleanup(&value); }
  Resource(const Resource &) = delete;
  Resource &operator=(const Resource &) = delete;
  Resource(Resource &&other) noexcept : value(other.release()) {}
  Resource &operator=(Resource &&other) noexcept {
    if (this != &other) {
      Cleanup(&value);
      value = other.release();
    }
    return *this;
  }
  T &get() { return value; }
  const T &get() const { return value; }
  T release() noexcept { return std::exchange(value, T{}); }
};
} // namespace Kotonoha
