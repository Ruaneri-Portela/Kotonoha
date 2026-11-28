#include <Kotonoha/integrations/RichPresence.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace Kotonoha::Integrations;

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

struct Endpoint {
  int fd = -1;
  std::string path;
  ~Endpoint() {
    if (fd >= 0)
      ::close(fd);
    if (!path.empty())
      ::unlink(path.c_str());
  }
};

void bindEndpoint(Endpoint &endpoint, const std::string &path) {
  endpoint.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  require(endpoint.fd >= 0, "socket");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  require(path.size() < sizeof(address.sun_path), "socket path");
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  const auto size = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + path.size() + 1);
#ifdef __APPLE__
  address.sun_len = static_cast<unsigned char>(size);
#endif
  require(::bind(endpoint.fd, reinterpret_cast<sockaddr *>(&address), size) == 0,
          "bind");
  endpoint.path = path;
}

void readAll(int fd, void *buffer, std::size_t size) {
  auto *out = static_cast<char *>(buffer);
  while (size) {
    const auto n = ::read(fd, out, size);
    require(n > 0, "read IPC frame");
    out += n;
    size -= static_cast<std::size_t>(n);
  }
}

std::string readFrame(int fd, std::uint32_t expected) {
  std::uint32_t header[2]{};
  readAll(fd, header, sizeof(header));
  require(header[0] == expected && header[1] < 65536, "IPC header");
  std::string body(header[1], '\0');
  readAll(fd, body.data(), body.size());
  return body;
}

void writeAll(int fd, const char *buffer, std::size_t size) {
  while (size) {
#ifdef MSG_NOSIGNAL
    const auto n = ::send(fd, buffer, size, MSG_NOSIGNAL);
#else
    const auto n = ::send(fd, buffer, size, 0);
#endif
    require(n > 0, "write IPC frame");
    buffer += n;
    size -= static_cast<std::size_t>(n);
  }
}

void frame(int fd, std::uint32_t opcode, const std::string &body) {
  std::uint32_t header[2]{opcode, static_cast<std::uint32_t>(body.size())};
  const char *bytes = reinterpret_cast<const char *>(header);
  writeAll(fd, bytes, 3);
  std::this_thread::sleep_for(5ms);
  writeAll(fd, bytes + 3, sizeof(header) - 3);
  if (!body.empty()) {
    writeAll(fd, body.data(), 1);
    std::this_thread::sleep_for(5ms);
    writeAll(fd, body.data() + 1, body.size() - 1);
  }
}
} // namespace

int main() {
  char temporary[] = "/tmp/kotonoha-rpc-XXXXXX";
  const char *directory = ::mkdtemp(temporary);
  if (!directory)
    return 1;
  int result = 0;
  try {
    const std::string root(directory);
    require(::setenv("XDG_RUNTIME_DIR", directory, 1) == 0, "setenv");
    Endpoint stale, listener;
    bindEndpoint(stale, root + "/discord-ipc-0"); // exists but not listening
    bindEndpoint(listener, root + "/discord-ipc-1");
    require(::listen(listener.fd, 1) == 0, "listen");
    std::exception_ptr serverError;
    std::thread server([&] {
      try {
        Endpoint peer;
        peer.fd = ::accept(listener.fd, nullptr, nullptr);
        require(peer.fd >= 0, "accept");
#ifdef SO_NOSIGPIPE
        const int yes = 1;
        require(::setsockopt(peer.fd, SOL_SOCKET, SO_NOSIGPIPE, &yes,
                             sizeof(yes)) == 0, "SO_NOSIGPIPE");
#endif
        require(readFrame(peer.fd, 0).find("12345") != std::string::npos,
                "handshake client ID");
        frame(peer.fd, 1, "{\"evt\":\"READY\"}");
        const auto activity = readFrame(peer.fd, 1);
        require(activity.find("SET_ACTIVITY") != std::string::npos &&
                    activity.find("Example") != std::string::npos, "activity");
        frame(peer.fd, 3, "ping");
        require(readFrame(peer.fd, 4) == "ping", "pong");
        frame(peer.fd, 1, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"1\"}");
        require(readFrame(peer.fd, 1).find("\"activity\":null") !=
                    std::string::npos, "clear");
      } catch (...) {
        serverError = std::current_exception();
      }
    });
    auto transport = discordDesktop("12345", "");
    const bool connected = transport && transport->connect();
    const auto start = std::chrono::steady_clock::now();
    const bool healthy = connected && transport->healthy();
    const bool responsive = std::chrono::steady_clock::now() - start < 500ms;
    const bool published = healthy && transport->publish("Example", "Menu", 123);
    if (published)
      transport->clear();
    if (transport)
      transport->close();
    // Wake accept if a failed connection never reached the listening socket.
    ::shutdown(listener.fd, SHUT_RDWR);
    server.join();
    if (serverError)
      std::rethrow_exception(serverError);
    require(connected && healthy && responsive && published, "RPC round trip");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  ::rmdir(directory);
  return result;
}
