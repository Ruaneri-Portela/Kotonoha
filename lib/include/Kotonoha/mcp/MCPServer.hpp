#pragma once

#include <memory>
#include <string>

namespace Kotonoha {

class Engine;

// MCP transport: stdio (default) or a local TCP socket.  All protocol and
// engine operations run on the SDL thread.
class MCPServer {
  struct State;
  std::unique_ptr<State> state;

public:
  // Stdio transport (JSON-RPC lines on stdin/stdout).
  MCPServer(Engine &engine, const std::string &fileRoot);
  // TCP socket transport listening on `port` (127.0.0.1).  Use this to drive
  // the engine from an external MCP client over a loopback socket instead of
  // stdio.  Pass port == 0 to select stdio.  Available on Linux, macOS and
  // Windows.
  MCPServer(Engine &engine, const std::string &fileRoot, int port);
  ~MCPServer();
  MCPServer(const MCPServer &) = delete;
  MCPServer &operator=(const MCPServer &) = delete;
  bool pump();
};

} // namespace Kotonoha
