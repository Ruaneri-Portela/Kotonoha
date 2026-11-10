// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "libreallive/intmemref.h"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
class RLMemory {
public:
  std::map<std::pair<int, int>, std::int32_t> integers;
  std::map<std::pair<int, int>, std::string> strings;
  int GetIntValue(const libreallive::IntMemRef &r) const {
    if (r.location() < 0 || r.type() < 0 || r.type() > 4)
      throw std::out_of_range("RealLive integer reference");
    int bits = r.type() ? 1 << (r.type() - 1) : 32;
    int slots = 32 / bits;
    auto it = integers.find({r.bank(), r.location() / slots});
    std::uint32_t v = it == integers.end() ? 0 : it->second;
    return bits == 32 ? static_cast<std::int32_t>(v)
                      : int((v >> ((r.location() % slots) * bits)) &
                            ((1u << bits) - 1));
  }
  void SetIntValue(const libreallive::IntMemRef &r, int value) {
    if (r.location() < 0 || r.type() < 0 || r.type() > 4)
      throw std::out_of_range("RealLive integer reference");
    int bits = r.type() ? 1 << (r.type() - 1) : 32;
    int slots = 32 / bits;
    auto &v = integers[{r.bank(), r.location() / slots}];
    if (bits == 32)
      v = value;
    else {
      unsigned shift = (r.location() % slots) * bits, mask = ((1u << bits) - 1)
                                                             << shift;
      v = (std::uint32_t(v) & ~mask) | ((std::uint32_t(value) << shift) & mask);
    }
  }
  const std::string &GetStringValue(int bank, int index) {
    return strings[{bank, index}];
  }
  void SetStringValue(int bank, int index, const std::string &v) {
    strings[{bank, index}] = v;
  }
};
class IntReferenceIterator {
  RLMemory *memory_ = nullptr;
  int *direct_ = nullptr;
  int bank_ = 0, index_ = 0;

public:
  class Reference {
    RLMemory *m_;
    int *p_;
    int b_, i_;

  public:
    Reference(RLMemory *m, int *p, int b, int i) : m_(m), p_(p), b_(b), i_(i) {}
    operator int() const {
      return p_ ? *p_ : m_->GetIntValue(libreallive::IntMemRef(b_, i_));
    }
    Reference &operator=(int v) {
      if (p_)
        *p_ = v;
      else
        m_->SetIntValue(libreallive::IntMemRef(b_, i_), v);
      return *this;
    }
  };
  explicit IntReferenceIterator(int *p) : direct_(p) {}
  IntReferenceIterator(RLMemory *m, int b, int i)
      : memory_(m), bank_(b), index_(i) {}
  // Identifies the destination bank/index so ports can recognise the native
  // input registers without hardcoding a title-specific slot.
  int bank() const { return memory_ ? bank_ : -1; }
  int index() const { return memory_ ? index_ : -1; }
  Reference operator*() const {
    return Reference(memory_, direct_, bank_, index_);
  }
  IntReferenceIterator &operator++() {
    if (direct_)
      ++direct_;
    else
      ++index_;
    return *this;
  }
  IntReferenceIterator operator+(int n) const {
    auto r = *this;
    if (r.direct_)
      r.direct_ += n;
    else
      r.index_ += n;
    return r;
  }
  bool operator==(const IntReferenceIterator &r) const {
    return memory_ == r.memory_ && direct_ == r.direct_ && bank_ == r.bank_ &&
           index_ == r.index_;
  }
  bool operator!=(const IntReferenceIterator &r) const { return !(*this == r); }
};
class StringReferenceIterator {
  RLMemory *memory_;
  int bank_, index_;

public:
  StringReferenceIterator(RLMemory *m, int b, int i)
      : memory_(m), bank_(b), index_(i) {}
  std::string &operator*() const { return memory_->strings[{bank_, index_}]; }
  StringReferenceIterator &operator++() {
    ++index_;
    return *this;
  }
  StringReferenceIterator operator+(int n) const {
    return StringReferenceIterator(memory_, bank_, index_ + n);
  }
  bool operator==(const StringReferenceIterator &r) const {
    return memory_ == r.memory_ && bank_ == r.bank_ && index_ == r.index_;
  }
  bool operator!=(const StringReferenceIterator &r) const {
    return !(*this == r);
  }
};
