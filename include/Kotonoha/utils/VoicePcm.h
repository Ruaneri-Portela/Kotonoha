#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <Kotonoha/utils/IOMonitor.h>

#ifdef __cplusplus
extern "C" {
#endif

bool Kotonoha_DecodeVoicePcm(const char* path, int16_t** samples,
	size_t* sampleCount, Kotonoha_IOMonitor* ioMonitor);

#ifdef __cplusplus
}
#endif
