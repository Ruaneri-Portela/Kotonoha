#pragma once

#include <Kotonoha/utils/SceneTime.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Kotonoha {

// Pure School Days HQ ABC mouth-animation state reconstructed from the
// authenticated v1.02 producer/consumer path.  The runtime/compositor owns
// decoding, resource I/O and rendering; this header only carries deterministic
// game policy so it can also be used for seek reconstruction and tests.
struct SchoolDaysAbcActivityRecord {
    Kotonoha_SceneTick tick = 0; // voice-relative, 24 Hz
    bool active = false;
};

inline uint64_t SchoolDaysAbcActivityTime100ns(uint64_t recordIndex) {
    return (recordIndex * UINT64_C(10000000) + 12u) / 24u;
}

inline bool SchoolDaysAbcSamplePcmChunk(const std::vector<int16_t>& samples,
                                        size_t processedBytes,
                                        size_t chunkBytes,
                                        uint64_t recordIndex) {
    const int64_t localIndex =
        static_cast<int64_t>((chunkBytes * recordIndex) / 23u) -
        static_cast<int64_t>(processedBytes / sizeof(int16_t));

    // The retail comparison is byte-oriented.  Keep its geometry while also
    // bounding the translated s16 access so malformed/partial input cannot
    // reproduce the original backing-allocation overread.
    if (localIndex < 0 || localIndex >= static_cast<int64_t>(chunkBytes) ||
        localIndex >= static_cast<int64_t>(chunkBytes / sizeof(int16_t))) {
        return false;
    }
    const size_t index = processedBytes / sizeof(int16_t) +
                         static_cast<size_t>(localIndex);
    if (index >= samples.size()) return false;
    const int16_t value = samples[index];
    return value > 60 || value <= -60;
}

// Input is normalized signed mono s16 PCM at 44.1 kHz.  The original producer
// walks 44,100-byte blocks and emits activity records on its 24 Hz clock.
inline std::vector<SchoolDaysAbcActivityRecord>
SchoolDaysAnalyzeAbcPcmActivity(const std::vector<int16_t>& samples) {
    constexpr size_t kChunkBytes = 44100u;
    const size_t totalBytes = samples.size() * sizeof(int16_t);
    std::vector<SchoolDaysAbcActivityRecord> records;
    uint64_t recordIndex = 0;
    for (size_t processed = 0; processed < totalBytes;
         processed += kChunkBytes) {
        const size_t bytes = std::min(kChunkBytes, totalBytes - processed);
        const float limit = static_cast<float>(
            (static_cast<double>(processed + bytes) / 44100.0) * 11.5);
        while (static_cast<float>(recordIndex) <= limit) {
            records.push_back({recordIndex,
                               SchoolDaysAbcSamplePcmChunk(
                                   samples, processed, bytes, recordIndex)});
            ++recordIndex;
        }
    }
    return records;
}

inline bool SchoolDaysAbcVoiceActiveAt(
    const std::vector<SchoolDaysAbcActivityRecord>& records,
    Kotonoha_SceneTick relativeTick) {
    if (records.empty()) return false;
    const auto it = std::upper_bound(
        records.begin(), records.end(), relativeTick,
        [](Kotonoha_SceneTick tick, const SchoolDaysAbcActivityRecord& record) {
            return tick < record.tick;
        });
    return it != records.begin() && relativeTick <= records.back().tick &&
           (it - 1)->active;
}

struct SchoolDaysAbcAnimationState {
    uint32_t counter = 0;
    uint8_t activeIndex = 0; // 0=A, 1=B, 2=C
};

struct SchoolDaysAbcAnimationCursor {
    SchoolDaysAbcAnimationState state{};
    Kotonoha_SceneTick lastTick = 0;
    bool initialized = false;
};

inline bool SchoolDaysAbcAnimationUpdatesAtRate(double playbackRate) {
    // ImageChar stops updating at the two authenticated fast rates, 12x/24x.
    // The selected frame remains frozen until ordinary-rate updates resume.
    return playbackRate < 12.0;
}

inline void SchoolDaysAdvanceAbcAnimation(
    SchoolDaysAbcAnimationState& state,
    const std::vector<SchoolDaysAbcActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick graphicTick) {
    if (graphicTick < voiceStart) return;
    if (!SchoolDaysAbcVoiceActiveAt(records, graphicTick - voiceStart)) {
        state.activeIndex = 0;
        return;
    }
    if (graphicTick % 3u == 0u) ++state.counter;
    state.activeIndex = static_cast<uint8_t>(state.counter % 3u);
}

inline SchoolDaysAbcAnimationState SchoolDaysComputeAbcAnimationAt(
    const std::vector<SchoolDaysAbcActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick target) {
    SchoolDaysAbcAnimationState state;
    if (target < voiceStart) return state;
    for (Kotonoha_SceneTick tick = voiceStart; tick <= target; ++tick) {
        SchoolDaysAdvanceAbcAnimation(state, records, voiceStart, tick);
        if (tick == target) break;
    }
    return state;
}

