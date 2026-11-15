#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/mcp/MCPServer.hpp>

#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle invalidSocket = -1;
void closeSocket(SocketHandle socket) { close(socket); }
#endif

void require(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}

struct SocketStartup {
  SocketStartup() {
#ifdef _WIN32
    WSADATA data{};
    require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock startup");
#endif
  }
  ~SocketStartup() {
#ifdef _WIN32
    WSACleanup();
#endif
  }
};

struct Socket {
  SocketHandle handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  Socket() { require(handle != invalidSocket, "Create TCP socket"); }
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
  ~Socket() {
    if (handle != invalidSocket)
      closeSocket(handle);
  }
  void shutdown() {
    if (handle != invalidSocket) {
      closeSocket(handle);
      handle = invalidSocket;
    }
  }
};

sockaddr_in loopback(unsigned short port) {
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  local.sin_port = htons(port);
  return local;
}

unsigned short freeLoopbackPort() {
  Socket probe;
  auto local = loopback(0);
  require(bind(probe.handle, reinterpret_cast<const sockaddr *>(&local),
               sizeof(local)) == 0,
          "Bind local test port");
#ifdef _WIN32
  int length = sizeof(local);
#else
  socklen_t length = sizeof(local);
#endif
  require(getsockname(probe.handle, reinterpret_cast<sockaddr *>(&local),
                      &length) == 0,
          "Discover local test port");
  return ntohs(local.sin_port);
}

void connectTo(Socket &client, unsigned short port) {
  const auto local = loopback(port);
  require(connect(client.handle, reinterpret_cast<const sockaddr *>(&local),
                  sizeof(local)) == 0,
          "Connect local MCP client");
#ifdef _WIN32
  DWORD timeout = 2000;
#else
  timeval timeout{2, 0};
#endif
  require(setsockopt(client.handle, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&timeout),
                     sizeof(timeout)) == 0,
          "Set test socket timeout");
}

void sendLine(Socket &client, const std::string &line) {
  const auto message = line + "\n";
  std::size_t offset = 0;
  while (offset < message.size()) {
    const auto sent = ::send(client.handle, message.data() + offset,
                             static_cast<int>(message.size() - offset), 0);
    require(sent > 0, "Send MCP request");
    offset += static_cast<std::size_t>(sent);
  }
}

std::string receiveLine(Socket &client, const char *stage) {
  std::string response;
  char bytes[1024];
  while (response.find('\n') == std::string::npos) {
    const auto read = recv(client.handle, bytes, sizeof(bytes), 0);
    if (read <= 0) {
#ifdef _WIN32
      throw std::runtime_error(std::string("Receive MCP response (") + stage +
                               ") failed: recv=" +
                               std::to_string(read) + " WSA=" +
                               std::to_string(WSAGetLastError()));
#else
      throw std::runtime_error(std::string("Receive MCP response (") + stage +
                               ") failed: recv=" +
                               std::to_string(read));
#endif
    }
    require(response.size() + static_cast<std::size_t>(read) <= 65536,
            "Receive bounded MCP response");
    response.append(bytes, static_cast<std::size_t>(read));
  }
  return response.substr(0, response.find('\n'));
}

bool socketReadable(Socket &client) {
  fd_set ready;
  FD_ZERO(&ready);
  FD_SET(client.handle, &ready);
  timeval noWait{0, 0};
#ifdef _WIN32
  const int result = select(0, &ready, nullptr, nullptr, &noWait);
#else
  const int result = select(client.handle + 1, &ready, nullptr, nullptr, &noWait);
#endif
  require(result >= 0, "Cannot poll reconnect socket");
  return result > 0 && FD_ISSET(client.handle, &ready);
}

void pumpUntilResponse(Kotonoha::MCPServer &server, Socket &client,
                       const char *stage) {
  // TCP connect(), send() and receive readiness are asynchronous even on
  // loopback. Do not assume one zero-delay engine tick always sees packets.
  // This tests prompt processing without artificial sleep or blocking the
  // engine on socket I/O. A real game pumps continuously every frame.
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  unsigned pumps = 0;
  do {
    require(server.pump(), "TCP MCP must not exit the engine");
    ++pumps;
    if (socketReadable(client))
      return;
    std::this_thread::yield();
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error(std::string("MCP ") + stage +
                           " did not respond within 2s after " +
                           std::to_string(pumps) + " pump iterations");
}

struct EmptyRuntime : Kotonoha::Runtime {
  EmptyRuntime() : Runtime("MCP Transport Test", "1", "mcp.transport.test") {}
  void init(Kotonoha::Engine &) override {}
};

void testSocket() {
  SocketStartup network;
  const auto port = freeLoopbackPort();
  EmptyRuntime runtime;
  Kotonoha::EngineOptions options;
  options.rendererOverride = "software";
  Kotonoha::Engine engine(runtime, options);
  Kotonoha::MCPServer server(engine, std::filesystem::current_path().string(),
                             port);

  Socket first;
  connectTo(first, port);
  sendLine(first, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
  pumpUntilResponse(server, first, "first.initialize");
  require(receiveLine(first, "first.initialize").find("kotonoha-engine") != std::string::npos,
          "TCP initializes using the native engine dispatcher");

  sendLine(first, R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  sendLine(first, R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
  pumpUntilResponse(server, first, "first.tools/list");
  require(receiveLine(first, "first.tools/list").find("engine_state") != std::string::npos,
          "TCP exposes native engine MCP tools");

  first.shutdown();
  Socket second;
  connectTo(second, port);
  sendLine(second, R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
  // Reconnect before pumping the closed first socket. Accept-before-read
  // used to reset this connection with WSAECONNRESET on Windows.
  // Pump until the new socket has an actual response, not an arbitrary number
  // of CPU-speed frames. On Windows a FIN can be delivered after 32 tight
  // iterations even though ordinary engine frames are spaced at ~16 ms.
  // Yield rather than sleeping: reconnect must not need an artificial delay.
  pumpUntilResponse(server, second, "second.uninitialized.tools/list");
  require(receiveLine(second, "second.uninitialized.tools/list").find("-32002") != std::string::npos,
          "Immediate reconnect must remain open with fresh MCP handshake");
  sendLine(second, R"({"jsonrpc":"2.0","id":4,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"reconnect-test","version":"1"}}})");
  pumpUntilResponse(server, second, "second.initialize");
  require(receiveLine(second, "second.initialize").find("kotonoha-engine") != std::string::npos,
          "Reconnected client shares the native MCP dispatcher");
}
} // namespace

int main() {
  try {
    testSocket();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MCPTransportTests failure: " << error.what() << '\n';
    return 1;
  }
}
