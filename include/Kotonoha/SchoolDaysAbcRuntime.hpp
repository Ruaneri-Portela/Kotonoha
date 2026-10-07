#pragma once

#include <Kotonoha/SchoolDaysSceneTime.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

struct SchoolDaysActivityRecord {
    Kotonoha_SceneTick tick = 0; // media-relative, 24 Hz
    bool active = false;
};

inline uint64_t OriginalSchoolDaysActivityTime100ns(uint64_t n) {
    return (n * 10000000ULL + 12) / 24;
}

inline bool SampleSchoolDaysPcmChunk(const std::vector<int16_t>& samples,
                                    size_t processedBytes, size_t chunkBytes,
                                    uint64_t recordIndex) {
    const int64_t localIndex =
        static_cast<int64_t>((chunkBytes * recordIndex) / 23) -
        static_cast<int64_t>(processedBytes / 2);
    /* The original comparison is against bytes; the second bound protects
       this port from the original's unproven backing-allocation overread. */
    if (localIndex < 0 || localIndex >= static_cast<int64_t>(chunkBytes) ||
        localIndex >= static_cast<int64_t>(chunkBytes / 2)) return false;
    const size_t index = processedBytes / 2 + static_cast<size_t>(localIndex);
    if (index >= samples.size()) return false;
    const int16_t value = samples[index];
    return value > 60 || value <= -60;
}

/* G1.4/1.4.1: signed mono PCM at 44100 Hz, split into 44100-byte blocks.
   Input is already normalized; playback PCM is kept separate. */
inline std::vector<SchoolDaysActivityRecord> AnalyzeSchoolDaysPcm(
    const std::vector<int16_t>& samples) {
    constexpr size_t kChunkBytes = 44100;
    const size_t totalBytes = samples.size() * sizeof(int16_t);
    std::vector<SchoolDaysActivityRecord> records;
    uint64_t n = 0;
    for (size_t processed = 0; processed < totalBytes; processed += kChunkBytes) {
        const size_t bytes = std::min(kChunkBytes, totalBytes - processed);
        const float limit = static_cast<float>(
            (static_cast<double>(processed + bytes) / 44100.0) * 11.5);
        while (static_cast<float>(n) <= limit) {
            records.push_back({n, SampleSchoolDaysPcmChunk(
                samples, processed, bytes, n)});
            ++n;
        }
    }
    return records;
}

inline bool SchoolDaysActivityAt(const std::vector<SchoolDaysActivityRecord>& records,
                                 Kotonoha_SceneTick relativeTick) {
    const auto it = std::upper_bound(records.begin(), records.end(), relativeTick,
        [](Kotonoha_SceneTick tick, const SchoolDaysActivityRecord& record) {
            return tick < record.tick;
        });
    return it != records.begin() && relativeTick <= records.back().tick &&
           (it - 1)->active;
}

struct SchoolDaysAbcState {
    uint32_t counter = 0;
    uint8_t activeIndex = 0;
};

inline void AdvanceSchoolDaysAbcState(
    SchoolDaysAbcState& state,
    const std::vector<SchoolDaysActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick tick) {
    if (tick < voiceStart) return;
    if (!SchoolDaysActivityAt(records, tick - voiceStart)) {
        state.activeIndex = 0;
        return;
    }
    if (tick % 3 == 0) ++state.counter;
    state.activeIndex = static_cast<uint8_t>(state.counter % 3);
}

inline SchoolDaysAbcState ComputeSchoolDaysAbcStateAt(
    const std::vector<SchoolDaysActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick target) {
    SchoolDaysAbcState state;
    if (target < voiceStart) return state;
    for (Kotonoha_SceneTick tick = voiceStart; tick <= target; ++tick)
        AdvanceSchoolDaysAbcState(state, records, voiceStart, tick);
    return state;
}

struct SchoolDaysAbcGroupResources {
    std::string baseStem;
    std::string key;
    std::string paths[3];
    bool Complete() const {
        return !paths[0].empty() && !paths[1].empty() && !paths[2].empty();
    }
};
} // namespace Kotonoha
