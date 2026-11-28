#include <Kotonoha/integrations/RichPresence.hpp>

#include <json-c/json.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <poll.h>
#include <fcntl.h>
#include <cstddef>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kotonoha::Integrations {
namespace {

using Clock = std::chrono::steady_clock;
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;

std::string quote(const std::string &text) {
  Json value(json_object_new_string_len(text.data(),
                                        static_cast<int>(text.size())),
             json_object_put);
  if (!value)
    throw std::bad_alloc();
  return json_object_to_json_string_ext(value.get(), JSON_C_TO_STRING_PLAIN);
}

const char *field(json_object *obj, const char *key) {
  json_object *item = nullptr;
  return json_object_object_get_ex(obj, key, &item) &&
                 json_object_is_type(item, json_type_string)
             ? json_object_get_string(item)
             : "";
}

// Discord exposes IPC sockets under XDG_RUNTIME_DIR, TMPDIR and /tmp. Try all
// ten slots on each root so multiple Discord instances/clients are supported.
std::vector<std::string> socketPaths() {
  std::vector<std::string> paths;
#ifdef __APPLE__
  // Finder-launched apps may not inherit the shell's TMPDIR.
  std::string darwinTemp;
  const std::size_t size = ::confstr(_CS_DARWIN_USER_TEMP_DIR, nullptr, 0);
  if (size > 0) {
    darwinTemp.resize(size);
    if (::confstr(_CS_DARWIN_USER_TEMP_DIR, darwinTemp.data(), size) == size)
      darwinTemp.resize(size - 1);
    else
      darwinTemp.clear();
  }
#else
  const std::string darwinTemp;
#endif
  const char *roots[] = {std::getenv("XDG_RUNTIME_DIR"), std::getenv("TMPDIR"),
                         darwinTemp.c_str(), "/tmp", "/var/tmp"};
  for (const char *root : roots) {
    if (!root || !*root)
      continue;
    for (int index = 0; index < 10; ++index) {
      std::string path(root);
      if (path.back() != '/')
        path += '/';
      path += "discord-ipc-" + std::to_string(index);
      if (std::find(paths.begin(), paths.end(), path) == paths.end() &&
          ::access(path.c_str(), F_OK) == 0)
        paths.push_back(path);
    }
  }
  return paths;
}

class DiscordSocket final : public PresenceTransport {
public:
  DiscordSocket(std::string id, std::string image)
      : id_(std::move(id)), image_(std::move(image)) {}
  ~DiscordSocket() override { close(); }

  bool connect() override {
    close();
    const auto deadline = Clock::now() + std::chrono::milliseconds(900);
    for (const auto &path : socketPaths()) {
      if (Clock::now() >= deadline)
        break;
      sockaddr_un address{};
      if (path.size() >= sizeof(address.sun_path))
        continue;
      address.sun_family = AF_UNIX;
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      const auto size = static_cast<socklen_t>(
          offsetof(sockaddr_un, sun_path) + path.size() + 1);
#ifdef __APPLE__
      address.sun_len = static_cast<unsigned char>(size);
#endif
      fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
      if (fd_ < 0)
        continue;
      const int flags = ::fcntl(fd_, F_GETFL, 0);
      if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0 ||
          ::fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0) {
        close();
        continue;
      }
#ifdef SO_NOSIGPIPE
      const int yes = 1;
      if (::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) != 0) {
        close();
        continue;
      }
#endif
      const auto attempt = deadline;
      if (::connect(fd_, reinterpret_cast<sockaddr *>(&address), size) != 0) {
        int error = 0;
        socklen_t errorSize = sizeof(error);
        if (errno != EINPROGRESS || !ready(POLLOUT, attempt) ||
            ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &errorSize) != 0 ||
            error != 0) {
          close();
          continue;
        }
      }
      if (send(0, "{\"v\":1,\"client_id\":" + quote(id_) + "}", attempt) &&
          waitReply(true, attempt))
        return true;
      close();
    }
    return false;
  }

  bool healthy() override { return pump(nullptr, false); }

  bool publish(const std::string &details, const std::string &state,
               std::int64_t start) override {
    std::string body = "{\"type\":0,\"details\":" + quote(details) +
                       ",\"state\":" + quote(state) +
                       ",\"timestamps\":{\"start\":" + std::to_string(start) +
                       "}";
    if (!image_.empty())
      body += ",\"assets\":{\"large_image\":" + quote(image_) + "}";
    const auto deadline = Clock::now() + std::chrono::milliseconds(900);
    return activity(body + "}", deadline) && waitReply(false, deadline);
  }

  void clear() override {
    activity("null", Clock::now() + std::chrono::milliseconds(900));
  }

  void close() noexcept override {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = -1;
    incoming_.clear();
  }

