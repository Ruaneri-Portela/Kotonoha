#pragma once

#include <stdbool.h>

#if defined(_WIN32)
#define KotonohaPath_SEPARATOR "\\"
#else
#define KotonohaPath_SEPARATOR "/"
#endif

char *KotonohaPathGetAbsolute(const char *path);

bool KotonohaPathIsAbsolute(const char *path);

char *KotonohaPathJoin(const char *base, const char *relative);