#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Pmail::StateIO {

inline uint64_t fingerprint(const void *data, size_t size) {
  auto bytes = static_cast<const uint8_t *>(data);
  uint64_t hash = 14695981039346656037ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

class Writer {
  std::string data_;
public:
  void u32(uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data_.push_back(char(value >> (i * 8)));
  }
  void u64(uint64_t value) { u32(uint32_t(value)); u32(uint32_t(value >> 32)); }
  void boolean(bool value) { u32(value ? 1 : 0); }
  void string(const std::string &value) {
    if (value.size() > (16u << 20)) throw std::runtime_error("PMAIL state field too large");
    u32(uint32_t(value.size())); data_ += value;
  }
  std::string finish() {
    u64(fingerprint(data_.data(), data_.size()));
    return std::move(data_);
  }
};

class Reader {
  std::string_view data_;
  size_t position_ = 0;
public:
  explicit Reader(const std::string &data) : data_(data) {
    if (data.size() < 8 || data.size() > (32u << 20))
      throw std::runtime_error("Invalid PMAIL state size");
    uint64_t checksum = 0;
    for (unsigned i = 0; i < 8; ++i)
      checksum |= uint64_t(uint8_t(data[data.size() - 8 + i])) << (8 * i);
    data_ = data_.substr(0, data.size() - 8);
    if (fingerprint(data_.data(), data_.size()) != checksum)
      throw std::runtime_error("PMAIL save checksum mismatch");
  }
  uint32_t u32() {
    if (position_ + 4 > data_.size()) throw std::runtime_error("Truncated PMAIL state");
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= uint32_t(uint8_t(data_[position_++])) << (i * 8);
    return value;
  }
  uint64_t u64() { const uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
  uint32_t count(uint32_t limit) {
    const auto value = u32();
    if (value > limit) throw std::runtime_error("Invalid PMAIL state count");
    return value;
  }
  bool boolean() { return count(1) != 0; }
  std::string string(uint32_t limit = 1u << 20) {
    const auto size = count(limit);
    if (size > data_.size() - position_) throw std::runtime_error("Truncated PMAIL string");
    std::string out(data_.substr(position_, size)); position_ += size; return out;
  }
  void end() const {
    if (position_ != data_.size()) throw std::runtime_error("Trailing PMAIL state data");
  }
};
} // namespace Pmail::StateIO
