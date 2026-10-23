#include <Kotonoha/Init.hpp>
#include <Kotonoha/debug/LogPanel.hpp>
#include <Kotonoha/debug/McpServer.hpp>
#include <Kotonoha/utils/Log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#define KMCP_CLOSE closesocket
#define KMCP_POLL WSAPoll
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#define KMCP_CLOSE close
#define KMCP_POLL poll
#endif

namespace Kotonoha::Debug::Mcp {
namespace {

// ---------------------------------------------------------------- JSON ----

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Json> array;
  std::vector<std::pair<std::string, Json>> object;

  Json() = default;
  Json(bool v) : type(Type::Bool), boolean(v) {}
  Json(int v) : type(Type::Number), number(v) {}
  Json(long long v) : type(Type::Number), number(static_cast<double>(v)) {}
  Json(Uint64 v) : type(Type::Number), number(static_cast<double>(v)) {}
  Json(double v) : type(Type::Number), number(v) {}
  Json(const char *v) : type(Type::String), string(v != nullptr ? v : "") {}
  Json(std::string v) : type(Type::String), string(std::move(v)) {}

  static Json Array() {
    Json j;
    j.type = Type::Array;
    return j;
  }
  static Json Object() {
    Json j;
    j.type = Type::Object;
    return j;
  }

  Json &Set(const std::string &key, Json value) {
    for (auto &entry : object) {
      if (entry.first == key) {
        entry.second = std::move(value);
        return *this;
      }
    }
    object.emplace_back(key, std::move(value));
    return *this;
  }
  Json &Push(Json value) {
    array.push_back(std::move(value));
    return *this;
  }

  const Json *Get(const std::string &key) const {
    if (type != Type::Object) return nullptr;
    for (const auto &entry : object) {
      if (entry.first == key) return &entry.second;
    }
    return nullptr;
  }
  std::string Str(const std::string &key, const std::string &fallback = "") const {
    const Json *v = Get(key);
    return v != nullptr && v->type == Type::String ? v->string : fallback;
  }
  double Num(const std::string &key, double fallback = 0.0) const {
    const Json *v = Get(key);
    return v != nullptr && v->type == Type::Number ? v->number : fallback;
  }
  bool Has(const std::string &key) const { return Get(key) != nullptr; }
};

void AppendUtf8(std::string &out, unsigned cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class JsonParser {
public:
  explicit JsonParser(const std::string &text) : text_(text) {}

  bool Parse(Json &out) {
    if (!Value(out, 0)) return false;
    SkipSpace();
    return pos_ == text_.size();
  }

private:
  const std::string &text_;
  size_t pos_ = 0;

  void SkipSpace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
            text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Literal(const char *word) {
    const size_t n = std::strlen(word);
    if (text_.compare(pos_, n, word) != 0) return false;
    pos_ += n;
    return true;
  }

  bool Hex4(unsigned &out) {
    if (pos_ + 4 > text_.size()) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      out <<= 4;
      if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  bool String(std::string &out) {
    if (pos_ >= text_.size() || text_[pos_] != '"') return false;
    ++pos_;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) return false;
      const char e = text_[pos_++];
      switch (e) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        unsigned cp = 0;
        if (!Hex4(cp)) return false;
        if (cp >= 0xD800 && cp < 0xDC00 && pos_ + 1 < text_.size() &&
            text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
          pos_ += 2;
          unsigned low = 0;
          if (!Hex4(low)) return false;
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        AppendUtf8(out, cp);
        break;
      }
      default: return false;
      }
    }
    return false;
  }

