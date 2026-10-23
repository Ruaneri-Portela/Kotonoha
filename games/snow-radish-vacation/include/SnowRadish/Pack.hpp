#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace SnowRadish {

// LZSS used by the Overflow ADV3 engine (LOCPU.DLL DecodeLz): 4 KiB zeroed
// window, write cursor starts at 0xFEE, MSB-first flags, match = u16 LE with
// length (d & 15) + 3 and absolute window position d >> 4.
// Returns the number of source bytes consumed.
size_t lzssDecode(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);

struct PackEntry {
    uint32_t offset = 0;
    uint32_t compressedSize = 0;
    uint32_t rawSize = 0;
    uint32_t id = 0;
    std::string name;
};

// Reads the compressed directory of a *.DAT pack. The last 12 bytes are
// three u32 values XORed with 0xF0F0F0F0: directory compressed size, directory
// raw size and a checksum. Each 80-byte record is {id, name[64], offset,
// compSize, rawSize}; compSize == rawSize means the entry is stored.
std::vector<PackEntry> readDirectory(const std::vector<uint8_t>& pack);

// Entries with an unknown raw size are decoded until their compressed bytes run out.
std::vector<uint8_t> extractEntry(const std::vector<uint8_t>& pack, const PackEntry& entry);

} // namespace SnowRadish

namespace SnowRadish {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgb; // top-down, 3 bytes per pixel (R, G, B)
};

// Decodes an .EVD picture (type 3: u32 type, width, height + delta/run/literal
// tokens in BGR, stored bottom-up). Returns false on malformed data.
bool decodeEvd(const std::vector<uint8_t>& data, Image& out);

} // namespace SnowRadish

namespace SnowRadish {

struct Sprite {
    uint32_t width = 0, height = 0; // size of the cropped picture
    int32_t x = 0, y = 0;           // its position on the 800x600 screen canvas
    std::vector<uint8_t> rgba;      // top-down RGBA
};

// Decodes a type-1 .CHD sprite (LOCPU.DLL LoadChr): a 20-byte header (bounding
// box), then 600 screen rows stored bottom-up. Row tokens: 0xFF end of row,
// >=0x9F skip n-0x9E, 0x7F..0x9E opaque run of n-0x7E, <0x7F one pixel with
// alpha n/0x67. The result is cropped to its visible pixels.
bool decodeChd(const std::vector<uint8_t>& data, Sprite& out);

} // namespace SnowRadish

namespace SnowRadish {

struct Pcm {
    std::vector<int16_t> samples; // interleaved when channels == 2
    int channels = 2;
};

// Decodes a raw ADPS block body (two interleaved 4-bit step-table channels).
Pcm decodeAdps(const uint8_t* data, size_t size);

// An .ADPS file (SE/BGM): [u32 introSize][u32 loopSize], then the intro block
// {u32 n, n bytes} and the loop block {u32 n, n bytes}. Played once the intro is
// followed by the loop block; when looping, only the loop block repeats.
struct AdpsTrack {
    Pcm intro, loop;
};
bool decodeAdpsFile(const std::vector<uint8_t>& data, AdpsTrack& out);

// Decodes an .ADP4 voice: u32 unit count, then flag-driven runs (one flag byte per
// 8 runs; a run is a 1- or 2-byte length) of either silence or ADPCM bytes XORed
// with an incrementing key. Each unit is two mono samples.
Pcm decodeAdp4(const std::vector<uint8_t>& data);

} // namespace SnowRadish
