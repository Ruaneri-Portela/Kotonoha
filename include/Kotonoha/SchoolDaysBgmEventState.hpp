#pragma once

#include <Kotonoha/SchoolDaysSceneTime.h>
#include <string>

extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

namespace Kotonoha {

struct SchoolDaysBgmResources {
    std::string intro;
    std::string loop;
    std::string oneShot;

    static SchoolDaysBgmResources Normal(const std::string& path) {
        return {path + "_int.ogg", path + "_loop.ogg", ""};
    }
    static SchoolDaysBgmResources Ending(const std::string& path) {
        return {"", "", path + ".ogg"};
    }
};

/* Logical ORS ownership. Physical decoder handles remain in Event/Audio. */
class SchoolDaysBgmSlots {
public:
    struct Slot {
        const Kotonoha_orsEvent* event = nullptr;
        SchoolDaysBgmResources resources;
        bool active = false;
    };

    Slot ReplaceNormal(const Kotonoha_orsEvent* event,
                       const SchoolDaysBgmResources& resources) {
        Slot previous = normal_;
        normal_ = {event, resources, true};
        return previous;
    }
    Slot ReplaceEnding(const Kotonoha_orsEvent* event,
                       const SchoolDaysBgmResources& resources) {
        Slot previous = ending_;
        ending_ = {event, resources, true};
        return previous;
    }
    const Slot& Normal() const { return normal_; }
    const Slot& Ending() const { return ending_; }
    void ClearNormal() { normal_ = {}; }
    void ClearEnding() { ending_ = {}; }
    void Clear() { ClearNormal(); ClearEnding(); }

    /* Latest started event wins even when it has already ended. */
    static const Kotonoha_orsEvent* ActiveAt(
        const Kotonoha_orsData* source, Kotonoha_SceneTick target,
        Kotonoha_orsType command) {
        const Kotonoha_orsEvent* latest = nullptr;
        for (auto* event = source ? source->data : nullptr;
             event != nullptr && event->startTick <= target; event = event->next)
            if (event->command == command) latest = event;
        return latest && target < latest->endTick ? latest : nullptr;
    }

private:
    Slot normal_;
    Slot ending_;
};
} // namespace Kotonoha
