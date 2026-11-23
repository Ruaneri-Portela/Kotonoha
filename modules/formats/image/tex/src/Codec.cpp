// CMP byte codecs: LZSS-style decompressor, UTF-16 conversion and key path
// canonicalisation shared by archive and image format readers.
#include <SummerRadish/Codec.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace SummerRadish {
namespace {

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

void appendUtf8(std::string &out, uint32_t c) {
  if (c < 0x80)
    out += char(c);
  else if (c < 0x800) {
    out += char(0xc0 | (c >> 6));
    out += char(0x80 | (c & 63));
  } else if (c < 0x10000) {
    out += char(0xe0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 63));
    out += char(0x80 | (c & 63));
  } else {
    out += char(0xf0 | (c >> 18));
    out += char(0x80 | ((c >> 12) & 63));
    out += char(0x80 | ((c >> 6) & 63));
    out += char(0x80 | (c & 63));
  }
}

} // namespace

uint32_t word(const Bytes &d, size_t p) {
  require(p <= d.size() && d.size() - p >= 4, "Truncated data word");
  return uint32_t(d[p]) | uint32_t(d[p + 1]) << 8 | uint32_t(d[p + 2]) << 16 |
         uint32_t(d[p + 3]) << 24;
}

Bytes decompress(const Bytes &d, size_t size, size_t p) {
  require(size <= (256u << 20), "CMP decompression exceeds allocation limit");
  Bytes out;
  out.reserve(size);
  while (out.size() < size) {
    require(p < d.size(), "Truncated CMP control");
    unsigned a = d[p++];
    if (a < 128) {
      size_t n = std::min<size_t>(a + 1, size - out.size());
      require(p <= d.size() && n <= d.size() - p, "Truncated CMP literal");
      out.insert(out.end(), d.begin() + p, d.begin() + p + n);
      p += n;
    } else {
      require(p < d.size(), "Truncated CMP reference");
      a = (a << 8) | d[p++];
      size_t n = std::min<size_t>(((a >> 10) & 30) + 2, size - out.size()),
             back = (a & 2047) + 1;
      require(back <= out.size(), "Invalid CMP back reference");
      while (n--)
        out.push_back(out[out.size() - back]);
    }
  }
  return out;
}

Bytes decodeStackWork(const Bytes &d) {
  if (d.size() >= 260 && std::memcmp(d.data(), "StackWorkFile1.0", 16) == 0)
    return decompress(d, word(d, 256), 260);
  return d;
}

std::string utf16(const Bytes &d, size_t p, size_t count) {
  require(p <= d.size() && count <= (d.size() - p) / 2,
          "Truncated UTF-16 string");
  std::string result;
  for (size_t i = 0; i < count; ++i) {
    uint32_t c = d[p + 2 * i] | unsigned(d[p + 2 * i + 1]) << 8;
    if (c >= 0xd800 && c <= 0xdbff) {
      require(++i < count, "Unpaired UTF-16 surrogate");
      uint32_t low = d[p + 2 * i] | unsigned(d[p + 2 * i + 1]) << 8;
      require(low >= 0xdc00 && low <= 0xdfff, "Invalid UTF-16 surrogate");
      c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
    } else
      require(c < 0xdc00 || c > 0xdfff, "Unpaired UTF-16 surrogate");
    appendUtf8(result, c);
  }
  return result;
}

std::string canonical(std::string s) {
  for (char &c : s) {
    if (c == '\\')
      c = '/';
    if (c >= 'a' && c <= 'z')
      c -= 32;
  }
  return s;
}

} // namespace SummerRadish