  bool Value(Json &out, int depth) {
    if (depth > 32) return false;
    SkipSpace();
    if (pos_ >= text_.size()) return false;
    const char c = text_[pos_];
    if (c == '{') {
      ++pos_;
      out = Json::Object();
      SkipSpace();
      if (pos_ < text_.size() && text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      while (true) {
        SkipSpace();
        std::string key;
        if (!String(key)) return false;
        SkipSpace();
        if (pos_ >= text_.size() || text_[pos_++] != ':') return false;
        Json value;
        if (!Value(value, depth + 1)) return false;
        out.object.emplace_back(std::move(key), std::move(value));
        SkipSpace();
        if (pos_ >= text_.size()) return false;
        const char d = text_[pos_++];
        if (d == '}') return true;
        if (d != ',') return false;
      }
    }
    if (c == '[') {
      ++pos_;
      out = Json::Array();
      SkipSpace();
      if (pos_ < text_.size() && text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      while (true) {
        Json value;
        if (!Value(value, depth + 1)) return false;
        out.array.push_back(std::move(value));
        SkipSpace();
        if (pos_ >= text_.size()) return false;
        const char d = text_[pos_++];
        if (d == ']') return true;
        if (d != ',') return false;
      }
    }
    if (c == '"') {
      out.type = Json::Type::String;
      return String(out.string);
    }
    if (c == 't') {
      out = Json(true);
      return Literal("true");
    }
    if (c == 'f') {
      out = Json(false);
      return Literal("false");
    }
    if (c == 'n') {
      out = Json();
      return Literal("null");
    }
    char *end = nullptr;
    const double number = std::strtod(text_.c_str() + pos_, &end);
    if (end == text_.c_str() + pos_) return false;
    pos_ = static_cast<size_t>(end - text_.c_str());
    out = Json(number);
    return true;
  }
};

void DumpString(const std::string &in, std::string &out) {
  out += '"';
  for (const unsigned char c : in) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
        out += buffer;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  out += '"';
}

void Dump(const Json &j, std::string &out) {
  switch (j.type) {
  case Json::Type::Null: out += "null"; break;
  case Json::Type::Bool: out += j.boolean ? "true" : "false"; break;
  case Json::Type::Number: {
    char buffer[40];
    if (j.number == static_cast<double>(static_cast<long long>(j.number))) {
      std::snprintf(buffer, sizeof(buffer), "%lld",
                    static_cast<long long>(j.number));
    } else {
      std::snprintf(buffer, sizeof(buffer), "%.6g", j.number);
    }
    out += buffer;
    break;
  }
  case Json::Type::String: DumpString(j.string, out); break;
  case Json::Type::Array: {
    out += '[';
    for (size_t i = 0; i < j.array.size(); ++i) {
      if (i != 0) out += ',';
      Dump(j.array[i], out);
    }
    out += ']';
    break;
  }
  case Json::Type::Object: {
    out += '{';
    bool first = true;
    for (const auto &entry : j.object) {
      if (!first) out += ',';
      first = false;
      DumpString(entry.first, out);
      out += ':';
      Dump(entry.second, out);
    }
    out += '}';
    break;
  }
  }
}

std::string Serialize(const Json &j) {
  std::string out;
  Dump(j, out);
  return out;
}

std::string Base64(const unsigned char *data, size_t size) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((size + 2) / 3 * 4);
  for (size_t i = 0; i < size; i += 3) {
    const unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                       (i + 1 < size ? static_cast<unsigned>(data[i + 1]) << 8 : 0) |
                       (i + 2 < size ? static_cast<unsigned>(data[i + 2]) : 0);
    out += alphabet[(v >> 18) & 63];
    out += alphabet[(v >> 12) & 63];
    out += i + 1 < size ? alphabet[(v >> 6) & 63] : '=';
    out += i + 2 < size ? alphabet[v & 63] : '=';
  }
  return out;
}

// ---------------------------------------------------------------- tools ---

Json ToolText(const std::string &text, bool isError = false) {
  Json block = Json::Object();
  block.Set("type", "text").Set("text", text);
  Json result = Json::Object();
  result.Set("content", Json::Array().Push(std::move(block)))
      .Set("isError", isError);
  return result;
}

Json ToolJson(const Json &value) { return ToolText(Serialize(value)); }

struct ToolContext {
  ::Kotonoha::Kotonoha *game;
  ::Kotonoha::Gameplay *running;
};

::Kotonoha::Gameplay *CurrentGameplay(const ToolContext &ctx) {
  if (ctx.running != nullptr) return ctx.running;
  if (ctx.game == nullptr) return nullptr;
  const int scene = global.config.scene.load();
  if (scene < 0 || static_cast<size_t>(scene) >= ctx.game->gameplays.size()) {
    return nullptr;
  }
  return ctx.game->gameplays[static_cast<size_t>(scene)];
}

Json ToolGetState(const ToolContext &ctx, const Json &) {
  Json state = Json::Object();
  int w = 0, h = 0;
  if (global.window != nullptr) SDL_GetWindowSize(global.window, &w, &h);
  state.Set("window", Json::Object().Set("width", w).Set("height", h));
  state.Set("scene", global.config.scene.load());
  state.Set("scenes", ctx.game != nullptr
                          ? static_cast<int>(ctx.game->gameplays.size())
                          : 0);
  state.Set("paused", global.config.paused.load());
  state.Set("splashActive", ctx.game != nullptr && ctx.game->IsSplashActive());
  state.Set("debugUi", global.config.enableDebugUi.load());

  if (::Kotonoha::Gameplay *play = CurrentGameplay(ctx)) {
    Json g = Json::Object();
    g.Set("script", play->scriptPath);
    g.Set("timeSeconds", play->tm != nullptr ? static_cast<double>(play->GetTime()) : 0.0);
    g.Set("userPaused", play->UserPaused(&global));
    g.Set("autoMode", play->SchoolDaysAutoMode());
    g.Set("promptId", play->promptId);
    g.Set("promptShown", play->putPrompt);
    g.Set("mediaActive", play->IsMediaActive());
    g.Set("loadingOverlay", play->ShouldShowLoadingOverlay());
    state.Set("gameplay", std::move(g));
  }
  return ToolJson(state);
}

Json BufferInfoJson(const std::vector<::Kotonoha::MediaBufferInfo> &infos) {
  Json list = Json::Array();
  for (const auto &info : infos) {
    Json item = Json::Object();
    item.Set("id", info.id).Set("type", info.type).Set("channel", info.channel);
    item.Set("path", info.path);
    item.Set("totalSeconds", static_cast<double>(info.totalMs) / 1000.0);
    item.Set("playedSeconds", static_cast<double>(info.playedMs) / 1000.0);
    item.Set("cachedSeconds", static_cast<double>(info.cachedMs) / 1000.0);
    item.Set("eof", info.eof);
    list.Push(std::move(item));
  }
  return list;
}

Json ToolGetBuffers(const ToolContext &ctx, const Json &) {
  ::Kotonoha::Gameplay *play = CurrentGameplay(ctx);
  if (play == nullptr) return ToolText("no active gameplay", true);
  Json result = Json::Object();
  if (play->audio != nullptr)
    result.Set("audio", BufferInfoJson(play->audio->GetBufferInfo()));
  if (play->video != nullptr)
    result.Set("video", BufferInfoJson(play->video->GetBufferInfo()));
  if (play->image != nullptr)
    result.Set("image", BufferInfoJson(play->image->GetBufferInfo()));
  return ToolJson(result);
}

Json ToolGetLogs(const ToolContext &, const Json &args) {
  const size_t count = static_cast<size_t>(SDL_clamp(args.Num("count", 100), 1, 2000));
  const std::string level = args.Str("min_level", "info");
  SDL_LogPriority priority = SDL_LOG_PRIORITY_INFO;
  if (level == "debug") priority = SDL_LOG_PRIORITY_DEBUG;
  else if (level == "warn") priority = SDL_LOG_PRIORITY_WARN;
  else if (level == "error") priority = SDL_LOG_PRIORITY_ERROR;
  std::string text;
  for (const std::string &line : RecentLogLines(count, priority)) {
    text += line;
    text += '\n';
  }
  return ToolText(text.empty() ? "(no log lines)" : text);
}

Json ToolScreenshot(const ToolContext &, const Json &args) {
  if (global.render == nullptr) return ToolText("no renderer", true);
  if (SDL_GetRenderTarget(global.render) != nullptr) {
    return ToolText("render target is not the backbuffer", true);
  }
  SDL_Surface *surface = SDL_RenderReadPixels(global.render, nullptr);
  if (surface == nullptr) {
    return ToolText(std::string("capture failed: ") + SDL_GetError(), true);
  }
  const int maxWidth = static_cast<int>(args.Num("max_width", 1280));
  if (maxWidth > 0 && surface->w > maxWidth) {
    const int height = std::max(1, surface->h * maxWidth / surface->w);
    if (SDL_Surface *scaled =
            SDL_ScaleSurface(surface, maxWidth, height, SDL_SCALEMODE_LINEAR)) {
      SDL_DestroySurface(surface);
      surface = scaled;
    }
  }

  SDL_IOStream *io = SDL_IOFromDynamicMem();
  std::vector<unsigned char> png;
  bool ok = io != nullptr && SDL_SavePNG_IO(surface, io, false);
  if (ok) {
    const Sint64 size = SDL_GetIOSize(io);
    ok = size > 0 && SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) == 0;
    if (ok) {
      png.resize(static_cast<size_t>(size));
      ok = SDL_ReadIO(io, png.data(), png.size()) == png.size();
    }
  }
  const std::string error = SDL_GetError();
  if (io != nullptr) SDL_CloseIO(io);
  const int w = surface->w, h = surface->h;
  SDL_DestroySurface(surface);
  if (!ok) return ToolText("PNG encoding failed: " + error, true);

