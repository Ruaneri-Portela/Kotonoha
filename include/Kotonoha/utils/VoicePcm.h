#pragma once

#include <Kotonoha/utils/IOMonitor.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool Kotonoha_DecodeVoicePcm(const char *path, int16_t **samples,
                             size_t *sampleCount,
                             Kotonoha_IOMonitor *ioMonitor);
