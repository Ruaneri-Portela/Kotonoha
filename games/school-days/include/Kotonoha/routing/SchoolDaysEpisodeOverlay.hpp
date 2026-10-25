#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Kotonoha {

// School Days HQ v1.02 bakes the Episode 1 caption into 00-00-A00-001.WMV,
// but later episode entry scenes do not contain that caption in their ORS
// media.  The retail host supplies it at the authenticated callback_38 episode
// handoff.  Keep that presentation policy in the School Days profile rather
// than teaching the generic engine about episodes.
class SchoolDaysEpisodeOverlay final {
public:
    static constexpr uint64_t FadeInEndMs = 500u;
    static constexpr uint64_t FadeOutStartMs = 4000u;
    static constexpr uint64_t EndMs = 5000u;

    bool ArmForNaturalHandoff(std::string_view sourceSceneKey,
                              std::string_view destinationSceneKey) {
        const int sourceEpisode = SceneEpisodeIndex(sourceSceneKey);
        const int destinationEpisode = SceneEpisodeIndex(destinationSceneKey);
        Reset();

        // KTRF episode folders are zero based: 00 is Episode 1, 01 is
        // Episode 2, ... 05 is Episode 6.  Episode 1 already owns its caption
        // inside the retail WMV and must never receive a host duplicate.
        if (sourceEpisode < 0 || destinationEpisode < 1 ||
            destinationEpisode > 5 ||
            destinationEpisode != sourceEpisode + 1) {
            return false;
        }

        episodeNumber_ = destinationEpisode + 1;
        destinationSceneKey_.assign(destinationSceneKey.data(),
                                    destinationSceneKey.size());
        return true;
    }

    void Reset() {
        episodeNumber_ = 0;
        destinationSceneKey_.clear();
    }

    bool Armed() const { return episodeNumber_ != 0; }
    int EpisodeNumber() const { return episodeNumber_; }
    static std::string TelopLogicalPath(int episodeNumber) {
        if (episodeNumber < 1 || episodeNumber > 6) return {};
        return "System/SCREEN/TELOP/TELOP0" +
               std::to_string(episodeNumber) + ".PNG";
    }
    const std::string& DestinationSceneKey() const {
        return destinationSceneKey_;
    }

    bool AppliesTo(std::string_view sceneKey) const {
        return Armed() && sceneKey == destinationSceneKey_;
    }

    static uint8_t Alpha(uint64_t sceneMs) {
        if (sceneMs >= EndMs) return 0u;
        if (sceneMs < FadeInEndMs) {
            return static_cast<uint8_t>((sceneMs * 255u) / FadeInEndMs);
        }
        if (sceneMs <= FadeOutStartMs) return 255u;
        return static_cast<uint8_t>(
            ((EndMs - sceneMs) * 255u) / (EndMs - FadeOutStartMs));
    }

private:
    static int SceneEpisodeIndex(std::string_view sceneKey) {
        if (sceneKey.size() < 3u || sceneKey[2] != '/' ||
            sceneKey[0] < '0' || sceneKey[0] > '9' ||
            sceneKey[1] < '0' || sceneKey[1] > '9') {
            return -1;
        }
        return (sceneKey[0] - '0') * 10 + (sceneKey[1] - '0');
    }

    int episodeNumber_ = 0;
    std::string destinationSceneKey_;
};

} // namespace Kotonoha