  Json image = Json::Object();
  image.Set("type", "image")
      .Set("data", Base64(png.data(), png.size()))
      .Set("mimeType", "image/png");
  Json info = Json::Object();
  info.Set("type", "text").Set("text", "screenshot " + std::to_string(w) + "x" + std::to_string(h));
  Json result = Json::Object();
  result.Set("content", Json::Array().Push(std::move(image)).Push(std::move(info)))
      .Set("isError", false);
  return result;
}

void PushKey(SDL_Scancode scancode, bool down) {
  SDL_Event event;
  SDL_zero(event);
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.timestamp = SDL_GetTicksNS();
  event.key.windowID = global.window != nullptr ? SDL_GetWindowID(global.window) : 0;
  event.key.scancode = scancode;
  event.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
  event.key.down = down;
  SDL_PushEvent(&event);
}

Json ToolKey(const ToolContext &, const Json &args) {
  const std::string name = args.Str("key");
  SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
  if (scancode == SDL_SCANCODE_UNKNOWN) {
    const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
    if (key != SDLK_UNKNOWN) scancode = SDL_GetScancodeFromKey(key, nullptr);
  }
  if (scancode == SDL_SCANCODE_UNKNOWN) {
    return ToolText("unknown key '" + name + "' (use SDL key names, e.g. Space, Return, Escape, Left, N)", true);
  }
  const std::string action = args.Str("action", "press");
  if (action == "press" || action == "down") PushKey(scancode, true);
  if (action == "press" || action == "up") PushKey(scancode, false);
  if (action != "press" && action != "down" && action != "up") {
    return ToolText("action must be press, down or up", true);
  }
  return ToolText("ok");
}

