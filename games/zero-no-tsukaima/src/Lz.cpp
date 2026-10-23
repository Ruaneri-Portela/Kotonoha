#include <ZeroNoTsukaima/Lz.hpp>

#include <algorithm>
#include <array>

namespace ZeroNoTsukaima {

bool ReadPackedHeader(const uint8_t *data, size_t size, PackedHeader *out) {
  if (data == nullptr || out == nullptr || size < 8) return false;
  out->size = data[0] | (data[1] << 8) | (data[2] << 16) |
              (static_cast<uint32_t>(data[3]) << 24);
  out->mode = data[4] | (data[5] << 8) | (data[6] << 16) |
              (static_cast<uint32_t>(data[7]) << 24);
  return true;
}

bool Unpack(const uint8_t *data, size_t size, std::vector<uint8_t> *out) {
  PackedHeader header;
  if (out == nullptr || !ReadPackedHeader(data, size, &header)) return false;
  out->clear();
  out->reserve(header.size);

  // Index == size marks the virtual "repeat 0x00" command.
  const size_t zeroCommand = size;
  const auto commandByte = [&](size_t at, size_t offset) -> uint8_t {
    if (at == zeroCommand) return offset == 0 ? 0xC0 : 0x00;
    return at + offset < size ? data[at + offset] : 0;
  };
  std::array<size_t, 6> mru;
  mru.fill(zeroCommand);

  const auto push = [&](size_t at) {
    std::rotate(mru.rbegin(), mru.rbegin() + 1, mru.rend());
    mru[0] = at;
  };
  const auto emit = [&](uint8_t byte, size_t count) {
    const size_t room = header.size - out->size();
    out->insert(out->end(), std::min(count, room), byte);
  };

  size_t pos = 8;
  while (out->size() < header.size) {
    if (pos >= size) return false;
    const uint8_t cmd = data[pos];
    const unsigned kind = cmd >> 5;
    const size_t n = cmd & 0x1F;
    if (kind == 6) {
      if (pos + 1 >= size) return false;
      emit(data[pos + 1], n + 2);
      push(pos);
      pos += 2;
    } else if (kind == 7) {
      if (pos + 1 + n >= size) return false;
      const size_t room = header.size - out->size();
      const size_t count = std::min(n + 1, room);
      out->insert(out->end(), data + pos + 1, data + pos + 1 + count);
      push(pos);
      pos += 2 + n;
    } else {
      const size_t target = mru[kind];
      std::rotate(mru.begin(), mru.begin() + kind, mru.begin() + kind + 1);
      ++pos;
      const uint8_t old = commandByte(target, 0);
      if (old >> 5 == 6) {
        emit(commandByte(target, 1), n + 2);
      } else if (old >> 5 == 7) {
        const size_t skip = n >> 2;
        const size_t count = (n & 3) + 1;
        const size_t room = header.size - out->size();
        for (size_t i = 0; i < std::min(count, room); ++i) {
          out->push_back(commandByte(target, 1 + skip + i));
        }
      }
    }
  }
  if (out->size() != header.size) return false;

  // Second stage: bytes were stored as `stride` planes of N=size/stride bytes
  // (routine at 0x10fa30); transpose them back to interleaved order.
  const size_t stride = header.mode;
  if (stride > 1) {
    const size_t rows = header.size / stride;
    std::vector<uint8_t> plain(header.size);
    size_t o = 0;
    for (size_t j = 0; j < rows; ++j) {
      for (size_t k = 0; k < stride; ++k) plain[o++] = (*out)[j + k * rows];
    }
    for (size_t i = rows * stride; i < header.size; ++i) plain[i] = (*out)[i];
    *out = std::move(plain);
  }
  return true;
}

} // namespace ZeroNoTsukaima
