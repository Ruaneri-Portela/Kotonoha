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

  return path[length - 1] == '/' || path[length - 1] == '\\';
}

static char *KotonohaPathNormalizeSeparators(const char *path) {
  /* '/' is accepted by every platform SDL runs on, so it is the only
     separator the engine produces. */
  return KotonohaPathDuplicateString(path);
}

bool KotonohaPathIsAbsolute(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return false;
  }

  /* Drive paths (C:/foo, C:\foo) and UNC paths (//server/share). */
  if (SDL_isalpha((unsigned char)path[0]) && path[1] == ':' &&
      (path[2] == '/' || path[2] == '\\')) {
    return true;
  }
  if ((path[0] == '\\' || path[0] == '/') &&
      (path[1] == '\\' || path[1] == '/')) {
    return true;
  }
  return path[0] == '/';
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
    baseHasSeparator = basePath[basePathLength - 1] == '\\' ||
                       basePath[basePathLength - 1] == '/';
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

  return result;
}