Json ToolMouse(const ToolContext &, const Json &args) {
  const float x = static_cast<float>(args.Num("x"));
  const float y = static_cast<float>(args.Num("y"));
  const std::string action = args.Str("action", "click");
  const std::string buttonName = args.Str("button", "left");
  Uint8 button = SDL_BUTTON_LEFT;
  if (buttonName == "right") button = SDL_BUTTON_RIGHT;
  else if (buttonName == "middle") button = SDL_BUTTON_MIDDLE;
  else if (buttonName != "left") return ToolText("button must be left, right or middle", true);
  if (action != "move" && action != "click" && action != "down" && action != "up") {
    return ToolText("action must be move, click, down or up", true);
  }

  const SDL_WindowID window = global.window != nullptr ? SDL_GetWindowID(global.window) : 0;
  SDL_Event motion;
  SDL_zero(motion);
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.timestamp = SDL_GetTicksNS();
  motion.motion.windowID = window;
  motion.motion.x = x;
  motion.motion.y = y;
  SDL_PushEvent(&motion);

  auto pushButton = [&](bool down) {
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.timestamp = SDL_GetTicksNS();
    event.button.windowID = window;
    event.button.button = button;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
  };
  if (action == "click" || action == "down") pushButton(true);
  if (action == "click" || action == "up") pushButton(false);
  return ToolText("ok");
}

Json ToolPlayback(const ToolContext &ctx, const Json &args) {
  ::Kotonoha::Gameplay *play = CurrentGameplay(ctx);
  if (play == nullptr) return ToolText("no active gameplay", true);
  const std::string action = args.Str("action");
  const Uint64 ms = static_cast<Uint64>(std::max(0.0, args.Num("ms", 5000)));
  if (action == "pause") play->SetUserPaused(&global, true);
  else if (action == "resume") play->SetUserPaused(&global, false);
  else if (action == "toggle") play->ToggleUserPaused(&global);
  else if (action == "seek_forward") play->SeekForward(ms);
  else if (action == "seek_backward") play->SeekBackward(ms);
  else if (action == "skip_to_end") return ToolText(play->SkipSchoolDaysToEnd() ? "ok" : "nothing to skip");
  else if (action == "restart_scene") play->RestartCurrentScene();
  else if (action == "set_speed") {
    const int index = static_cast<int>(args.Num("index", 0));
    if (!play->SetSchoolDaysPlaybackSpeedIndex(index))
      return ToolText("speed index must be 0..4 (x1,x2,x4,x12,x24)", true);
  }
  else if (action == "auto_on") play->SetSchoolDaysAutoMode(true);
  else if (action == "auto_off") play->SetSchoolDaysAutoMode(false);
  else {
    return ToolText("action must be pause, resume, toggle, seek_forward, seek_backward, skip_to_end, restart_scene, set_speed, auto_on or auto_off", true);
  }
  return ToolText("ok");
}

