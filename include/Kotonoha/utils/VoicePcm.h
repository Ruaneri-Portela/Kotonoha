#pragma once

#include <Kotonoha/utils/FFmpeg.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

bool KotonohaPcmDecodeVoice(const char *path, Sint16 **samples,
                            size_t *sampleCount);
bool KotonohaPcmDecodeVoiceInput(const Kotonoha_FFmpegInput *input,
                                 Sint16 **samples, size_t *sampleCount);

#ifdef __cplusplus
}
#endif
