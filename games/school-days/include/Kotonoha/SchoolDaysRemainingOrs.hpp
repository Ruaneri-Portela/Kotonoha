#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <Kotonoha/parsers/Ors.h>
#ifdef __cplusplus
}
#endif
#include <unordered_set>

namespace Kotonoha {

enum class SchoolDaysOrsClass {
    VisualWindow, MediaWindow, AudioWindow, AudioSlotWindow,
    TextWindow, FadeWindow, Interaction, LegacyNoOpWindow,
    Marker, SceneEndMarker, Unknown
};

inline SchoolDaysOrsClass ClassifySchoolDaysOrs(Kotonoha_orsType type) {
    switch (type) {
    case CREATE_BG: return SchoolDaysOrsClass::VisualWindow;
    case PLAY_MOVIE: case END_ROLL: return SchoolDaysOrsClass::MediaWindow;
    case PLAY_VOICE: return SchoolDaysOrsClass::AudioWindow;
    case PLAY_SE: case PLAY_BGM: case END_BGM:
        return SchoolDaysOrsClass::AudioSlotWindow;
    case PRINT_TEXT: return SchoolDaysOrsClass::TextWindow;
    case BLACK_FADE: case WHITE_FADE: return SchoolDaysOrsClass::FadeWindow;
    case SetSELECT: return SchoolDaysOrsClass::Interaction;
    case MOVE_SOM: return SchoolDaysOrsClass::LegacyNoOpWindow;
    case SkipFRAME: return SchoolDaysOrsClass::Marker;
    case Next: return SchoolDaysOrsClass::SceneEndMarker;
    default: return SchoolDaysOrsClass::Unknown;
    }
}

inline bool SchoolDaysOrsWindowContains(const Kotonoha_orsEvent* event,
                                       Kotonoha_SceneTick tick) {
    return event != nullptr && event->startTick <= tick && tick < event->endTick;
}

/* SOMCON was optional in the original. This tracks only the ORS cursor/window;
   no serial, USB, audio, graphics, or snapshot state is created. */
class SchoolDaysMoveSomNoOp {
public:
    void Start(const Kotonoha_orsEvent* event) {
        if (event != nullptr && event->command == MOVE_SOM)
            active_.insert(event);
    }
    void AdvanceTo(Kotonoha_SceneTick tick) {
        for (auto it = active_.begin(); it != active_.end();)
            if (tick >= (*it)->endTick) it = active_.erase(it);
            else ++it;
    }
    bool Active(const Kotonoha_orsEvent* event) const {
        return active_.find(event) != active_.end();
    }
    void Reset() { active_.clear(); }
private:
    std::unordered_set<const Kotonoha_orsEvent*> active_;
};

inline bool SchoolDaysChoiceShouldSubmit(bool alreadySubmitted, int result) {
    return !alreadySubmitted && result != -2;
}

} // namespace Kotonoha
