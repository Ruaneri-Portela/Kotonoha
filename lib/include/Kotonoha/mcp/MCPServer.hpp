#pragma once

#include <memory>
#include <string>

namespace Kotonoha {

class Engine;

// Opt-in stdio transport. All protocol and engine operations run on the SDL
// thread.
class MCPServer {
  struct State;
  std::unique_ptr<State> state;

public:
  MCPServer(Engine &engine, const std::string &fileRoot);
  ~MCPServer();
  MCPServer(const MCPServer &) = delete;
  MCPServer &operator=(const MCPServer &) = delete;
  bool pump();
};

} // namespace Kotonoha