Json ToolPrompt(const ToolContext &ctx, const Json &args) {
  ::Kotonoha::Gameplay *play = CurrentGameplay(ctx);
  if (play == nullptr || play->prompt == nullptr) {
    return ToolJson(Json::Object().Set("pending", false));
  }
  ::Kotonoha::Prompt *prompt = play->prompt;
  const std::string action = args.Str("action", "status");
  if (action == "choose") {
    const int index = static_cast<int>(args.Num("index", -1));
    if (!prompt->SelectChoice(index, play->audio)) {
      return ToolText("choice rejected (prompt not accepting input or bad index)", true);
    }
    return ToolText("ok");
  }
  if (action != "status") return ToolText("action must be status or choose", true);
  Json choices = Json::Array();
  for (const std::string &choice : prompt->Choices()) choices.Push(choice);
  Json status = Json::Object();
  status.Set("pending", play->promptId == -2 && play->putPrompt)
      .Set("acceptingInput", prompt->IsActive())
      .Set("promptId", play->promptId)
      .Set("choices", std::move(choices));
  return ToolJson(status);
}

Json ToolScene(const ToolContext &ctx, const Json &args) {
  const std::string action = args.Str("action");
  const int count = ctx.game != nullptr ? static_cast<int>(ctx.game->gameplays.size()) : 0;
  if (action == "next") global.config.next.store(true);
  else if (action == "prev") global.config.back.store(true);
  else if (action == "set") {
    const int index = static_cast<int>(args.Num("index", -1));
    if (index < 0 || index >= count) {
      return ToolText("index out of range (0.." + std::to_string(count - 1) + ")", true);
    }
    global.config.scene.store(index);
  } else {
    return ToolText("action must be next, prev or set", true);
  }
  return ToolText("ok");
}

Json ToolLoadScript(const ToolContext &ctx, const Json &args) {
  const std::string path = args.Str("path");
  if (ctx.game == nullptr || path.empty()) return ToolText("path is required", true);
  return ctx.game->LoadScriptFile(path.c_str()) ? ToolText("loaded")
                                                  : ToolText("failed to load script", true);
}

Json ToolQuit(const ToolContext &, const Json &) {
  SDL_Event event;
  SDL_zero(event);
  event.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&event);
  return ToolText("quitting");
}

struct ToolDef {
  const char *name;
  const char *description;
  const char *schema;
  Json (*handler)(const ToolContext &, const Json &);
};

std::mutex g_extraMutex;
std::vector<ToolRegistration> g_extraTools;

Json RunExtraTool(const ToolContext &ctx, const std::string &name, const Json &args) {
  ToolRegistration tool;
  {
    std::lock_guard<std::mutex> lock(g_extraMutex);
    for (const auto &entry : g_extraTools) {
      if (entry.name == name) tool = entry;
    }
  }
  if (tool.handler == nullptr) return ToolText("unknown tool", true);
  bool isError = false;
  const std::string text =
      tool.handler(ctx.game, ctx.running, Serialize(args), &isError, tool.userdata);
  return ToolText(text, isError);
}

const ToolDef kTools[] = {
    {"get_state", "Engine state: window, scene index/count, pause, current script, playback time, prompt.",
     R"J({"type":"object","properties":{}})J", ToolGetState},
    {"screenshot", "PNG of the frame about to be presented (includes the debug UI).",
     R"J({"type":"object","properties":{"max_width":{"type":"integer","description":"Downscale to this width (default 1280, 0 = full size)"}}})J",
     ToolScreenshot},
    {"get_buffers", "Scene media buffers in seconds: played, cached ahead and EOF for audio/video, plus images.",
     R"J({"type":"object","properties":{}})J", ToolGetBuffers},
    {"get_logs", "Most recent engine log lines.",
     R"J({"type":"object","properties":{"count":{"type":"integer","description":"Lines (default 100, max 2000)"},"min_level":{"type":"string","enum":["debug","info","warn","error"]}}})J",
     ToolGetLogs},
    {"key", "Inject a keyboard key through the normal SDL event path.",
     R"J({"type":"object","properties":{"key":{"type":"string","description":"SDL key name, e.g. Space, Return, Escape, Left, N"},"action":{"type":"string","enum":["press","down","up"]}},"required":["key"]})J",
     ToolKey},
    {"mouse", "Inject mouse motion/clicks at window coordinates.",
     R"J({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"action":{"type":"string","enum":["move","click","down","up"]},"button":{"type":"string","enum":["left","right","middle"]}},"required":["x","y"]})J",
     ToolMouse},
    {"playback", "Control the current scene: pause/resume/toggle, seek, skip_to_end, restart_scene, auto mode.",
     R"J({"type":"object","properties":{"action":{"type":"string","enum":["pause","resume","toggle","seek_forward","seek_backward","skip_to_end","restart_scene","set_speed","auto_on","auto_off"]},"index":{"type":"integer","description":"set_speed: 0..4 = x1,x2,x4,x12,x24"},"ms":{"type":"integer","description":"Seek amount in milliseconds (default 5000)"}},"required":["action"]})J",
     ToolPlayback},
    {"prompt", "Inspect or answer the pending A/B choice (status | choose index).",
     R"J({"type":"object","properties":{"action":{"type":"string","enum":["status","choose"]},"index":{"type":"integer"}}})J",
     ToolPrompt},
    {"scene", "Change the active scene (next, prev, or set by index).",
     R"J({"type":"object","properties":{"action":{"type":"string","enum":["next","prev","set"]},"index":{"type":"integer"}},"required":["action"]})J",
     ToolScene},
    {"load_script", "Load a script file as a new scene.",
     R"J({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})J",
     ToolLoadScript},
    {"quit", "Ask the application to quit.",
     R"J({"type":"object","properties":{}})J", ToolQuit},
};

