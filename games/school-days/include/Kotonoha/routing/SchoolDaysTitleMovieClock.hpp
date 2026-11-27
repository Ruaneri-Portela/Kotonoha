#pragma once

#include <cstdint>

namespace Kotonoha {

// Retail Title EndBG clock: requests an absolute 24 Hz frame index regardless
// of the WMV stream's own nominal fps. The original reader converts that index
// to a rounded 100 ns timestamp; the current VideoRender clock is millisecond-
// based, so expose the nearest-ms projection of the same absolute timestamp.
class SchoolDaysTitleMovieClock {
public:
    static uint64_t FrameIndex(uint64_t elapsedMs) {
        return (elapsedMs / 1000u) * 24u +
               ((elapsedMs % 1000u) * 24u) / 1000u;
    }

    static uint64_t Target100ns(uint64_t frameIndex) {
        return (frameIndex * UINT64_C(10000000) + 12u) / 24u;
    }

    static uint64_t TargetMilliseconds(uint64_t frameIndex) {
        const uint64_t target100ns = Target100ns(frameIndex);
        return (target100ns + 5000u) / 10000u;
    }
};

} // namespace Kotonoha
