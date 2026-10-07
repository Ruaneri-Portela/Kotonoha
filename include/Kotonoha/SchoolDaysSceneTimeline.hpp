#pragma once

#include <vector>

extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

namespace Kotonoha {

/* C++11-compatible optional tick; Kotonoha's production target predates C++17. */
struct SchoolDaysOptionalTick {
    bool present = false;
    Kotonoha_SceneTick tick = 0;

    bool has_value() const { return present; }
    explicit operator bool() const { return present; }
    Kotonoha_SceneTick operator*() const { return tick; }
    bool operator==(Kotonoha_SceneTick other) const {
        return present && tick == other;
    }
};

/* Scene-relative ORS scheduler. The eventTouched flags are its dispatch cursor;
   the linked list is already sorted stably by startTick by the parser. */
class SchoolDaysSceneTimeline {
public:
    void Bind(Kotonoha_orsData* source) {
        source_ = source;
        nextTick_ = {};
        skipFrameTick_ = {};
        for (auto* event = source_ ? source_->data : nullptr;
             event != nullptr; event = event->next) {
            if (event->command == Next) nextTick_ = {true, event->startTick};
            if (event->command == SkipFRAME)
                skipFrameTick_ = {true, event->startTick};
        }
        Restart();
    }

    SchoolDaysOptionalTick NextTick() const { return nextTick_; }
    SchoolDaysOptionalTick SkipFrameTick() const {
        return skipFrameTick_;
    }

    bool ReachedNext(Kotonoha_SceneTick currentTick) const {
        return nextTick_.has_value() && currentTick >= *nextTick_;
    }

    bool ReadyToEnd(Kotonoha_SceneTick currentTick) const {
        return ReachedNext(currentTick) && lastAdvancedTick_.has_value() &&
               *lastAdvancedTick_ >= *nextTick_;
    }

    static SchoolDaysOptionalTick ComputeSkipToEndTarget(
        Kotonoha_SceneTick currentTick,
        Kotonoha_SceneTick skipFrameTick,
        Kotonoha_SceneTick nextTick) {
        if (skipFrameTick == nextTick && currentTick < nextTick)
            return {true, nextTick};
        if (skipFrameTick < nextTick && currentTick < skipFrameTick)
            return {true, skipFrameTick > 24 ? skipFrameTick - 24 : 0};
        /* The original action after SkipFRAME is not yet established. */
        return {};
    }

    SchoolDaysOptionalTick ComputeSkipToEndTarget(
        Kotonoha_SceneTick currentTick) const {
        if (!skipFrameTick_ || !nextTick_) return {};
        return ComputeSkipToEndTarget(currentTick, *skipFrameTick_, *nextTick_);
    }

    std::vector<Kotonoha_orsEvent*> AdvanceTo(Kotonoha_SceneTick currentTick) {
        std::vector<Kotonoha_orsEvent*> due;
        if (lastAdvancedTick_ && currentTick < *lastAdvancedTick_)
            return due; /* A backwards move requires ResetToTick. */
        const Kotonoha_SceneTick boundary = nextTick_ && currentTick > *nextTick_
                                                ? *nextTick_ : currentTick;
        for (auto* event = source_ ? source_->data : nullptr;
             event != nullptr && event->startTick <= boundary;
             event = event->next) {
            if (!event->eventTouched) {
                event->eventTouched = true;
                due.push_back(event);
            }
        }
        lastAdvancedTick_ = {true, boundary};
        return due;
    }

    /* Prior events stay consumed; events strictly after target remain eligible. */
    void ResetToTick(Kotonoha_SceneTick target) {
        for (auto* event = source_ ? source_->data : nullptr;
             event != nullptr; event = event->next)
            event->eventTouched = event->startTick <= target;
        lastAdvancedTick_ = {true, target};
    }

    /* Runtime media reset may re-register only events active at the target. */
    void RebuildActiveAt(Kotonoha_SceneTick target) {
        for (auto* event = source_ ? source_->data : nullptr;
             event != nullptr; event = event->next)
            event->eventTouched = event->startTick <= target &&
                                  event->endTick <= target;
        lastAdvancedTick_ = {};
    }

    void Restart() {
        for (auto* event = source_ ? source_->data : nullptr;
             event != nullptr; event = event->next)
            event->eventTouched = false;
        lastAdvancedTick_ = {};
    }

private:
    Kotonoha_orsData* source_ = nullptr;
    SchoolDaysOptionalTick nextTick_;
    SchoolDaysOptionalTick skipFrameTick_;
    SchoolDaysOptionalTick lastAdvancedTick_;
};

} // namespace Kotonoha
