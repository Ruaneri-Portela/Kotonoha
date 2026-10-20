#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL.h>

#include <Kotonoha/utils/Path.h>

#include <stdlib.h>
#include <string.h>

static char *KotonohaPathDuplicateString(const char *text) {
  size_t length;
  char *copy;

  if (text == NULL) {
    return NULL;
  }

  length = SDL_strlen(text);

  copy = (char *)SDL_malloc(length + 1);
  if (copy == NULL) {
    return NULL;
  }

  SDL_memcpy(copy, text, length);
  copy[length] = '\0';

  return copy;
}

static bool KotonohaPathHasTrailingSeparator(const char *path) {
  size_t length;

  if (path == NULL) {
    return false;
  }

  length = SDL_strlen(path);

  if (length == 0) {
    return false;
  }

#if defined(_WIN32)
  return path[length - 1] == '\\' || path[length - 1] == '/';
#else
  return path[length - 1] == '/';
#endif
}

static char *KotonohaPathNormalizeSeparators(const char *path) {
  char *copy;
  size_t i;
  size_t length;

  if (path == NULL) {
    return NULL;
  }

  copy = KotonohaPathDuplicateString(path);
  if (copy == NULL) {
    return NULL;
  }

#if defined(_WIN32)
  length = SDL_strlen(copy);

  for (i = 0; i < length; i++) {
    if (copy[i] == '/') {
      copy[i] = '\\';
    }
  }
#endif

  return copy;
}

bool KotonohaPathIsAbsolute(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return false;
  }

#if defined(_WIN32)
  /* C:\foo ou C:/foo */
  if (SDL_isalpha((unsigned char)path[0]) && path[1] == ':') {
    return true;
  }

  /* \\server\share ou //server/share */
  if ((path[0] == '\\' || path[0] == '/') &&
      (path[1] == '\\' || path[1] == '/')) {
    return true;
  }

  return false;
#else
  /* /usr/share/fonts, /private/var/..., etc. */
  return path[0] == '/';
#endif
}

char *KotonohaPathGetAbsolute(const char *path) {
  const char *basePath;
  const char *separator;
  size_t basePathLength;
  size_t pathLength;
  size_t separatorLength;
  size_t totalLength;
  char *result;
  bool baseHasSeparator;

  if (path == NULL || path[0] == '\0') {
    return NULL;
  }

  if (KotonohaPathIsAbsolute(path)) {
    return KotonohaPathNormalizeSeparators(path);
  }

  basePath = SDL_GetCurrentDirectory();
  if (basePath == NULL) {
    KLOG_WARN(CORE, "Failed to get base path: %s", SDL_GetError());
    return NULL;
  }

  separator = KotonohaPath_SEPARATOR;
  separatorLength = SDL_strlen(separator);

  basePathLength = SDL_strlen(basePath);
  pathLength = SDL_strlen(path);

  baseHasSeparator = false;

  if (basePathLength > 0) {
#if defined(_WIN32)
    baseHasSeparator = basePath[basePathLength - 1] == '\\' ||
                       basePath[basePathLength - 1] == '/';
#else
    baseHasSeparator = basePath[basePathLength - 1] == '/';
#endif
  }

  totalLength = basePathLength + (baseHasSeparator ? 0 : separatorLength) +
                pathLength + 1;

  result = (char *)SDL_malloc(totalLength);
  if (result == NULL) {
    return NULL;
  }

  SDL_memcpy(result, basePath, basePathLength);

  if (!baseHasSeparator) {
    SDL_memcpy(result + basePathLength, separator, separatorLength);
  }

  SDL_memcpy(result + basePathLength + (baseHasSeparator ? 0 : separatorLength),
             path, pathLength);

  result[totalLength - 1] = '\0';

#if defined(_WIN32)
  {
    size_t i;

    for (i = 0; i < totalLength - 1; i++) {
      if (result[i] == '/') {
        result[i] = '\\';
      }
    }
  }
#endif

  return result;
}

char *KotonohaPathJoin(const char *base, const char *relative) {
  const char *separator;
  size_t baseLength;
  size_t relativeLength;
  size_t separatorLength;
  size_t totalLength;
  char *result;
  bool baseHasSeparator;

  if (base == NULL || base[0] == '\0' || relative == NULL ||
      relative[0] == '\0') {
    return NULL;
  }

  if (KotonohaPathIsAbsolute(relative)) {
    return KotonohaPathNormalizeSeparators(relative);
  }

  separator = KotonohaPath_SEPARATOR;
  separatorLength = SDL_strlen(separator);

  baseLength = SDL_strlen(base);
  relativeLength = SDL_strlen(relative);

  baseHasSeparator = KotonohaPathHasTrailingSeparator(base);

  totalLength = baseLength + (baseHasSeparator ? 0 : separatorLength) +
                relativeLength + 1;

  result = (char *)SDL_malloc(totalLength);
  if (result == NULL) {
    return NULL;
  }

  SDL_memcpy(result, base, baseLength);

  if (!baseHasSeparator) {
    SDL_memcpy(result + baseLength, separator, separatorLength);
  }

  SDL_memcpy(result + baseLength + (baseHasSeparator ? 0 : separatorLength),
             relative, relativeLength);

  result[totalLength - 1] = '\0';

  char *normalized = KotonohaPathNormalizeSeparators(result);
  SDL_free(result);
  return normalized;
}