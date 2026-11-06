#include "SchoolDaysDevBridge.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <json-c/json.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace SchoolDays {
namespace {
constexpr int DevProtocolVersion = 1;
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
Json object() { return Json(json_object_new_object(), json_object_put); }
void put(json_object *o, const char *key, const std::string &value) {
  json_object_object_add(o, key, json_object_new_string(value.c_str()));
}
void put(json_object *o, const char *key, int64_t value) {
  json_object_object_add(o, key, json_object_new_int64(value));
}
void put(json_object *o, const char *key, int value) {
  put(o, key, static_cast<int64_t>(value));
}
void put(json_object *o, const char *key, bool value) {
  json_object_object_add(o, key, json_object_new_boolean(value));
}
json_object *field(json_object *o, const char *key) {
  json_object *v = nullptr;
  if (o) json_object_object_get_ex(o, key, &v);
  return v;
}
int number(json_object *o, const char *key, int fallback = 0) {
  auto *v = field(o, key);
  return v && json_object_is_type(v, json_type_int) ? json_object_get_int(v) : fallback;
}
std::string string(json_object *o, const char *key) {
  auto *v = field(o, key);
  return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : "";
}
bool nonblocking(SOCKET s) {
  u_long mode = 1;
  return ioctlsocket(s, FIONBIO, &mode) == 0;
}
}

struct DevBridge::State {
  struct Client { SOCKET socket = INVALID_SOCKET; std::string input, output; };
  WSADATA wsa{};
  SOCKET listener = INVALID_SOCKET;
  std::vector<Client> clients;
  std::deque<std::string> log;
  uint64_t captureIndex = 0;
  struct FrameMetrics { std::string hash; double meanLuma = 0, nearBlackRatio = 0; };
  FrameMetrics screenFrame, narrativeFrame;
  std::filesystem::path captureDirectory;

