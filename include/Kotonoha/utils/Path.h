#pragma  once

#include <stdbool.h>

#if defined(_WIN32)
#define KOTONOHA_PATH_SEPARATOR "\\"
#else
#define KOTONOHA_PATH_SEPARATOR "/"
#endif

char *Kotonoha_GetAbsolutePath(const char *path);

bool Kotonoha_IsAbsolutePath(const char *path);

char *Kotonoha_JoinPath(const char *base, const char *relative);