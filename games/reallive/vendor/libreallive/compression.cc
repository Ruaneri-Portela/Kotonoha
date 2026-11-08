// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006, 2007 Peter Jolly
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

#include "libreallive/compression.h"

#include <string>

namespace libreallive {
namespace compression {

/* RealLive uses a rather basic XOR encryption scheme, to which this
 * is the key. */
static char xor_mask[256] = {
    static_cast<char>(0x8b), static_cast<char>(0xe5), 0x5d, static_cast<char>(0xc3), static_cast<char>(0xa1), static_cast<char>(0xe0), 0x30, 0x44, 0x00, static_cast<char>(0x85), static_cast<char>(0xc0), 0x74,
    0x09, 0x5f, 0x5e, 0x33, static_cast<char>(0xc0), 0x5b, static_cast<char>(0x8b), static_cast<char>(0xe5), 0x5d, static_cast<char>(0xc3), static_cast<char>(0x8b), 0x45,
    0x0c, static_cast<char>(0x85), static_cast<char>(0xc0), 0x75, 0x14, static_cast<char>(0x8b), 0x55, static_cast<char>(0xec), static_cast<char>(0x83), static_cast<char>(0xc2), 0x20, 0x52,
    0x6a, 0x00, static_cast<char>(0xe8), static_cast<char>(0xf5), 0x28, 0x01, 0x00, static_cast<char>(0x83), static_cast<char>(0xc4), 0x08, static_cast<char>(0x89), 0x45,
    0x0c, static_cast<char>(0x8b), 0x45, static_cast<char>(0xe4), 0x6a, 0x00, 0x6a, 0x00, 0x50, 0x53, static_cast<char>(0xff), 0x15,
    0x34, static_cast<char>(0xb1), 0x43, 0x00, static_cast<char>(0x8b), 0x45, 0x10, static_cast<char>(0x85), static_cast<char>(0xc0), 0x74, 0x05, static_cast<char>(0x8b),
    0x4d, static_cast<char>(0xec), static_cast<char>(0x89), 0x08, static_cast<char>(0x8a), 0x45, static_cast<char>(0xf0), static_cast<char>(0x84), static_cast<char>(0xc0), 0x75, 0x78, static_cast<char>(0xa1),
    static_cast<char>(0xe0), 0x30, 0x44, 0x00, static_cast<char>(0x8b), 0x7d, static_cast<char>(0xe8), static_cast<char>(0x8b), 0x75, 0x0c, static_cast<char>(0x85), static_cast<char>(0xc0),
    0x75, 0x44, static_cast<char>(0x8b), 0x1d, static_cast<char>(0xd0), static_cast<char>(0xb0), 0x43, 0x00, static_cast<char>(0x85), static_cast<char>(0xff), 0x76, 0x37,
    static_cast<char>(0x81), static_cast<char>(0xff), 0x00, 0x00, 0x04, 0x00, 0x6a, 0x00, 0x76, 0x43, static_cast<char>(0x8b), 0x45,
    static_cast<char>(0xf8), static_cast<char>(0x8d), 0x55, static_cast<char>(0xfc), 0x52, 0x68, 0x00, 0x00, 0x04, 0x00, 0x56, 0x50,
    static_cast<char>(0xff), 0x15, 0x2c, static_cast<char>(0xb1), 0x43, 0x00, 0x6a, 0x05, static_cast<char>(0xff), static_cast<char>(0xd3), static_cast<char>(0xa1), static_cast<char>(0xe0),
    0x30, 0x44, 0x00, static_cast<char>(0x81), static_cast<char>(0xef), 0x00, 0x00, 0x04, 0x00, static_cast<char>(0x81), static_cast<char>(0xc6), 0x00,
    0x00, 0x04, 0x00, static_cast<char>(0x85), static_cast<char>(0xc0), 0x74, static_cast<char>(0xc5), static_cast<char>(0x8b), 0x5d, static_cast<char>(0xf8), 0x53, static_cast<char>(0xe8),
    static_cast<char>(0xf4), static_cast<char>(0xfb), static_cast<char>(0xff), static_cast<char>(0xff), static_cast<char>(0x8b), 0x45, 0x0c, static_cast<char>(0x83), static_cast<char>(0xc4), 0x04, 0x5f, 0x5e,
    0x5b, static_cast<char>(0x8b), static_cast<char>(0xe5), 0x5d, static_cast<char>(0xc3), static_cast<char>(0x8b), 0x55, static_cast<char>(0xf8), static_cast<char>(0x8d), 0x4d, static_cast<char>(0xfc), 0x51,
    0x57, 0x56, 0x52, static_cast<char>(0xff), 0x15, 0x2c, static_cast<char>(0xb1), 0x43, 0x00, static_cast<char>(0xeb), static_cast<char>(0xd8), static_cast<char>(0x8b),
    0x45, static_cast<char>(0xe8), static_cast<char>(0x83), static_cast<char>(0xc0), 0x20, 0x50, 0x6a, 0x00, static_cast<char>(0xe8), 0x47, 0x28, 0x01,
    0x00, static_cast<char>(0x8b), 0x7d, static_cast<char>(0xe8), static_cast<char>(0x89), 0x45, static_cast<char>(0xf4), static_cast<char>(0x8b), static_cast<char>(0xf0), static_cast<char>(0xa1), static_cast<char>(0xe0), 0x30,
    0x44, 0x00, static_cast<char>(0x83), static_cast<char>(0xc4), 0x08, static_cast<char>(0x85), static_cast<char>(0xc0), 0x75, 0x56, static_cast<char>(0x8b), 0x1d, static_cast<char>(0xd0),
    static_cast<char>(0xb0), 0x43, 0x00, static_cast<char>(0x85), static_cast<char>(0xff), 0x76, 0x49, static_cast<char>(0x81), static_cast<char>(0xff), 0x00, 0x00, 0x04,
    0x00, 0x6a, 0x00, 0x76};

/* In some new titles, a second round of XORing is performed on a block of
 * uncompressed bytecode. The keys appear to be on a game-by-game basis. */
const XorKey little_busters_xor_mask[] = {
    {{static_cast<char>(0xa8), 0x28, static_cast<char>(0xfd), 0x66, static_cast<char>(0xa0), 0x23, 0x77, 0x69, static_cast<char>(0xf9), 0x45, static_cast<char>(0xf8), 0x2c,
      0x7c, 0x00, static_cast<char>(0xad), static_cast<char>(0xf4)},
     256, 257},
    {{0x0}, -1, -1}};

const XorKey little_busters_ex_xor_mask[] = {
    {{static_cast<char>(0xa8), 0x28, static_cast<char>(0xfd), 0x71, static_cast<char>(0xb4), 0x23, 0x64, 0x15, static_cast<char>(0x96), 0x48, static_cast<char>(0x8a), 0x43,
      0x62, 0x0e, static_cast<char>(0xad), static_cast<char>(0xf0)},
     256, 128},
    {{static_cast<char>(0xde), static_cast<char>(0xd9), 0x4a, 0x18, static_cast<char>(0xaf), 0x23, 0x1d, static_cast<char>(0x9a), static_cast<char>(0xac), 0x23, 0x25, 0x48,
      static_cast<char>(0xd8), static_cast<char>(0xd4), static_cast<char>(0x8f), static_cast<char>(0xa7)},
     384, 128},
    {{static_cast<char>(0xde), static_cast<char>(0xf1), static_cast<char>(0xb7), 0x69, 0x1b, 0x00, 0x79, static_cast<char>(0x8f), 0x3a, 0x6b, static_cast<char>(0xaf), 0x0b,
      static_cast<char>(0xba), static_cast<char>(0xda), 0x22, 0x57},
     512, 16},
    {{0x76, static_cast<char>(0xf1), static_cast<char>(0xb7), 0x69, 0x1b, 0x00, 0x79, static_cast<char>(0x8f), 0x3a, 0x6b, static_cast<char>(0xaf), 0x0b,
      static_cast<char>(0xba), static_cast<char>(0xda), 0x22, 0x57},
     528, 113},
    {{0x0}, -1, -1}};

const XorKey clannad_full_voice_xor_mask[] = {
    {{static_cast<char>(0xAF), 0x2F, static_cast<char>(0xFB), 0x6B, static_cast<char>(0xAF), 0x30, 0x77, 0x17, static_cast<char>(0x87), 0x48, static_cast<char>(0xFE), 0x2C,
      0x68, 0x1A, static_cast<char>(0xB9), static_cast<char>(0xF0)},
     256, 257},
    {{0x0}, -1, -1}};

const XorKey snow_standard_edition_xor_mask[] = {
    {{static_cast<char>(0xe4), static_cast<char>(0xab), static_cast<char>(0xa2), static_cast<char>(0xc9), static_cast<char>(0xec), 0x39, 0x36, 0x62, static_cast<char>(0xc9), 0x03, static_cast<char>(0xba), 0x6d,
      0x2e, static_cast<char>(0x9c), static_cast<char>(0xf2), 0x64},
     256, 257},
    {{0x0}, -1, -1}};

const XorKey kud_wafter_xor_mask[] = {
    {{0x67, 0x1c, 0x21, static_cast<char>(0xbe), 0x6f, static_cast<char>(0xef), static_cast<char>(0xb5), 0x16, 0x4a, static_cast<char>(0x82), 0x39, 0x2b,
      static_cast<char>(0xad), 0x3a, 0x71, 0x3f},
     256, 257},
    {{0x0}, -1, -1}};

const XorKey kud_wafter_all_ages_xor_mask[] = {
    {{static_cast<char>(0xaf), 0x3f, static_cast<char>(0xe6), 0x63, static_cast<char>(0xad), 0x3a, 0x69, 0x18, static_cast<char>(0x85), 0x45, static_cast<char>(0xe5), 0x40,
      0x1e, 0x7e, static_cast<char>(0xb9), static_cast<char>(0xe0)},
     256, 257},
    {{0x0}, -1, -1}};


// -----------------------------------------------------------------------

// Decompress an archived file.
void Decompress(const char* src,
                size_t src_len,
                char* dst,
                size_t dst_len,
                const XorKey* per_game_xor_key) {
  int bit = 1;
  const char* srcend = src + src_len;
  char* dststart = dst;
  char* dstend = dst + dst_len;
  src += 8;
  unsigned char mask = 8;
  char flag = *src++ ^ xor_mask[mask++];
  while (src < srcend && dst < dstend) {
    if (bit == 256) {
      bit = 1;
      flag = *src++ ^ xor_mask[mask++];
    }
    if (flag & bit) {
      *dst++ = *src++ ^ xor_mask[mask++];
    } else {
      char* repeat;
      int count = *src++ ^ xor_mask[mask++];
      count += (*src++ ^ xor_mask[mask++]) << 8;
      repeat = dst - ((count >> 4) - 1) - 1;
      count = (count & 0x0f) + 2;
      if (repeat < dststart || repeat >= dst)
        throw Error("corrupt data");
      for (int i = 0; i < count; i++)
        *dst++ = *repeat++;
    }
    bit <<= 1;
  }

  if (per_game_xor_key) {
    for (; per_game_xor_key->xor_offset != -1; per_game_xor_key++) {
      dst = dststart + per_game_xor_key->xor_offset;
      for (int i = 0; i < per_game_xor_key->xor_length && dst < dstend; ++i) {
        *dst++ ^= per_game_xor_key->xor_key[i % 16];
      }
    }
  }
}

}  // namespace compression
}  // namespace libreallive
