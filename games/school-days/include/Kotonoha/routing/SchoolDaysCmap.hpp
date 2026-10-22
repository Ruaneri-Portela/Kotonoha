#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

// School Days HQ CMAP region map.
//
// Authenticated EXE 0x004659D0 reads two little-endian int32 dimensions and
// widens exactly width*height bytes beginning at offset 8 into a uint16 map.
// 0x00465BC0 bounds the already-offset x/y coordinates and returns the map
// value directly. Any bytes after the exact map payload are not consumed and
// are intentionally ignored here too.
class SchoolDaysCmap {
public:
    bool Parse(const uint8_t* bytes, size_t size, std::string* error = nullptr);
    bool LoadFile(const std::string& path, std::string* error = nullptr);
    void Clear();

    // Coordinates are local to the CMAP (the native caller applies its origin
    // offsets before 0x00465BC0 performs this lookup). Returns -1 out of bounds.
    int Get(int x, int y) const;

    // Compatibility spelling for the already-staged SaveFeel input model.
    int RegionAt(int x, int y) const { return Get(x, y); }

    uint32_t Width() const { return static_cast<uint32_t>(width_); }
    uint32_t Height() const { return static_cast<uint32_t>(height_); }
    bool Empty() const { return regions_.empty(); }
    const std::vector<uint16_t>& Regions() const { return regions_; }

private:
    static bool Fail(std::string* error, const char* message);

    int32_t width_ = 0;
    int32_t height_ = 0;
    std::vector<uint16_t> regions_;
};

} // namespace Kotonoha