inline void SchoolDaysUpdateAbcAnimation(
    SchoolDaysAbcAnimationCursor& cursor,
    const std::vector<SchoolDaysAbcActivityRecord>& records,
    Kotonoha_SceneTick voiceStart,
    Kotonoha_SceneTick currentTick,
    double playbackRate) {
    if (currentTick < voiceStart) return;
    if (cursor.initialized && currentTick <= cursor.lastTick) return;

    const Kotonoha_SceneTick first =
        cursor.initialized ? cursor.lastTick + 1u : voiceStart;
    if (SchoolDaysAbcAnimationUpdatesAtRate(playbackRate)) {
        for (Kotonoha_SceneTick tick = first; tick <= currentTick; ++tick) {
            SchoolDaysAdvanceAbcAnimation(cursor.state, records, voiceStart, tick);
            if (tick == currentTick) break;
        }
    }

    // The authenticated runtime advances this cursor even while >=12x freezes
    // ImageChar.  A later rate drop therefore resumes from the new time instead
    // of replaying the skipped mouth updates.
    cursor.lastTick = currentTick;
    cursor.initialized = true;
}

inline char SchoolDaysAbcAsciiLower(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value - 'A' + 'a')
               : static_cast<char>(value);
}

inline std::string SchoolDaysNormalizeAbcAnimationKey(std::string key) {
    std::transform(key.begin(), key.end(), key.begin(), SchoolDaysAbcAsciiLower);
    // The shipped scripts use "xxx" as an explicit no-ABC sentinel.  Empty is
    // also intentionally empty: there is no fallback to an uncoded .A/.B/.C set.
    if (key == "xxx") key.clear();
    return key;
}

struct SchoolDaysAbcGroupResources {
    std::string baseStem;
    std::string key;
    std::array<std::string, 3> paths{};
    bool collision = false;

    bool Complete() const {
        return !collision && !paths[0].empty() && !paths[1].empty() &&
               !paths[2].empty();
    }
};

inline bool SchoolDaysAssignAbcResource(SchoolDaysAbcGroupResources& group,
                                        uint8_t state,
                                        const std::string& resourceId) {
    if (state >= group.paths.size() || resourceId.empty() || group.collision)
        return false;
    if (group.paths[state].empty()) {
        group.paths[state] = resourceId;
        return true;
    }
    if (group.paths[state] == resourceId) return true;
    group.collision = true;
    return false;
}

struct SchoolDaysAbcSibling {
    std::string baseStem;
    std::string key;
    std::string resourceId;
    uint8_t state = 0;
};

inline std::string SchoolDaysAbcFileName(const std::string& resourceId) {
    const size_t slash = resourceId.find_last_of("/\\");
    return resourceId.substr(slash == std::string::npos ? 0u : slash + 1u);
}

inline std::string SchoolDaysAbcAsciiLowerString(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), SchoolDaysAbcAsciiLower);
    return text;
}

// Parse one already-enumerated sibling.  Directory listing belongs to the game
// asset provider; this helper deliberately knows nothing about filesystems,
// mounted GPKs or renderer objects.
inline bool SchoolDaysParseAbcSibling(const std::string& baseResourceId,
                                      const std::string& siblingResourceId,
                                      SchoolDaysAbcSibling* out) {
    if (out == nullptr || baseResourceId.empty() || siblingResourceId.empty())
        return false;
    const std::string baseName = SchoolDaysAbcFileName(baseResourceId);
    if (baseName.size() < 5u) return false;
    const std::string baseLower = SchoolDaysAbcAsciiLowerString(baseName);
    if (baseLower.substr(baseLower.size() - 4u) != ".png") return false;
    const std::string stem = baseName.substr(0u, baseName.size() - 4u);
    const std::string prefix = SchoolDaysAbcAsciiLowerString(stem);

    const std::string child = SchoolDaysAbcFileName(siblingResourceId);
    const std::string lower = SchoolDaysAbcAsciiLowerString(child);
    if (child.size() <= stem.size() + 6u || lower.size() < 6u ||
        lower.compare(0u, prefix.size(), prefix) != 0 ||
        lower.substr(lower.size() - 4u) != ".png") {
        return false;
    }
    const size_t statePos = lower.size() - 6u;
    if (lower[statePos] != '.' || lower[statePos + 1u] < 'a' ||
        lower[statePos + 1u] > 'c') {
        return false;
    }
    const std::string key = lower.substr(prefix.size(), statePos - prefix.size());
    if (key.empty()) return false;

    *out = {stem, key, siblingResourceId,
            static_cast<uint8_t>(lower[statePos + 1u] - 'a')};
    return true;
}

// Groups a provider-supplied sibling list exactly as the old Image discovery
// path did.  Results are key-sorted for deterministic callers; colliding groups
// remain marked so the later owner can reject them without losing diagnostics.
inline std::vector<SchoolDaysAbcGroupResources> SchoolDaysGroupAbcSiblings(
    const std::string& baseResourceId,
    const std::vector<std::string>& siblingResourceIds) {
    std::map<std::string, SchoolDaysAbcGroupResources> groups;
    for (const auto& resourceId : siblingResourceIds) {
        SchoolDaysAbcSibling sibling;
        if (!SchoolDaysParseAbcSibling(baseResourceId, resourceId, &sibling))
            continue;
        auto& group = groups[sibling.key];
        group.baseStem = sibling.baseStem;
        group.key = sibling.key;
        (void)SchoolDaysAssignAbcResource(group, sibling.state, sibling.resourceId);
    }

    std::vector<SchoolDaysAbcGroupResources> result;
    result.reserve(groups.size());
    for (auto& item : groups) result.push_back(std::move(item.second));
    return result;
}

} // namespace Kotonoha
