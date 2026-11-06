#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <memory>
#include <string>

namespace SchoolDays {
class Runtime;

// Windows-only, explicitly enabled local diagnostic transport. It never
// listens beyond 127.0.0.1 and never accepts paths or shell commands.
class DevBridge {
public:
  explicit DevBridge(unsigned short port);
  ~DevBridge();
  DevBridge(const DevBridge &) = delete;
  DevBridge &operator=(const DevBridge &) = delete;
  void poll(Runtime &runtime, Kotonoha::RendererRef renderer);
  void trace(const std::string &event, const Runtime &runtime);

private:
  struct State;
  std::unique_ptr<State> state;
};
} // namespace SchoolDays
