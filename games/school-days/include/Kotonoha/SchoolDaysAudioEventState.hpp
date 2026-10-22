#pragma once

#include <array>

extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

struct Kotonoha_audioDecode;

namespace Kotonoha {

inline bool SchoolDaysVoiceAllowed(Uint64 numeric, bool menVoiceEnabled) {
    return numeric == 0 || menVoiceEnabled;
}

/* The ORS key is an arbitrary graphic-association key, including an empty key. */
inline const char* SchoolDaysVoiceAnimationKey(const Kotonoha_orsEvent* event) {
    if (event == nullptr || event->command != PLAY_VOICE ||
        event->data.play_voice == nullptr ||
        event->data.play_voice->character_short == nullptr) return "";
    return event->data.play_voice->character_short;
}

class SchoolDaysSeSlots {
public:
    static constexpr size_t Count = 9;
    struct Slot {
        const Kotonoha_orsEvent* event = nullptr;
        Kotonoha_audioDecode* media = nullptr;
    };

    static bool Valid(Uint64 index) { return index < Count; }

    bool Replace(Uint64 index, const Kotonoha_orsEvent* event,
                 Kotonoha_audioDecode* media, Slot* previous = nullptr) {
        if (!Valid(index)) return false;
        if (previous) *previous = slots_[static_cast<size_t>(index)];
        slots_[static_cast<size_t>(index)] = {event, media};
        return true;
    }

    Slot At(Uint64 index) const {
        return Valid(index) ? slots_[static_cast<size_t>(index)] : Slot{};
    }

    void Clear() { slots_ = {}; }

    /* Latest started event owns its slot; an expired replacement leaves it empty. */
    static std::array<const Kotonoha_orsEvent*, Count> ActiveEventsAt(
        const Kotonoha_orsData* source, Uint64 targetMs) {
        std::array<const Kotonoha_orsEvent*, Count> latest{};
        const Kotonoha_SceneTick targetTick =
            Kotonoha_MillisecondsToSceneTick(targetMs);
        for (auto* event = source ? source->data : nullptr;
             event != nullptr && event->startTick <= targetTick;
             event = event->next) {
            if (event->command != PLAY_SE || event->data.play_se == nullptr) continue;
            const Uint64 slot = event->data.play_se->a;
            if (Valid(slot)) latest[static_cast<size_t>(slot)] = event;
        }
        for (auto& event : latest) {
            if (event != nullptr &&
                Kotonoha_SceneTickToMillisecondsCeil(event->endTick) < targetMs)
                event = nullptr;
        }
        return latest;
    }

private:
    std::array<Slot, Count> slots_{};
};

} // namespace Kotonoha
