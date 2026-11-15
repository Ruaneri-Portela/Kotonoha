#pragma once

#include <memory>
#include <string>

namespace Kotonoha {

class Engine;

// Opt-in stdio or loopback TCP transport. All protocol and engine operations
// run on the SDL thread; socket polling never blocks the game loop.
class MCPServer {
  struct State;
  std::unique_ptr<State> state;

public:
  MCPServer(Engine &engine, const std::string &fileRoot,
            unsigned short tcpPort = 0);
  ~MCPServer();
  MCPServer(const MCPServer &) = delete;
  MCPServer &operator=(const MCPServer &) = delete;
  bool pump();
};

} // namespace Kotonoha
