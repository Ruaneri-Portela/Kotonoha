#pragma once

#include <stdbool.h>
#include <string>
#include <filesystem>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

inline bool KotonohaPathIsAbsolute(const char *path) {
  if (path == nullptr || *path == '\0') {
    return false;
  }
  std::error_code ec;
  return std::filesystem::path(path).is_absolute();
}

inline char *KotonohaPathGetAbsolute(const char *path) {
  if (path == nullptr || *path == '\0') {
    return nullptr;
  }
  std::error_code ec;
  auto abs = std::filesystem::absolute(path, ec);
  if (ec) {
    return nullptr;
  }
  std::string s = abs.string();
  char *result = static_cast<char *>(SDL_malloc(s.size() + 1));
  if (result != nullptr) {
    SDL_memcpy(result, s.c_str(), s.size() + 1);
  }
  return result;
}

inline char *KotonohaPathJoin(const char *base, const char *relative) {
  if (base == nullptr || relative == nullptr) {
    return nullptr;
  }
  std::filesystem::path p(base);
  p /= relative;
  std::string s = p.string();
  char *result = static_cast<char *>(SDL_malloc(s.size() + 1));
  if (result != nullptr) {
    SDL_memcpy(result, s.c_str(), s.size() + 1);
  }
  return result;
}

#ifdef __cplusplus
}
#endif