Json ToolList() {
  Json tools = Json::Array();
  for (const ToolDef &def : kTools) {
    Json schema;
    const std::string text = def.schema;
    JsonParser(text).Parse(schema);
    Json tool = Json::Object();
    tool.Set("name", def.name)
        .Set("description", def.description)
        .Set("inputSchema", std::move(schema));
    tools.Push(std::move(tool));
  }
  {
    std::lock_guard<std::mutex> lock(g_extraMutex);
    for (const auto &extra : g_extraTools) {
      Json schema;
      if (!JsonParser(extra.schemaJson).Parse(schema)) {
        schema = Json::Object().Set("type", "object");
      }
      Json tool = Json::Object();
      tool.Set("name", extra.name)
          .Set("description", extra.description)
          .Set("inputSchema", std::move(schema));
      tools.Push(std::move(tool));
    }
  }
  return Json::Object().Set("tools", std::move(tools));
}

// ------------------------------------------------------------- dispatch ---

struct Job {
  std::string tool;
  Json args;
  Json result;
  bool done = false;
  bool abandoned = false;
};

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::shared_ptr<Job>> g_queue;

SDL_Thread *g_thread = nullptr;
std::atomic_bool g_stop{false};
std::atomic_bool g_running{false};
SocketHandle g_listen = kInvalidSocket;

Json RunToolOnMainThread(const std::string &tool, const Json &args) {
  auto job = std::make_shared<Job>();
  job->tool = tool;
  job->args = args;
  std::unique_lock<std::mutex> lock(g_mutex);
  g_queue.push_back(job);
  if (!g_cv.wait_for(lock, std::chrono::seconds(15), [&] { return job->done; })) {
    job->abandoned = true;
    return ToolText("timed out waiting for the main thread", true);
  }
  return std::move(job->result);
}

Json Rpc(const Json &id, const Json *result, int errorCode = 0,
         const char *errorMessage = nullptr) {
  Json response = Json::Object();
  response.Set("jsonrpc", "2.0").Set("id", id);
  if (result != nullptr) {
    response.Set("result", *result);
  } else {
    response.Set("error", Json::Object().Set("code", errorCode).Set("message", errorMessage));
  }
  return response;
}