private:
  bool ready(short events, Clock::time_point deadline) {
    while (Clock::now() < deadline) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - Clock::now()).count();
      pollfd descriptor{fd_, events, 0};
      const int result = ::poll(&descriptor, 1, static_cast<int>(remaining + 1));
      if (result > 0)
        return (descriptor.revents & events) != 0;
      if (result == 0 || errno != EINTR)
        return false;
    }
    return false;
  }

  bool send(std::uint32_t opcode, const std::string &json,
            Clock::time_point deadline) {
    if (json.size() > 64 * 1024)
      return false;
    const auto length = static_cast<std::uint32_t>(json.size());
    std::vector<char> frame(8 + length);
    std::memcpy(frame.data(), &opcode, 4);
    std::memcpy(frame.data() + 4, &length, 4);
    std::memcpy(frame.data() + 8, json.data(), length);
    std::size_t done = 0;
    while (done < frame.size()) {
      if (Clock::now() >= deadline)
        return false;
#ifdef MSG_NOSIGNAL
      constexpr int flags = MSG_NOSIGNAL;
#else
      constexpr int flags = 0;
#endif
      const ssize_t count = ::send(fd_, frame.data() + done,
                                   frame.size() - done, flags);
      if (count > 0) {
        done += static_cast<std::size_t>(count);
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!ready(POLLOUT, deadline))
          return false;
      } else {
        return false;
      }
    }
    return true;
  }

  bool activity(const std::string &payload, Clock::time_point deadline) {
    nonce_ = std::to_string(++sequence_);
    return send(1, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":" + quote(nonce_) +
                       ",\"args\":{\"pid\":" + std::to_string(::getpid()) +
                       ",\"activity\":" + payload + "}}", deadline);
  }

  bool waitReply(bool handshake, Clock::time_point deadline) {
    while (Clock::now() < deadline) {
      bool received = false;
      if (!pump(&received, handshake))
        return false;
      if (received)
        return true;
      if (!ready(POLLIN, deadline))
        return false;
    }
    return false;
  }

  bool pump(bool *received, bool handshake) {
    // Nonblocking reads preserve fragmented headers and payloads between calls.
    // Bound the amount of work even if the peer continuously sends events.
    for (unsigned iteration = 0; iteration < 32; ++iteration) {
      std::array<std::uint32_t, 2> header{};
      if (incoming_.size() >= sizeof(header)) {
        std::memcpy(header.data(), incoming_.data(), sizeof(header));
        const auto length = header[1];
        if (length > 64 * 1024)
          return false;
        if (incoming_.size() >= sizeof(header) + length) {
          std::string body(incoming_.data() + sizeof(header), length);
          incoming_.erase(incoming_.begin(),
                          incoming_.begin() + sizeof(header) + length);
          if (!handle(header[0], body, received, handshake))
            return false;
          if (received && *received)
            return true;
          continue;
        }
      }
      std::array<char, 4096> chunk{};
      const ssize_t count = ::recv(fd_, chunk.data(), chunk.size(), 0);
      if (count > 0) {
        incoming_.insert(incoming_.end(), chunk.data(), chunk.data() + count);
      } else if (count == 0) {
        return false;
      } else if (errno == EINTR) {
        continue;
      } else {
        return errno == EAGAIN || errno == EWOULDBLOCK;
      }
    }
    // A busy peer must not starve the session or leave buffered frames waiting
    // for an unrelated poll event.
    return incoming_.empty();
  }

  bool handle(std::uint32_t opcode, const std::string &body,
              bool *received, bool handshake) {
    if (opcode == 3)
      return send(4, body, Clock::now() + std::chrono::milliseconds(100));
    if (opcode != 1)
      return opcode == 4;
    Json obj(json_tokener_parse(body.c_str()), json_object_put);
    if (!obj || !json_object_is_type(obj.get(), json_type_object) ||
        std::strcmp(field(obj.get(), "evt"), "ERROR") == 0)
      return false;
    if (received) {
      *received = handshake
          ? std::strcmp(field(obj.get(), "evt"), "READY") == 0
          : std::strcmp(field(obj.get(), "cmd"), "SET_ACTIVITY") == 0 &&
                nonce_ == field(obj.get(), "nonce");
    }
    return true;
  }

  std::string id_, image_, nonce_;
  std::vector<char> incoming_;
  std::uint64_t sequence_ = 0;
  int fd_ = -1;
};

} // namespace

bool desktopPresenceAvailable() noexcept { return true; }

std::unique_ptr<PresenceTransport> discordDesktop(std::string id,
                                                   std::string image) {
  return std::make_unique<DiscordSocket>(std::move(id), std::move(image));
}

} // namespace Kotonoha::Integrations
