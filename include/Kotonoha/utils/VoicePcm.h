#pragma once

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

bool KotonohaPcmDecodeVoice(const char *path, Sint16 **samples,
                            size_t *sampleCount);

#ifdef __cplusplus
}
#endif