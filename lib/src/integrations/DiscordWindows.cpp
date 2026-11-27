#include <Kotonoha/integrations/RichPresence.hpp>

#define NOMINMAX
#include <windows.h>
#include <json-c/json.h>

#include <array>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Kotonoha::Integrations {
namespace {
using Clock = std::chrono::steady_clock;
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;

std::string quote(const std::string &text) {
  Json value(json_object_new_string_len(text.data(), static_cast<int>(text.size())),
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

class DiscordPipe final : public PresenceTransport {
public:
  DiscordPipe(std::string id, std::string image)
      : id_(std::move(id)), image_(std::move(image)) {}
  ~DiscordPipe() override { close(); }

  bool connect() override {
    close();
    for (int index = 0; index < 10; ++index) {
      const auto path = LR"(\\?\pipe\discord-ipc-)" + std::to_wstring(index);
      pipe_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
                          SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
      if (pipe_ != INVALID_HANDLE_VALUE)
        break;
    }
    if (pipe_ == INVALID_HANDLE_VALUE)
      return false;
    event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return event_ && send(0, "{\"v\":1,\"client_id\":" + quote(id_) + "}") &&
           waitReply(true);
  }

  bool healthy() override { return pump(nullptr, false); }

  bool publish(const std::string &details, std::int64_t start) override {
    std::string body = "{\"type\":0,\"details\":" + quote(details) +
                       ",\"timestamps\":{\"start\":" + std::to_string(start) + "}";
    if (!image_.empty())
      body += ",\"assets\":{\"large_image\":" + quote(image_) + "}";
    return activity(body + "}") && waitReply(false);
  }

  void clear() override { activity("null"); }

  void close() noexcept override {
    if (pipe_ != INVALID_HANDLE_VALUE)
      CloseHandle(pipe_);
    if (event_)
      CloseHandle(event_);
    pipe_ = INVALID_HANDLE_VALUE;
    event_ = nullptr;
  }

private:
  bool transfer(void *data, DWORD length, bool writing) {
    OVERLAPPED operation{};
    operation.hEvent = event_;
    ResetEvent(event_);
    DWORD done = 0;
    const BOOL completed = writing
        ? WriteFile(pipe_, data, length, &done, &operation)
        : ReadFile(pipe_, data, length, &done, &operation);
    if (!completed) {
      if (GetLastError() != ERROR_IO_PENDING)
        return false;
      if (WaitForSingleObject(event_, 300) != WAIT_OBJECT_0) {
        CancelIoEx(pipe_, &operation);
        GetOverlappedResult(pipe_, &operation, &done, TRUE);
        return false;
      }
      if (!GetOverlappedResult(pipe_, &operation, &done, FALSE))
        return false;
    }
    return done == length;
  }

  bool send(std::uint32_t opcode, const std::string &json) {
    const auto length = static_cast<std::uint32_t>(json.size());
    std::vector<char> frame(8 + length);
    std::memcpy(frame.data(), &opcode, 4);
    std::memcpy(frame.data() + 4, &length, 4);
    std::memcpy(frame.data() + 8, json.data(), length);
    return transfer(frame.data(), static_cast<DWORD>(frame.size()), true);
  }

  bool activity(const std::string &payload) {
    nonce_ = std::to_string(++sequence_);
    return send(1, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":" + quote(nonce_) +
                       ",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
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
      Sleep(10);
    } while (Clock::now() < until);
    return false;
  }

  bool pump(bool *received, bool handshake) {
    std::array<std::uint32_t, 2> header{};
    DWORD available = 0, peeked = 0;
    if (!PeekNamedPipe(pipe_, header.data(), sizeof(header), &peeked,
                       &available, nullptr))
      return false;
    if (peeked < sizeof(header))
      return true;
    const auto length = header[1];
    if (length > 64 * 1024)
      return false;
    if (available < sizeof(header) + length)
      return true;
    std::string body(length, '\0');
    if (!transfer(header.data(), sizeof(header), false) ||
        (length && !transfer(body.data(), length, false)))
      return false;
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
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE event_ = nullptr;
};
} // namespace

bool desktopPresenceAvailable() noexcept { return true; }
std::unique_ptr<PresenceTransport> discordDesktop(std::string id,
                                                  std::string image) {
  return std::make_unique<DiscordPipe>(std::move(id), std::move(image));
}
} // namespace Kotonoha::Integrations
