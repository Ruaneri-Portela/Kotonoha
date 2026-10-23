#pragma once

#include <stdbool.h>

#define KotonohaPath_SEPARATOR "/"

#ifdef __cplusplus
extern "C" {
#endif

char *KotonohaPathGetAbsolute(const char *path);

bool KotonohaPathIsAbsolute(const char *path);

char *KotonohaPathJoin(const char *base, const char *relative);

#ifdef __cplusplus
}
#endif