  static FrameMetrics measure(Kotonoha::RendererRef renderer, SDL_Texture *target) {
    FrameMetrics result;
    SDL_Texture *previous = SDL_GetRenderTarget(&renderer);
    if (previous != target && !SDL_SetRenderTarget(&renderer, target)) return result;
    Kotonoha::SurfacePtr surface(SDL_RenderReadPixels(&renderer, nullptr));
    if (previous != target) (void)SDL_SetRenderTarget(&renderer, previous);
    if (!surface) return result;
    Kotonoha::SurfacePtr rgba(SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGBA32));
    if (!rgba) return result;
    uint64_t hash = UINT64_C(14695981039346656037);
    double luma = 0;
    uint64_t black = 0;
    for (int y = 0; y < rgba->h; ++y) {
      const auto *row = static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch;
      for (int x = 0; x < rgba->w; ++x) {
        const auto *pixel = row + x * 4;
        for (int channel = 0; channel < 4; ++channel) {
          hash ^= pixel[channel]; hash *= UINT64_C(1099511628211);
        }
        const double value = 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
        luma += value;
        if (value <= 12) ++black;
      }
    }
    const double count = static_cast<double>(rgba->w) * rgba->h;
    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << hash;
    result.hash = hex.str();
    result.meanLuma = luma / count;
    result.nearBlackRatio = black / count;
    return result;
  }

  explicit State(unsigned short port) {
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      throw std::runtime_error("DEV bridge WSAStartup failed");
    try {
      listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (listener == INVALID_SOCKET)
        throw std::runtime_error("DEV bridge socket failed");
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_port = htons(port);
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ||
          listen(listener, 4) || !nonblocking(listener))
        throw std::runtime_error("DEV bridge could not bind 127.0.0.1");
      captureDirectory = Kotonoha::resourceDirectory() / "dev-captures";
    } catch (...) {
      if (listener != INVALID_SOCKET) closesocket(listener);
      WSACleanup();
      throw;
    }
  }
  ~State() {
    for (auto &client : clients) closesocket(client.socket);
    if (listener != INVALID_SOCKET) closesocket(listener);
    WSACleanup();
  }

  Json status(const Runtime &r) {
    Json o = object();
    put(o.get(), "protocolVersion", DevProtocolVersion);
    put(o.get(), "pid", static_cast<int64_t>(GetCurrentProcessId()));
    put(o.get(), "frame", static_cast<int64_t>(r.devFrameIndex));
    put(o.get(), "update", static_cast<int64_t>(r.devUpdateIndex));
    Json action = object();
    put(action.get(), "actionId", static_cast<int64_t>(r.devLastActionId));
    put(action.get(), "frame", static_cast<int64_t>(r.devLastActionFrame));
    put(action.get(), "timestampNs", static_cast<int64_t>(r.devLastActionTimestampNs));
    put(action.get(), "scene", r.devLastActionScene);
    put(action.get(), "nodeIndex", static_cast<int64_t>(r.devLastActionNode));
    json_object_object_add(o.get(), "lastAction", json_object_get(action.get()));
    put(o.get(), "scene", r.scene.sceneKey);
    put(o.get(), "script", r.scene.orsPath);
    put(o.get(), "gameState", static_cast<int>(r.currentState));
    put(o.get(), "nodeIndex", static_cast<int64_t>(r.scene.nodeIndex));
    put(o.get(), "tick", static_cast<int64_t>(r.tick));
    json_object_object_add(o.get(), "timelineSeconds", json_object_new_double(r.elapsed));
    put(o.get(), "paused", r.devPaused);
    put(o.get(), "storyPaused", r.paused);
    json_object_object_add(o.get(), "speed", json_object_new_double(r.speed));
    put(o.get(), "background", r.background);
    const bool inGameplay = r.currentState == Kotonoha::GameState::Gameplay;
    put(o.get(), "narrativeDrawable", inGameplay && r.framePrimaryReady);
    put(o.get(), "backgroundDrawable", inGameplay && !r.background.empty() &&
                                       r.framePrimaryReady);
    put(o.get(), "choiceOpen", r.choice != nullptr);
    put(o.get(), "backlogOpen", r.backlogModel.IsOpen());
    put(o.get(), "saveLoadOpen", r.systemUi.IsSaveLoadOpen());
    put(o.get(), "routeMapOpen", r.systemUi.IsRouteMapOpen());
    put(o.get(), "replayOpen", r.replayPlayback && r.replayPlayback->IsOpen());
    put(o.get(), "playDataOpen", r.playDataPlayback && r.playDataPlayback->IsOpen());
    put(o.get(), "lastPresentedFrameHash", screenFrame.hash);
    put(o.get(), "currentNarrativeFrameHash", narrativeFrame.hash);
    put(o.get(), "frameHashAlgorithm", std::string("fnv1a64-rgba"));
    json_object_object_add(o.get(), "meanLuma", json_object_new_double(screenFrame.meanLuma));
    json_object_object_add(o.get(), "nearBlackRatio", json_object_new_double(screenFrame.nearBlackRatio));
    put(o.get(), "abcGroupCount", static_cast<int64_t>(r.abcGroups.size()));
    put(o.get(), "abcVoiceCount", static_cast<int64_t>(r.abcVoices.size()));
    put(o.get(), "backgroundStartTick", static_cast<int64_t>(r.backgroundStartTick));
    put(o.get(), "backgroundEndTick", static_cast<int64_t>(r.backgroundEndTick));
    put(o.get(), "abcAnalysisGeneration", static_cast<int64_t>(r.abcAnalysisGeneration));
    put(o.get(), "abcAnalysisQueued", static_cast<int64_t>(r.abcAnalysisQueued.size()));
    Json abcGroups = object();
    for (const auto &group : r.abcGroups) {
      Json entry = object();
      put(entry.get(), "complete", group.Complete());
      put(entry.get(), "collision", group.collision);
      json_object *paths = json_object_new_array();
      for (const auto &path : group.paths)
        json_object_array_add(paths, json_object_new_string(path.c_str()));
      json_object_object_add(entry.get(), "paths", paths);
      json_object_object_add(abcGroups.get(), group.key.c_str(), json_object_get(entry.get()));
    }
    json_object_object_add(o.get(), "abcGroups", json_object_get(abcGroups.get()));
    json_object *abcVoices = json_object_new_array();
    for (const auto &voice : r.abcVoices) {
      Json entry = object();
      put(entry.get(), "key", voice.key);
      put(entry.get(), "groupIndex", voice.groupIndex);
      put(entry.get(), "activityRecords", static_cast<int64_t>(voice.activity.size()));
      put(entry.get(), "frame", static_cast<int64_t>(voice.cursor.state.activeIndex));
      put(entry.get(), "counter", static_cast<int64_t>(voice.cursor.state.counter));
      put(entry.get(), "startTick", voice.event ? static_cast<int64_t>(voice.event->startTick) : -1);
      put(entry.get(), "endTick", voice.event ? static_cast<int64_t>(voice.event->endTick) : -1);
      json_object_array_add(abcVoices, json_object_get(entry.get()));
    }
    json_object_object_add(o.get(), "abcVoices", abcVoices);
    put(o.get(), "playingMediaCount", static_cast<int64_t>(r.playing.size()));
    json_object *media = json_object_new_array();
    for (const auto &item : r.playing) {
      Json entry = object();
      put(entry.get(), "movie", item.movie);
      put(entry.get(), "hasVideoFrame", item.player && item.player->hasVideoFrame());
      put(entry.get(), "open", item.player && item.player->isOpen());
      put(entry.get(), "startTick", item.event ? static_cast<int64_t>(item.event->startTick) : -1);
      put(entry.get(), "endTick", item.event ? static_cast<int64_t>(item.event->endTick) : -1);
      if (item.player)
        json_object_object_add(entry.get(), "playbackRate",
                               json_object_new_double(item.player->playbackRate()));
      json_object_array_add(media, json_object_get(entry.get()));
    }
    json_object_object_add(o.get(), "media", media);
    const auto menu = r.menuBarProjection();
    Json menuState = object();
    put(menuState.get(), "fullBarSuppressed", menu.fullBarSuppressed);
    put(menuState.get(), "actionInhibit", menu.actionInhibit);
    put(menuState.get(), "transportLocked", menu.transportLocked);
    json_object_object_add(o.get(), "menu", json_object_get(menuState.get()));
    json_object *choices = json_object_new_array();
    if (r.choice && r.choice->data.set_select) {
      const auto *data = r.choice->data.set_select;
      for (Uint64 index = 0; index < data->size; ++index)
        if (data->options[index] && *data->options[index])
          json_object_array_add(choices, json_object_new_string(data->options[index]));
    }
    json_object_object_add(o.get(), "choices", choices);
    if (r.engine) {
      int windowWidth = 0, windowHeight = 0;
      SDL_GetWindowSize(&r.engine->GetWindow(), &windowWidth, &windowHeight);
      Json window = object();
      put(window.get(), "width", windowWidth);
      put(window.get(), "height", windowHeight);
      json_object_object_add(o.get(), "window", json_object_get(window.get()));
      const auto &t = r.engine->GetVisualTransition();
      Json transition = object();
      put(transition.get(), "active", t.active());
      put(transition.get(), "generation", static_cast<int64_t>(t.generation()));
      put(transition.get(), "state", static_cast<int64_t>(t.state()));
      put(transition.get(), "outgoingValid", t.outgoingValid());
      put(transition.get(), "incomingDrawable", t.incomingDrawable());
      put(transition.get(), "framesHeld", static_cast<int64_t>(t.framesHeld()));
      put(transition.get(), "reason", t.reason());
      put(transition.get(), "outgoingScene", t.outgoingOwner());
      put(transition.get(), "incomingScene", t.incomingOwner());
      json_object_object_add(o.get(), "transition", json_object_get(transition.get()));
      const auto &settings = r.engine->GetSettings();
      Json display = object();
      put(display.get(), "mode", static_cast<int64_t>(settings.mode));
      put(display.get(), "display", static_cast<int64_t>(settings.display));
      json_object_object_add(display.get(), "renderScale", json_object_new_double(settings.renderScale));
      json_object_object_add(display.get(), "refreshRate", json_object_new_double(settings.refreshRate));
      put(display.get(), "width", settings.width);
      put(display.get(), "height", settings.height);
      json_object_object_add(o.get(), "display", json_object_get(display.get()));
    }
    return o;
  }

  Json capture(const Runtime &r, Kotonoha::RendererRef renderer) {
    const Uint64 capturedNs = SDL_GetTicksNS();
    Kotonoha::SurfacePtr pixels(SDL_RenderReadPixels(&renderer, nullptr));
    if (!pixels) throw std::runtime_error(SDL_GetError());
    std::filesystem::create_directories(captureDirectory);
    const auto path = captureDirectory /
        ("frame-" + std::to_string(++captureIndex) + ".bmp");
    if (!SDL_SaveBMP(pixels.get(), path.string().c_str()))
      throw std::runtime_error(SDL_GetError());
    Json result = object();
    put(result.get(), "path", path.string());
    put(result.get(), "width", pixels->w);
    put(result.get(), "height", pixels->h);
    put(result.get(), "protocolVersion", DevProtocolVersion);
    put(result.get(), "engineTimestampNs", static_cast<int64_t>(capturedNs));
    put(result.get(), "frame", static_cast<int64_t>(r.devFrameIndex));
    put(result.get(), "update", static_cast<int64_t>(r.devUpdateIndex));
    put(result.get(), "scene", r.scene.sceneKey);
    put(result.get(), "nodeIndex", static_cast<int64_t>(r.scene.nodeIndex));
    put(result.get(), "transitionGeneration", static_cast<int64_t>(r.engine->GetVisualTransition().generation()));
    put(result.get(), "frameHash", screenFrame.hash);
    json_object_object_add(result.get(), "meanLuma", json_object_new_double(screenFrame.meanLuma));
    json_object_object_add(result.get(), "nearBlackRatio", json_object_new_double(screenFrame.nearBlackRatio));
    return result;
  }

  Json handle(Runtime &r, Kotonoha::RendererRef renderer, const std::string &method,
              json_object *params) {
    if (method == "hello") {
      const int requested = number(params, "protocolVersion", -1);
      if (requested != DevProtocolVersion)
        throw std::runtime_error("DEV protocol version mismatch: expected 1");
      Json hello = object();
      put(hello.get(), "protocolVersion", DevProtocolVersion);
      put(hello.get(), "pid", static_cast<int64_t>(GetCurrentProcessId()));
      put(hello.get(), "server", "SchoolDaysDevBridge");
      return hello;
    }
    if (method == "status" || method == "get_window" || method == "get_scene" || method == "get_transition_state" ||
        method == "get_display_state" || method == "get_media_state" ||
        method == "get_system_ui_state" || method == "get_save_load_state" ||
        method == "get_replay_state" || method == "get_playdata_state" ||
        method == "get_background_state" || method == "get_abc_state" ||
        method == "get_choices" || method == "get_current_script" ||
        method == "get_current_node" || method == "get_timeline_time" ||
        method == "get_menu_state") {
      // Reading back the swapchain twice on every idle DEV frame stalls the
      // renderer and distorts the very transitions this bridge measures.
      // Snapshot only when an observer requests frame metadata.
      screenFrame = measure(renderer, nullptr);
      if (auto *target = r.engine->GetNovelTarget())
        narrativeFrame = measure(renderer, target);
      else narrativeFrame = {};
      return status(r);
    }
    if (method == "get_audio_state") {
      Json audio = object();
      json_object_object_add(audio.get(), "masterVolume",
          json_object_new_double(r.engine->GetAudio().getMasterVolume()));
      put(audio.get(), "masterMuted", r.engine->GetAudio().isMasterMuted());
      json_object *channels = json_object_new_array();
      double observedPeak = 0.0, observedEnergy = 0.0;
      uint64_t observedSamples = 0, clipped = 0, underrunCandidates = 0;
      for (const auto &channel : r.engine->GetAudio().getChannels()) {
        Json item = object();
        put(item.get(), "name", channel->getName());
        json_object_object_add(item.get(), "volume", json_object_new_double(channel->getVolume()));
        put(item.get(), "muted", channel->isMuted());
        put(item.get(), "activeStreams", static_cast<int64_t>(channel->activeStreams()));
        const auto metric = channel->metrics();
        json_object_object_add(item.get(), "peak", json_object_new_double(metric.peak));
        json_object_object_add(item.get(), "rms", json_object_new_double(metric.rms));
        put(item.get(), "samples", static_cast<int64_t>(metric.samples));
        put(item.get(), "clippedSamples", static_cast<int64_t>(metric.clippedSamples));
        put(item.get(), "underrunCandidates", static_cast<int64_t>(metric.underrunCandidates));
        put(item.get(), "sampleRate", metric.sampleRate);
        put(item.get(), "sampleChannels", metric.channels);
        observedPeak = std::max(observedPeak, metric.peak);
        observedEnergy += metric.rms * metric.rms * metric.samples;
        observedSamples += metric.samples;
        clipped += metric.clippedSamples;
        underrunCandidates += metric.underrunCandidates;
        json_object_array_add(channels, json_object_get(item.get()));
      }
      json_object_object_add(audio.get(), "channels", channels);
      put(audio.get(), "pcmMetricsAvailable", true);
      json_object_object_add(audio.get(), "masterObservedPeak", json_object_new_double(observedPeak));
      json_object_object_add(audio.get(), "masterObservedRms", json_object_new_double(
          observedSamples ? std::sqrt(observedEnergy / observedSamples) : 0.0));
      put(audio.get(), "masterClippedSamples", static_cast<int64_t>(clipped));
      put(audio.get(), "masterUnderrunCandidates", static_cast<int64_t>(underrunCandidates));
      put(audio.get(), "truePhysicalMixAvailable", false);
      put(audio.get(), "measurementScope", "SDL per-stream logical-device postmix; Windows combines devices later");
      put(audio.get(), "underrunDefinition", "silent postmix block while an audible stream is attached; includes intentional silence");
      return audio;
    }
    if (method == "capture_frame") {
      screenFrame = measure(renderer, nullptr);
      return capture(r, renderer);
    }
    if (method == "pause_runtime") r.devPaused = true;
    else if (method == "resume_runtime") { r.devPaused = false; r.devStepBudget = 0; }
    else if (method == "step_frame") { r.devPaused = true; ++r.devStepBudget; }
    else if (method == "click" || method == "mouse_move" ||
             method == "mouse_down" || method == "mouse_up") {
      const float x = static_cast<float>(number(params, "x"));
      const float y = static_cast<float>(number(params, "y"));
      const Uint32 actionId = static_cast<Uint32>(std::max(0, number(params, "actionId")));
      const Uint32 windowID = SDL_GetWindowID(&r.engine->GetWindow());
      auto push = [&](Uint32 type) {
        SDL_Event event{}; event.type = type;
        event.common.reserved = actionId;
        if (type == SDL_EVENT_MOUSE_MOTION) {
          event.motion.windowID = windowID; event.motion.x = x; event.motion.y = y;
        } else {
          event.button.windowID = windowID; event.button.button = SDL_BUTTON_LEFT;
          event.button.x = x; event.button.y = y;
        }
        if (!SDL_PushEvent(&event)) throw std::runtime_error(SDL_GetError());
      };
      if (method == "click") { push(SDL_EVENT_MOUSE_BUTTON_DOWN); push(SDL_EVENT_MOUSE_BUTTON_UP); }
      else if (method == "mouse_move") push(SDL_EVENT_MOUSE_MOTION);
      else push(method == "mouse_down" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP);
    } else if (method == "key_down" || method == "key_up") {
      const auto key = SDL_GetKeyFromName(string(params, "key").c_str());
      if (key == SDLK_UNKNOWN) throw std::invalid_argument("Unknown key");
      SDL_Event event{};
      event.type = method == "key_down" ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
      event.common.reserved = static_cast<Uint32>(std::max(0, number(params, "actionId")));
      event.key.windowID = SDL_GetWindowID(&r.engine->GetWindow());
      event.key.key = key;
      if (!SDL_PushEvent(&event)) throw std::runtime_error(SDL_GetError());
    } else if (method == "choose") {
      const int slot = number(params, "index", -1);
      if (!r.choice || slot < 0) throw std::invalid_argument("No selectable choice");
      r.choose(r.semanticChoiceForSlot(slot));
    } else if (method == "get_logs") {
      Json result = object();
      json_object *items = json_object_new_array();
      for (const auto &line : log) json_object_array_add(items, json_object_new_string(line.c_str()));
      json_object_object_add(result.get(), "events", items);
      json_object *engineLogs = json_object_new_array();
      for (const auto &entry : Kotonoha::Log::entries()) {
        Json item = object();
        put(item.get(), "milliseconds", static_cast<int64_t>(entry.milliseconds));
        put(item.get(), "category", entry.category);
        put(item.get(), "priority", static_cast<int>(entry.priority));
        put(item.get(), "message", entry.message);
        json_object_array_add(engineLogs, json_object_get(item.get()));
      }
      json_object_object_add(result.get(), "engineLogs", engineLogs);
      return result;
    } else if (method == "clear_logs") {
      log.clear();
      Kotonoha::Log::clear();
    }
    else throw std::invalid_argument("Unsupported DEV method");
    return status(r);
  }
};

