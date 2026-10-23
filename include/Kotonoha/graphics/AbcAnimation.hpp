#pragma once

#include <Kotonoha/SceneTime.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

struct AbcActivityRecord {
    Kotonoha_SceneTick tick = 0; // media-relative, 24 Hz
    bool active = false;
};

inline uint64_t ActivityTime100ns(uint64_t n) {
    return (n * 10000000ULL + 12) / 24;
}

inline bool SamplePcmChunk(const std::vector<int16_t>& samples,
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

/* Analyze normalized signed mono PCM at 44100 Hz in 44100-byte blocks. */
inline std::vector<AbcActivityRecord> AnalyzePcmActivity(
    const std::vector<int16_t>& samples) {
    constexpr size_t kChunkBytes = 44100;
    const size_t totalBytes = samples.size() * sizeof(int16_t);
    std::vector<AbcActivityRecord> records;
    uint64_t n = 0;
    for (size_t processed = 0; processed < totalBytes; processed += kChunkBytes) {
        const size_t bytes = std::min(kChunkBytes, totalBytes - processed);
        const float limit = static_cast<float>(
            (static_cast<double>(processed + bytes) / 44100.0) * 11.5);
        while (static_cast<float>(n) <= limit) {
            records.push_back({n, SamplePcmChunk(
                samples, processed, bytes, n)});
            ++n;
        }
    }
    return records;
}

inline bool IsVoiceActiveAt(const std::vector<AbcActivityRecord>& records,
                            Kotonoha_SceneTick relativeTick) {
    const auto it = std::upper_bound(records.begin(), records.end(), relativeTick,
        [](Kotonoha_SceneTick tick, const AbcActivityRecord& record) {
            return tick < record.tick;
        });
    return it != records.begin() && relativeTick <= records.back().tick &&
           (it - 1)->active;
}

struct AbcAnimationState {
    uint32_t counter = 0;
    uint8_t activeIndex = 0;
};

inline bool AbcAnimationUpdatesAtRate(double playbackRate) {
    // High playback rates skip mouth-frame updates; the current frame remains
    // selected until normal-rate updates resume or the binding is retired.
    return playbackRate < 12.0;
}

inline void AdvanceAbcAnimation(
    AbcAnimationState& state,
    const std::vector<AbcActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick tick) {
    if (tick < voiceStart) return;
    if (!IsVoiceActiveAt(records, tick - voiceStart)) {
        state.activeIndex = 0;
        return;
    }
    if (tick % 3 == 0) ++state.counter;
    state.activeIndex = static_cast<uint8_t>(state.counter % 3);
}

inline AbcAnimationState ComputeAbcAnimationAt(
    const std::vector<AbcActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick target) {
    AbcAnimationState state;
    if (target < voiceStart) return state;
    for (Kotonoha_SceneTick tick = voiceStart; tick <= target; ++tick)
        AdvanceAbcAnimation(state, records, voiceStart, tick);
    return state;
}

struct AbcGroupResources {
    std::string baseStem;
    std::string key;
    std::string paths[3];
    bool collision = false;
    bool Complete() const {
        return !collision && !paths[0].empty() && !paths[1].empty() &&
               !paths[2].empty();
    }
};

inline bool AssignAbcResource(AbcGroupResources& group, uint8_t state,
                              const std::string& physicalPath) {
    if (state >= 3u || physicalPath.empty() || group.collision) return false;
    if (group.paths[state].empty()) {
        group.paths[state] = physicalPath;
        return true;
    }
    if (group.paths[state] == physicalPath) return true;
    group.collision = true;
    return false;
}
} // namespace Kotonoha
