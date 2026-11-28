#include <Kotonoha/integrations/RichPresence.hpp>

#include <json-c/json.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <poll.h>

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
std::string socketPath() {
  const char *roots[] = {std::getenv("XDG_RUNTIME_DIR"), std::getenv("TMPDIR"),
                         "/tmp", "/var/tmp"};
  for (const char *root : roots) {
    if (!root || !*root)
      continue;
    for (int index = 0; index < 10; ++index) {
      const std::string path =
          std::string(root) + "/discord-ipc-" + std::to_string(index);
      if (::access(path.c_str(), W_OK) == 0)
        return path;
    }
  }
  return {};
}

class DiscordSocket final : public PresenceTransport {
public:
  DiscordSocket(std::string id, std::string image)
      : id_(std::move(id)), image_(std::move(image)) {}
  ~DiscordSocket() override { close(); }

  bool connect() override {
    close();
    const auto path = socketPath();
    if (path.empty())
      return false;
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0)
      return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (::connect(fd_, reinterpret_cast<sockaddr *>(&address),
                  sizeof(address)) != 0) {
      close();
      return false;
    }
    return send(0, "{\"v\":1,\"client_id\":" + quote(id_) + "}") &&
           waitReply(true);
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
    return activity(body + "}") && waitReply(false);
  }

  void clear() override { activity("null"); }

  void close() noexcept override {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = -1;
  }

private:
  bool transfer(void *data, std::size_t length, bool writing) {
    std::size_t done = 0;
    while (done < length) {
      const ssize_t count = writing
          ? ::write(fd_, static_cast<const char *>(data) + done, length - done)
          : ::read(fd_, static_cast<char *>(data) + done, length - done);
      if (count > 0) {
        done += static_cast<std::size_t>(count);
        continue;
      }
      if (count < 0 && (errno == EINTR || errno == EAGAIN))
        continue;
      return false;
    }
    return true;
  }

  bool send(std::uint32_t opcode, const std::string &json) {
    const auto length = static_cast<std::uint32_t>(json.size());
    std::vector<char> frame(8 + length);
    std::memcpy(frame.data(), &opcode, 4);
    std::memcpy(frame.data() + 4, &length, 4);
    std::memcpy(frame.data() + 8, json.data(), length);
    return transfer(frame.data(), frame.size(), true);
  }

  bool activity(const std::string &payload) {
    nonce_ = std::to_string(++sequence_);
    return send(1, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":" + quote(nonce_) +
                       ",\"args\":{\"pid\":" + std::to_string(::getpid()) +
                       ",\"activity\":" + payload + "}}");
  }

  bool waitReply(bool handshake) {
    const auto until = Clock::now() + std::chrono::milliseconds(900);
    do {
      bool received = false;
      if (!pump(&received, handshake))
        return false;
      if (received)
        return true;
      ::poll(nullptr, 0, 10);
    } while (Clock::now() < until);
    return false;
  }

  bool pump(bool *received, bool handshake) {
    std::array<std::uint32_t, 2> header{};
    // Read the header without consuming it when the body is not ready yet.
    ssize_t count = ::recv(fd_, header.data(), sizeof(header), MSG_PEEK);
    if (count == 0)
      return false;
    if (count < 0)
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    if (static_cast<std::size_t>(count) < sizeof(header))
      return true;
    const auto length = header[1];
    if (length > 64 * 1024)
      return false;
    std::vector<char> raw(sizeof(header) + length);
    if (!transfer(raw.data(), raw.size(), false))
      return false;
    std::string body(raw.data() + sizeof(header), length);
    if (header[0] == 3)
      return send(4, body);
    if (header[0] != 1)
      return header[0] == 4;
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