// Returns false when the message is a notification (no response body).
bool HandleMessage(const Json &message, Json &response) {
  const Json *idPtr = message.Get("id");
  const bool isNotification = idPtr == nullptr;
  const Json id = idPtr != nullptr ? *idPtr : Json();
  const std::string method = message.Str("method");
  const Json empty = Json::Object();
  const Json *paramsPtr = message.Get("params");
  const Json &params = paramsPtr != nullptr ? *paramsPtr : empty;

  if (isNotification) return false;

  if (method == "initialize") {
    Json result = Json::Object();
    const std::string requested = params.Str("protocolVersion", "2025-06-18");
    result.Set("protocolVersion", requested)
        .Set("capabilities", Json::Object().Set("tools", Json::Object()))
        .Set("serverInfo", Json::Object().Set("name", "kotonoha").Set("version", "0.1"))
        .Set("instructions",
             "Controls the running Kotonoha engine. Use get_state first, "
             "screenshot to see the frame, get_buffers for media timings in "
             "seconds, key/mouse to inject input.");
    response = Rpc(id, &result);
  } else if (method == "ping") {
    const Json result = Json::Object();
    response = Rpc(id, &result);
  } else if (method == "tools/list") {
    const Json result = ToolList();
    response = Rpc(id, &result);
  } else if (method == "tools/call") {
    const std::string name = params.Str("name");
    bool known = false;
    for (const ToolDef &def : kTools) known = known || name == def.name;
    {
      std::lock_guard<std::mutex> lock(g_extraMutex);
      for (const auto &extra : g_extraTools) known = known || name == extra.name;
    }
    if (!known) {
      response = Rpc(id, nullptr, -32602, "unknown tool");
    } else {
      const Json *args = params.Get("arguments");
      const Json result = RunToolOnMainThread(name, args != nullptr ? *args : empty);
      response = Rpc(id, &result);
    }
  } else {
    response = Rpc(id, nullptr, -32601, "method not found");
  }
  return true;
}

// ----------------------------------------------------------------- HTTP ---

