#pragma once

#include <SDL3/SDL.h>

/* ORS scene-relative time is an integer count of 1/24-second ticks. */
typedef Uint64 Kotonoha_SceneTick;

/* Absolute conversions avoid drift from accumulating rounded frame lengths. */
static inline Uint64 Kotonoha_SceneTickToMicroseconds(Kotonoha_SceneTick tick) {
    return (tick / 24) * 1000000 + ((tick % 24) * 1000000) / 24;
}

/* First representable millisecond at or after the tick boundary. */
static inline Uint64 Kotonoha_SceneTickToMillisecondsCeil(Kotonoha_SceneTick tick) {
    return (tick / 24) * 1000 + (((tick % 24) * 1000) + 23) / 24;
}

static inline Kotonoha_SceneTick Kotonoha_MillisecondsToSceneTick(Uint64 ms) {
    return (ms / 1000) * 24 + ((ms % 1000) * 24) / 1000;
}
