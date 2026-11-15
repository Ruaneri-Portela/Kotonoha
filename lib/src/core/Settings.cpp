#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <Kotonoha/media/AudioMixer.hpp>

#include <cmath>
#include <fstream>
#include <json-c/json.h>
#include <limits>
#include <memory>
#include <stdexcept>

namespace Kotonoha {
namespace {
using JSON = std::unique_ptr<json_object, decltype(&json_object_put)>;

json_object *member(json_object *root, const char *name, json_type type) {
  json_object *value = nullptr;
  if (!json_object_object_get_ex(root, name, &value))
    return nullptr;
  if (!json_object_is_type(value, type) &&
      !(type == json_type_double && json_object_is_type(value, json_type_int)))
    throw std::runtime_error(std::string("Invalid settings field: ") + name);
  return value;
}

void read(json_object *root, const char *name, int &target) {
  if (auto *value = member(root, name, json_type_int)) {
    const auto number = json_object_get_int64(value);
    if (number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max())
      throw std::runtime_error(std::string("Settings integer overflow: ") +
                               name);
    target = static_cast<int>(number);
  }
}
void read(json_object *root, const char *name, float &target) {
  if (auto *value = member(root, name, json_type_double))
    target = static_cast<float>(json_object_get_double(value));
}
void read(json_object *root, const char *name, bool &target) {
  if (auto *value = member(root, name, json_type_boolean))
    target = json_object_get_boolean(value);
}
void read(json_object *root, const char *name, std::string &target) {
  if (auto *value = member(root, name, json_type_string))
    target.assign(json_object_get_string(value),
                  json_object_get_string_len(value));
}
} // namespace

void EngineSettings::validate() const {
  upscaling.validate();
  if (vnFontPath.find('\0') != std::string::npos)
    throw std::invalid_argument("Invalid VN font path");
  for (const auto &[name, volume] : channelVolumes) {
    AudioMixer::validateChannelName(name);
    if (!std::isfinite(volume) || volume < 0 || volume > 1)
      throw std::invalid_argument("Invalid mixer volume");
  }
  if (width < 320 || width > 16384 || height < 240 || height > 16384 ||
      display < 0 || mode < WindowMode::Windowed ||
      mode > WindowMode::Exclusive || fullscreenWidth < 0 ||
      fullscreenHeight < 0 || fullscreenWidth > 16384 ||
      fullscreenHeight > 16384 ||
      (fullscreenWidth == 0) != (fullscreenHeight == 0) ||
      !std::isfinite(refreshRate) || refreshRate < 0 || refreshRate > 1000 ||
      logPriority < SDL_LOG_PRIORITY_TRACE ||
      logPriority > SDL_LOG_PRIORITY_CRITICAL ||
      renderer.find('\0') != std::string::npos ||
      assetOverlayPath.find('\0') != std::string::npos ||
      translationPath.find('\0') != std::string::npos ||
      fontPath.find('\0') != std::string::npos ||
      !std::isfinite(masterVolume) || masterVolume < 0 || masterVolume > 1 ||
      !std::isfinite(debugUIScale) || debugUIScale < 1 || debugUIScale > 3 ||
      !std::isfinite(renderScale) || renderScale < 0.25f || renderScale > 1.0f)
    throw std::invalid_argument("Invalid engine/window settings");
}

EngineSettings EngineSettings::load(const std::string &path) {
  return load(path, EngineSettings{});
}

EngineSettings EngineSettings::load(const std::string &path,
                                    const EngineSettings &defaults) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open settings: " + path);
  std::string text(1024 * 1024 + 1, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  const auto size = input.gcount();
  if (input.bad() || size > 1024 * 1024)
    throw std::runtime_error("Cannot read settings or file exceeds 1 MiB: " +
                             path);
  text.resize(static_cast<std::size_t>(size));
  std::unique_ptr<json_tokener, decltype(&json_tokener_free)> parser(
      json_tokener_new(), json_tokener_free);
  if (!parser)
    throw std::bad_alloc();
  json_tokener_set_flags(parser.get(),
                         JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  JSON root(json_tokener_parse_ex(parser.get(), text.c_str(),
                                  static_cast<int>(text.size() + 1)),
            json_object_put);
  const auto end = json_tokener_get_parse_end(parser.get());
  if (json_tokener_get_error(parser.get()) != json_tokener_success || !root ||
      !json_object_is_type(root.get(), json_type_object) ||
      (end < text.size() &&
       text.find_first_not_of(" \t\r\n", end) != std::string::npos))
    throw std::runtime_error("Invalid settings JSON: " + path);
  EngineSettings result = defaults;
  int version = 1, mode = static_cast<int>(result.mode);
  int backend = static_cast<int>(result.upscaling.backend);
  int preset = static_cast<int>(result.upscaling.preset);
  int priority = result.logPriority;
  read(root.get(), "version", version);
  if (version != 1)
    throw std::runtime_error("Unsupported settings version: " + path);
  read(root.get(), "width", result.width);
  read(root.get(), "height", result.height);
  read(root.get(), "display", result.display);
  read(root.get(), "mode", mode);
  read(root.get(), "fullscreenWidth", result.fullscreenWidth);
  read(root.get(), "fullscreenHeight", result.fullscreenHeight);
  read(root.get(), "refreshRate", result.refreshRate);
  read(root.get(), "resizable", result.resizable);
  read(root.get(), "vsync", result.vsync);
  read(root.get(), "swapControllerAB", result.swapControllerAB);
  read(root.get(), "enableForcedSubtitles", result.enableForcedSubtitles);
  read(root.get(), "masterVolume", result.masterVolume);
  read(root.get(), "masterMuted", result.masterMuted);
  read(root.get(), "debugUIScale", result.debugUIScale);
  read(root.get(), "renderScale", result.renderScale);
  read(root.get(), "renderer", result.renderer);
  read(root.get(), "logPriority", priority);
  read(root.get(), "upscaler", backend);
  read(root.get(), "preset", preset);
  read(root.get(), "factor", result.upscaling.factor);
  read(root.get(), "shaderFiles", result.upscaling.shaderFiles);
  read(root.get(), "translationPath", result.translationPath);
  read(root.get(), "assetOverlayPath", result.assetOverlayPath);
  read(root.get(), "fontPath", result.fontPath);
  read(root.get(), "vnFontPath", result.vnFontPath);
  read(root.get(), "vnTextColorOverride", result.vnTextColorOverride);
  auto color = [&](const char *key, SDL_Color &value) {
    if (auto *array = member(root.get(), key, json_type_array)) {
      if (json_object_array_length(array) != 4)
        throw std::runtime_error("Invalid VN color");
      Uint8 *components[] = {&value.r, &value.g, &value.b, &value.a};
      for (int i = 0; i < 4; ++i) {
        auto *item = json_object_array_get_idx(array, i);
        if (!json_object_is_type(item, json_type_int) ||
            json_object_get_int64(item) < 0 ||
            json_object_get_int64(item) > 255)
          throw std::runtime_error("Invalid VN color component");
        *components[i] = Uint8(json_object_get_int(item));
      }
    }
  };
  color("vnTextColor", result.vnTextColor);
  color("vnSpeakerColor", result.vnSpeakerColor);
  if (auto *volumes = member(root.get(), "channelVolumes", json_type_object)) {
    json_object_object_foreach(volumes, name, value) {
      if (!json_object_is_type(value, json_type_double) &&
          !json_object_is_type(value, json_type_int))
        throw std::runtime_error("Invalid mixer volume");
      result.channelVolumes[name] = float(json_object_get_double(value));
    }
  }
  result.mode = static_cast<WindowMode>(mode);
  result.upscaling.backend = static_cast<UpscaleBackend>(backend);
  result.upscaling.preset = static_cast<UpscalePreset>(preset);
  if (priority < SDL_LOG_PRIORITY_TRACE || priority > SDL_LOG_PRIORITY_CRITICAL)
    throw std::runtime_error("Invalid log priority: " + path);
  result.logPriority = static_cast<SDL_LogPriority>(priority);
  result.validate();
  return result;
}

std::string EngineSettings::defaultPath(const std::string &identify) {
  return (preferredDocumentsDirectory(identify) / "settings.json").string();
}

void EngineSettings::save(const std::string &path) const {
  validate();
  JSON root(json_object_new_object(), json_object_put);
  if (!root)
    throw std::bad_alloc();
  const auto integer = [&](const char *name, int value) {
    json_object_object_add(root.get(), name, json_object_new_int(value));
  };
  const auto boolean = [&](const char *name, bool value) {
    json_object_object_add(root.get(), name, json_object_new_boolean(value));
  };
  integer("version", 1);
  integer("width", width);
  integer("height", height);
  integer("display", display);
  integer("mode", static_cast<int>(mode));
  integer("fullscreenWidth", fullscreenWidth);
  integer("fullscreenHeight", fullscreenHeight);
  json_object_object_add(root.get(), "refreshRate",
                         json_object_new_double(refreshRate));
  boolean("resizable", resizable);
  boolean("vsync", vsync);
  boolean("swapControllerAB", swapControllerAB);
  boolean("enableForcedSubtitles", enableForcedSubtitles);
  json_object_object_add(root.get(), "masterVolume",
                         json_object_new_double(masterVolume));
  boolean("masterMuted", masterMuted);
  json_object_object_add(root.get(), "debugUIScale",
                         json_object_new_double(debugUIScale));
  json_object_object_add(root.get(), "renderScale",
                         json_object_new_double(renderScale));
  json_object_object_add(root.get(), "renderer",
                         json_object_new_string(renderer.c_str()));
  integer("logPriority", static_cast<int>(logPriority));
  integer("upscaler", static_cast<int>(upscaling.backend));
  integer("preset", static_cast<int>(upscaling.preset));
  json_object_object_add(root.get(), "factor",
                         json_object_new_double(upscaling.factor));
  auto shaderReferences = UpscaleShaderFiles(upscaling.shaderFiles);
  for (auto &file : shaderReferences)
    file = FileResolver::referenceName(file);
  json_object_object_add(
      root.get(), "shaderFiles",
      json_object_new_string(UpscaleShaderFileList(shaderReferences).c_str()));
  json_object_object_add(
      root.get(), "assetOverlayPath",
      json_object_new_string(
          FileResolver::referenceName(assetOverlayPath).c_str()));
  json_object_object_add(
      root.get(), "translationPath",
      json_object_new_string(
          FileResolver::referenceName(translationPath).c_str()));
  json_object_object_add(
      root.get(), "fontPath",
      json_object_new_string(FileResolver::referenceName(fontPath).c_str()));
  json_object_object_add(
      root.get(), "vnFontPath",
      json_object_new_string(FileResolver::referenceName(vnFontPath).c_str()));
  json_object_object_add(root.get(), "vnTextColorOverride",
                         json_object_new_boolean(vnTextColorOverride));
  auto color = [&](const char *key, SDL_Color value) {
    auto *array = json_object_new_array();
    for (int component : {value.r, value.g, value.b, value.a})
      json_object_array_add(array, json_object_new_int(component));
    json_object_object_add(root.get(), key, array);
  };
  color("vnTextColor", vnTextColor);
  color("vnSpeakerColor", vnSpeakerColor);
  auto *volumes = json_object_new_object();
  for (const auto &[name, volume] : channelVolumes)
    json_object_object_add(volumes, name.c_str(),
                           json_object_new_double(volume));
  json_object_object_add(root.get(), "channelVolumes", volumes);
  writeTextFileAtomically(path, json_object_to_json_string_ext(
                                    root.get(), JSON_C_TO_STRING_PRETTY));
}
} // namespace Kotonoha