DevBridge::DevBridge(unsigned short port) : state(std::make_unique<State>(port)) {}
DevBridge::~DevBridge() = default;

void DevBridge::trace(const std::string &event, const Runtime &runtime) {
  if (!state) return;
  const auto &transition = runtime.engine->GetVisualTransition();
  std::string line = std::to_string(SDL_GetTicks()) + "|" +
      std::to_string(runtime.devFrameIndex) + "|" +
      std::to_string(transition.generation()) + "|" + runtime.scene.sceneKey + "|" + event;
  state->log.push_back(std::move(line));
  if (state->log.size() > 4096) state->log.pop_front();
}

void DevBridge::poll(Runtime &runtime, Kotonoha::RendererRef renderer) {
  if (!state) return;
  for (;;) {
    SOCKET socket = accept(state->listener, nullptr, nullptr);
    if (socket == INVALID_SOCKET) break;
    if (state->clients.size() >= 4 || !nonblocking(socket)) closesocket(socket);
    else state->clients.push_back({socket, {}, {}});
  }
  for (auto &client : state->clients) {
    char bytes[4096];
    const int size = recv(client.socket, bytes, sizeof(bytes), 0);
    if (size > 0) client.input.append(bytes, size);
    else if (size == 0) { closesocket(client.socket); client.socket = INVALID_SOCKET; continue; }
    else if (WSAGetLastError() != WSAEWOULDBLOCK) {
      closesocket(client.socket); client.socket = INVALID_SOCKET; continue;
    }
    if (client.input.size() > 65536) { closesocket(client.socket); client.socket = INVALID_SOCKET; continue; }
    const size_t newline = client.input.find('\n');
    if (newline != std::string::npos) {
      std::string request = client.input.substr(0, newline);
      client.input.erase(0, newline + 1);
      Json response = object();
      try {
        Json root(json_tokener_parse(request.c_str()), json_object_put);
        if (!root || !json_object_is_type(root.get(), json_type_object))
          throw std::invalid_argument("Invalid JSON request");
        json_object *id = field(root.get(), "id");
        if (id) json_object_object_add(response.get(), "id", json_object_get(id));
        Json result = state->handle(runtime, renderer, string(root.get(), "method"),
                                    field(root.get(), "params"));
        put(response.get(), "ok", true);
        json_object_object_add(response.get(), "result", json_object_get(result.get()));
      } catch (const std::exception &error) {
        put(response.get(), "ok", false);
        put(response.get(), "error", std::string(error.what()));
      }
      client.output += json_object_to_json_string_ext(response.get(), JSON_C_TO_STRING_PLAIN);
      client.output.push_back('\n');
    }
    if (!client.output.empty()) {
      const int sent = send(client.socket, client.output.data(),
                            static_cast<int>(client.output.size()), 0);
      if (sent > 0) client.output.erase(0, sent);
      else if (sent < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(client.socket); client.socket = INVALID_SOCKET;
      }
    }
  }
  state->clients.erase(std::remove_if(state->clients.begin(), state->clients.end(),
      [](const State::Client &client) { return client.socket == INVALID_SOCKET; }),
      state->clients.end());
}
} // namespace SchoolDays
