// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Universal detached game host. The launcher core spawns this executable with
// --kotonoha-detached-run <request-json>. It runs the game on its own fresh
// main thread, avoiding fork() inside a live AppKit/SwiftUI/SDL process.
#include <KotonohaLauncher/LauncherBridge.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

int main(int argc, char **argv) {
#if defined(__APPLE__) || defined(__linux__)
  // Frontends set this path so their UI can tail the game's output.
  if (const char *logPath = std::getenv("KOTONOHA_DETACHED_LOG")) {
    const int fd = ::open(logPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
      ::dup2(fd, STDOUT_FILENO);
      ::dup2(fd, STDERR_FILENO);
      if (fd > STDERR_FILENO)
        ::close(fd);
    }
  }
#endif
  const char *json = nullptr;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--kotonoha-detached-run") {
      json = argv[i + 1];
      break;
    }
  }
  if (!json) {
    if (const char *env = std::getenv("KOTONOHA_LAUNCH_REQUEST_JSON"))
      json = env;
  }
  if (!json || !*json) {
    std::fprintf(stderr, "[KotonohaGameHost] missing launch request\n");
    return 2;
  }
  KotonohaLauncherRunGameJson(json);
  return 0;
}
