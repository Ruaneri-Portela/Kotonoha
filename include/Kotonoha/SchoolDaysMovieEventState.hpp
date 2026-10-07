#pragma once

#include <Kotonoha/SchoolDaysSceneTime.h>
#include <string>

namespace Kotonoha {

enum class SchoolDaysMovieKind { PlayMovie, EndRoll };

/* The ORS-visible movie state contains no decoder or renderer pointers. */
struct SchoolDaysMovieEventState {
    SchoolDaysMovieKind kind = SchoolDaysMovieKind::PlayMovie;
    std::string resource;
    Kotonoha_SceneTick startTick = 0;
    Kotonoha_SceneTick endTick = 0;
    Uint64 numeric = 0;
    bool prepared = false;
    bool mediaReady = false;
    bool active = false;
    bool completed = false;
    bool eof = false;

    static SchoolDaysMovieEventState Create(SchoolDaysMovieKind type,
            const std::string& logicalPath, Kotonoha_SceneTick start,
            Kotonoha_SceneTick end, Uint64 field = 0) {
        SchoolDaysMovieEventState state;
        state.kind = type;
        state.resource = logicalPath + ".wmv";
        state.startTick = start;
        state.endTick = end;
        state.numeric = type == SchoolDaysMovieKind::EndRoll ? 0 : field;
        return state;
    }

    bool Supported() const { return numeric == 0; }
    bool InWindow(Kotonoha_SceneTick tick) const {
        return startTick <= tick && tick < endTick;
    }
    void Prepare(bool ready) {
        prepared = true;
        mediaReady = ready;
    }
    bool Activate(Kotonoha_SceneTick tick) {
        if (!prepared || !Supported() || !InWindow(tick)) return false;
        active = true; // A missing WMV does not prevent the ORS clock advancing.
        return mediaReady;
    }
    void MarkEof() { eof = true; } // EOF never implies scene end.
    bool AdvanceTo(Kotonoha_SceneTick tick) {
        if (tick < endTick || completed) return false;
        const bool wasActive = active;
        active = false;
        completed = true;
        return wasActive;
    }
    void Reset() {
        prepared = mediaReady = active = completed = eof = false;
    }
};

} // namespace Kotonoha
