#pragma once

#include <memory>
#include <string>

namespace Kotonoha {

class Engine;

// Stdio and loopback TCP transports; all protocol and engine operations
// remain on the SDL thread, as in the upstream implementation.
class MCPServer {
  struct State;
  std::unique_ptr<State> state;

public:
  MCPServer(Engine &engine, const std::string &fileRoot);
  MCPServer(Engine &engine, const std::string &fileRoot, int port);
  ~MCPServer();
  MCPServer(const MCPServer &) = delete;
  MCPServer &operator=(const MCPServer &) = delete;
  bool pump();
};

} // namespace Kotonoha
