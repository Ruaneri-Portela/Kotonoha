#pragma once

#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL.h>
#include <string>

// Optional Model Context Protocol server that lets an external agent inspect
// and drive the running engine (state, screenshots, logs, buffers, input,
// playback).  It speaks JSON-RPC over HTTP ("streamable HTTP", POST /mcp),
// listens on 127.0.0.1 only and is disabled unless requested:
//
//   --mcp [port]            command line (default port 8765)
//   KOTONOHA_MCP_PORT=port  environment
//
// Every tool executes on the main thread, inside Service(), so the engine
// never sees concurrent access from the network thread.
namespace Kotonoha::Debug::Mcp {

constexpr int kDefaultPort = 8765;

// A tool contributed by a game.  `argumentsJson` is the raw JSON object of the
// call arguments; the handler returns the text shown to the client and sets
// *isError.  It runs on the main thread inside Service().
struct ToolRegistration {
  std::string name;
  std::string description;
  std::string schemaJson;
  std::string (*handler)(::Kotonoha::Kotonoha *game,
                         ::Kotonoha::Gameplay *running,
                         const std::string &argumentsJson, bool *isError,
                         void *userdata);
  void *userdata = nullptr;
};

// Adds (or replaces) a tool.  Call before or after Start(); thread-safe.
void RegisterTool(ToolRegistration tool);

// Looks for "--mcp [port]" (marking it consumed like the other engine
// switches) or KOTONOHA_MCP_PORT.  Returns the port, or 0 when disabled.
int ParseArguments(int argc, char *argv[]);

// Starts listening; returns false (and logs) when the socket cannot be bound.
bool Start(int port);
void Stop();
bool IsRunning();

// Runs the queued tool calls.  Call it on the main thread right before
// SDL_RenderPresent, once the frame has been fully drawn, so screenshots show
// exactly what is about to be presented.  `running` may be null.
void Service(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *running);

} // namespace Kotonoha::Debug::Mcp