bool SendAll(SocketHandle socket, const std::string &data) {
  size_t sent = 0;
  while (sent < data.size()) {
    const auto n = ::send(socket, data.data() + sent,
                          static_cast<int>(data.size() - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

void SendResponse(SocketHandle socket, int status, const char *statusText,
                  const std::string &body = "") {
  std::string head = "HTTP/1.1 " + std::to_string(status) + " " + statusText +
                     "\r\nContent-Type: application/json\r\nContent-Length: " +
                     std::to_string(body.size()) + "\r\nConnection: close\r\n";
  if (status == 405) head += "Allow: POST\r\n";
  head += "\r\n";
  SendAll(socket, head + body);
}

bool WaitReadable(SocketHandle socket, int timeoutMs) {
  pollfd pfd{};
  pfd.fd = socket;
  pfd.events = POLLIN;
  return KMCP_POLL(&pfd, 1, timeoutMs) > 0;
}

std::string Lower(std::string s) {
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool OriginAllowed(const std::string &origin) {
  if (origin.empty()) return true;
  const std::string o = Lower(origin);
  for (const char *host : {"localhost", "127.0.0.1", "[::1]"}) {
    const std::string prefixHttp = std::string("http://") + host;
    const std::string prefixHttps = std::string("https://") + host;
    for (const std::string &prefix : {prefixHttp, prefixHttps}) {
      if (o.compare(0, prefix.size(), prefix) == 0 &&
          (o.size() == prefix.size() || o[prefix.size()] == ':')) {
        return true;
      }
    }
  }
  return false;
}

void HandleConnection(SocketHandle socket) {
  constexpr size_t kMaxBody = 8u * 1024u * 1024u;
  std::string data;
  size_t headerEnd = std::string::npos;
  char buffer[4096];
  while (headerEnd == std::string::npos) {
    if (data.size() > 64 * 1024 || !WaitReadable(socket, 5000)) return;
    const auto n = ::recv(socket, buffer, sizeof(buffer), 0);
    if (n <= 0) return;
    data.append(buffer, static_cast<size_t>(n));
    headerEnd = data.find("\r\n\r\n");
  }

  const std::string head = data.substr(0, headerEnd);
  const std::string lowered = Lower(head);
  const std::string requestLine = head.substr(0, head.find("\r\n"));
  auto header = [&](const char *name) {
    const std::string key = std::string("\r\n") + name + ":";
    const size_t at = lowered.find(key);
    if (at == std::string::npos) return std::string();
    const size_t start = at + key.size();
    const size_t end = head.find("\r\n", start);
    std::string value = head.substr(start, end == std::string::npos ? end : end - start);
    const size_t first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string() : value.substr(first);
  };

  if (!OriginAllowed(header("origin"))) {
    SendResponse(socket, 403, "Forbidden");
    return;
  }
  if (requestLine.compare(0, 5, "POST ") != 0) {
    SendResponse(socket, 405, "Method Not Allowed");
    return;
  }

  const unsigned long long length = std::strtoull(header("content-length").c_str(), nullptr, 10);
  if (length > kMaxBody) {
    SendResponse(socket, 413, "Payload Too Large");
    return;
  }
  std::string body = data.substr(headerEnd + 4);
  while (body.size() < length) {
    if (!WaitReadable(socket, 5000)) return;
    const auto n = ::recv(socket, buffer, sizeof(buffer), 0);
    if (n <= 0) return;
    body.append(buffer, static_cast<size_t>(n));
  }
  body.resize(static_cast<size_t>(length));

  Json request;
  if (!JsonParser(body).Parse(request)) {
    SendResponse(socket, 400, "Bad Request",
                 Serialize(Rpc(Json(), nullptr, -32700, "parse error")));
    return;
  }

  if (request.type == Json::Type::Array) {
    Json responses = Json::Array();
    for (const Json &message : request.array) {
      Json response;
      if (HandleMessage(message, response)) responses.Push(std::move(response));
    }
    if (responses.array.empty()) SendResponse(socket, 202, "Accepted");
    else SendResponse(socket, 200, "OK", Serialize(responses));
    return;
  }
  Json response;
  if (!HandleMessage(request, response)) {
    SendResponse(socket, 202, "Accepted");
    return;
  }
  SendResponse(socket, 200, "OK", Serialize(response));
}

int SDLCALL ServerThread(void *) {
  while (!g_stop.load()) {
    if (!WaitReadable(g_listen, 200)) continue;
    const SocketHandle client = ::accept(g_listen, nullptr, nullptr);
    if (client == kInvalidSocket) continue;
    HandleConnection(client);
    KMCP_CLOSE(client);
  }
  return 0;
}

} // namespace

void RegisterTool(ToolRegistration tool) {
  std::lock_guard<std::mutex> lock(g_extraMutex);
  for (auto &entry : g_extraTools) {
    if (entry.name == tool.name) {
      entry = std::move(tool);
      return;
    }
  }
  g_extraTools.push_back(std::move(tool));
}

int ParseArguments(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr || std::strcmp(argv[i], "--mcp") != 0) continue;
    argv[i][0] = '!';
    int port = kDefaultPort;
    if (i + 1 < argc && argv[i + 1] != nullptr && argv[i + 1][0] >= '0' &&
        argv[i + 1][0] <= '9') {
      port = std::atoi(argv[i + 1]);
      argv[i + 1][0] = '!';
    }
    return port > 0 && port < 65536 ? port : 0;
  }
  if (const char *env = std::getenv("KOTONOHA_MCP_PORT")) {
    const int port = std::atoi(env);
    return port > 0 && port < 65536 ? port : 0;
  }
  return 0;
}

bool Start(int port) {
  if (g_running.load()) return true;
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
  g_listen = ::socket(AF_INET, SOCK_STREAM, 0);
  if (g_listen == kInvalidSocket) {
    KLOG_ERROR(CORE, "MCP: socket creation failed");
    return false;
  }
  const int yes = 1;
  setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char *>(&yes), sizeof(yes));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (::bind(g_listen, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(g_listen, 8) != 0) {
    KLOG_ERROR(CORE, "MCP: cannot listen on 127.0.0.1:%d", port);
    KMCP_CLOSE(g_listen);
    g_listen = kInvalidSocket;
    return false;
  }
  g_stop = false;
  g_thread = SDL_CreateThread(ServerThread, "kotonoha-mcp", nullptr);
  if (g_thread == nullptr) {
    KMCP_CLOSE(g_listen);
    g_listen = kInvalidSocket;
    return false;
  }
  g_running = true;
  KLOG_INFO(CORE, "MCP server listening on http://127.0.0.1:%d/mcp", port);
  return true;
}

void Stop() {
  if (!g_running.exchange(false)) return;
  g_stop = true;
  {
    // Release any network thread still waiting for a tool result.
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &job : g_queue) {
      job->result = ToolText("server stopped", true);
      job->done = true;
    }
    g_queue.clear();
  }
  g_cv.notify_all();
  SDL_WaitThread(g_thread, nullptr);
  g_thread = nullptr;
  KMCP_CLOSE(g_listen);
  g_listen = kInvalidSocket;
}

bool IsRunning() { return g_running.load(); }

void Service(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *running) {
  if (!g_running.load(std::memory_order_relaxed)) return;
  const ToolContext context{game, running};
  while (true) {
    std::shared_ptr<Job> job;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_queue.empty()) return;
      job = g_queue.front();
      g_queue.pop_front();
    }
    Json result;
    if (!job->abandoned) {
      bool handled = false;
      for (const ToolDef &def : kTools) {
        if (job->tool == def.name) {
          result = def.handler(context, job->args);
          handled = true;
          break;
        }
      }
      if (!handled) result = RunExtraTool(context, job->tool, job->args);
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      job->result = std::move(result);
      job->done = true;
    }
    g_cv.notify_all();
  }
}

} // namespace Kotonoha::Debug::Mcp
