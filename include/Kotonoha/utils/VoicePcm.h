#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Kotonoha_DecodeVoicePcm(const char* path, int16_t** samples,
	size_t* sampleCount);

#ifdef __cplusplus
}
#endif
