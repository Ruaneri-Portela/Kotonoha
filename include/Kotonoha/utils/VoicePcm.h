#pragma once

#include <Kotonoha/utils/IOMonitor.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

bool KotonohaPcmDecodeVoice(const char *path, Sint16 **samples,
                            size_t *sampleCount, Kotonoha_IOMonitor *ioMonitor);

#ifdef __cplusplus
}
#endif