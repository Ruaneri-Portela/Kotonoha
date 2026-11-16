// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006 Peter Jolly
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#ifndef SRC_LIBREALLIVE_ALLDEFS_H_
#define SRC_LIBREALLIVE_ALLDEFS_H_

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using std::string;

namespace libreallive {

inline void insert_i16(string &dest, int dpos, const int i16) {
  dest[dpos++] = i16 & 0xff;
  dest[dpos] = (i16 >> 8) & 0xff;
}

inline void insert_i32(string &dest, int dpos, const int i32) {
  dest[dpos++] = i32 & 0xff;
  dest[dpos++] = (i32 >> 8) & 0xff;
  dest[dpos++] = (i32 >> 16) & 0xff;
  dest[dpos] = (i32 >> 24) & 0xff;
}

inline void insert_i32(char *dest, const int i32) {
  *dest++ = i32 & 0xff;
  *dest++ = (i32 >> 8) & 0xff;
  *dest++ = (i32 >> 16) & 0xff;
  *dest = (i32 >> 24) & 0xff;
}

inline void append_i16(string &dest, const int i16) {
  size_t dpos = dest.size();
  dest.resize(dpos + 2, i16 & 0xff);
  dest[++dpos] = (i16 >> 8) & 0xff;
}

inline void append_i32(string &dest, const int i32) {
  size_t dpos = dest.size();
  dest.resize(dpos + 4, i32 & 0xff);
  dest[++dpos] = (i32 >> 8) & 0xff;
  dest[++dpos] = (i32 >> 16) & 0xff;
  dest[++dpos] = (i32 >> 24) & 0xff;
}

inline int read_i16(const char *src) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(src);
  return static_cast<int>(bytes[0]) | (static_cast<int>(bytes[1]) << 8);
}

inline int read_i16(const string &src, const int spos) {
  return read_i16(src.data() + spos);
}

inline long read_i32(const char *src) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(src);
  const std::uint32_t value =
      std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
      (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);

  // Decode the signed 32-bit value without relying on signed char or
  // implementation-defined unsigned-to-signed narrowing conversions.
  return value >= 0x80000000u
             ? static_cast<long>(static_cast<std::int64_t>(value) -
                                 0x100000000LL)
             : static_cast<long>(value);
}

inline long read_i32(const string &src, const int spos) {
  return read_i32(src.data() + spos);
}

class Error : public std::exception {
public:
  explicit Error(string what) : description(what) {}
  virtual ~Error() throw() {}

  virtual const char *what() const throw() { return description.c_str(); }

private:
  string description;
};

} // namespace libreallive

#endif // SRC_LIBREALLIVE_ALLDEFS_H_
