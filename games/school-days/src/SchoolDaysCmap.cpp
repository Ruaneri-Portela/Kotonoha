#include <Kotonoha/routing/SchoolDaysCmap.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>

#include <limits>

namespace Kotonoha {
namespace {

int32_t ReadLeI32(const uint8_t* bytes) {
    const uint32_t raw = static_cast<uint32_t>(bytes[0]) |
                         (static_cast<uint32_t>(bytes[1]) << 8u) |
                         (static_cast<uint32_t>(bytes[2]) << 16u) |
                         (static_cast<uint32_t>(bytes[3]) << 24u);
    if (raw <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return static_cast<int32_t>(raw);
    }
    return -1 - static_cast<int32_t>(
                    std::numeric_limits<uint32_t>::max() - raw);
}

} // namespace

bool SchoolDaysCmap::Fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message != nullptr ? message : "School Days CMAP error";
    return false;
}

void SchoolDaysCmap::Clear() {
    width_ = 0;
    height_ = 0;
    regions_.clear();
}

bool SchoolDaysCmap::Parse(const uint8_t* bytes, size_t size,
                           std::string* error) {
    if (error != nullptr) error->clear();
    Clear();
    if (bytes == nullptr || size < 8u) {
        return Fail(error, "School Days CMAP is shorter than its dimension header");
    }

    const int32_t width = ReadLeI32(bytes);
    const int32_t height = ReadLeI32(bytes + 4u);
    if (width <= 0 || height <= 0) {
        return Fail(error, "School Days CMAP dimensions must be positive");
    }
    const size_t hostWidth = static_cast<size_t>(width);
    const size_t hostHeight = static_cast<size_t>(height);
    if (hostWidth > std::numeric_limits<size_t>::max() / hostHeight) {
        return Fail(error, "School Days CMAP dimensions overflow host size_t");
    }
    const size_t count = hostWidth * hostHeight;
    if (count > std::numeric_limits<size_t>::max() / sizeof(uint16_t) ||
        count > regions_.max_size()) {
        return Fail(error, "School Days CMAP widened region map size overflows");
    }
    if (count > size - 8u) {
        return Fail(error, "School Days CMAP pixel-region map is truncated");
    }

    try {
        regions_.resize(count);
    }
    catch (...) {
        Clear();
        return Fail(error, "School Days CMAP region allocation failed");
    }
    for (size_t i = 0; i < count; ++i) {
        // Native 0x004659D0 performs zero-extension byte -> uint16_t.
        regions_[i] = static_cast<uint16_t>(bytes[8u + i]);
    }
    width_ = width;
    height_ = height;
    return true;
}

bool SchoolDaysCmap::LoadFile(const std::string& path, std::string* error) {
    if (error != nullptr) error->clear();
    Clear();
    std::vector<uint8_t> bytes;
    if (!ReadSchoolDaysAssetReference(path, &bytes, error)) return false;
    return Parse(bytes.data(), bytes.size(), error);
}

int SchoolDaysCmap::Get(int x, int y) const {
    if (regions_.empty()) return -1;
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return -1;
    }
    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width_) +
                         static_cast<size_t>(x);
    return static_cast<int>(regions_[index]);
}

} // namespace Kotonoha
