#include <ZeroNoTsukaima/Disc.hpp>
#include <ZeroNoTsukaima/Lz.hpp>
#include <ZeroNoTsukaima/Tim2.hpp>

#include <cstdio>
#include <vector>

using namespace ZeroNoTsukaima;

static int failures = 0;
#define CHECK(c) \
  do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  // literal "TIM2" (+3), zero-run via MRU[1] (len 3), RLE 'A' x4, MRU[0] replay x2
  const std::vector<uint8_t> packed = {
      14, 0, 0, 0, 1, 0, 0, 0,
      0xE3, 'T', 'I', 'M', '2', // literal 4 bytes
      0x21,                     // MRU idx1 = zero run, n=1 -> 3 zero bytes
      0xC2, 'A',                // 'A' x4
      0x01,                     // MRU idx0 (the 'A' run), n=1 -> 3 more 'A'
  };
  std::vector<uint8_t> out;
  CHECK(Unpack(packed.data(), packed.size(), &out));
  const std::vector<uint8_t> expected = {'T', 'I', 'M', '2', 0, 0, 0,
                                         'A', 'A', 'A', 'A', 'A', 'A', 'A'};
  CHECK(out == expected);

  // truncated stream must fail instead of overrunning
  std::vector<uint8_t> truncated(packed.begin(), packed.begin() + 10);
  CHECK(!Unpack(truncated.data(), truncated.size(), &out));

  // archive index: entries land on 2048-byte boundaries
  const std::vector<uint8_t> hd = {0x10, 0, 0, 0, 0x01, 0x08, 0, 0, 5, 0, 0, 0};
  const auto index = ParseArchiveIndex(hd);
  CHECK(index.size() == 3);
  CHECK(index[1].offset == 2048 && index[2].offset == 2048 + 4096);

  // 2x1 8-bit TIM2 with a 256-entry 32-bit CLUT (CSM1)
  std::vector<uint8_t> tim(16 + 48, 0);
  tim[0] = 'T'; tim[1] = 'I'; tim[2] = 'M'; tim[3] = '2'; tim[4] = 4; tim[6] = 1;
  uint8_t *p = tim.data() + 16;
  p[4] = 0x00; p[5] = 0x04;          // clut size 0x400
  p[8] = 2;                          // image size
  p[12] = 0x30;                      // header size
  p[14] = 0x00; p[15] = 0x01;        // 256 colours
  p[18] = 3; p[19] = 5;              // 32-bit CLUT, 8bpp
  p[20] = 2; p[22] = 1;              // 2x1
  tim.push_back(0); tim.push_back(8);  // pixels: indices 0 and 8
  tim.resize(tim.size() + 0x400, 0);
  uint8_t *clut = tim.data() + 16 + 48 + 2;
  clut[0] = 10; clut[1] = 20; clut[2] = 30; clut[3] = 0x80;     // index 0
  // CSM1 swaps entries 8..15 with 16..23
  clut[4 * 16 + 0] = 1; clut[4 * 16 + 1] = 2; clut[4 * 16 + 2] = 3; clut[4 * 16 + 3] = 0x40;
  Image img;
  CHECK(DecodeTim2(tim.data(), tim.size(), &img));
  CHECK(img.width == 2 && img.height == 1);
  CHECK(img.rgba[0] == 10 && img.rgba[3] == 255);
  CHECK(img.rgba[4] == 1 && img.rgba[7] == 128);
  CHECK(!DecodeTim2(tim.data(), 10, &img));

  std::printf(failures == 0 ? "ZeroFormatsTest OK\n" : "ZeroFormatsTest FAILED\n");
  return failures == 0 ? 0 : 1;
}
