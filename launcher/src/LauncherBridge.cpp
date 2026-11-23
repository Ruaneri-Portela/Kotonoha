// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <KotonohaLauncher/LauncherBridge.h>
#include <KotonohaLauncher/UnifiedLauncher.hpp>

#include <Kotonoha/core/Settings.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
#include "SchoolDaysLauncher.hpp"
#include <SchoolDays/SchoolDaysAssets.hpp>
#endif

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace {

namespace fs = std::filesystem;

char *dupCString(const std::string &str) {
  auto *out = static_cast<char *>(std::malloc(str.size() + 1));
  if (!out)
    return nullptr;
  std::memcpy(out, str.data(), str.size());
  out[str.size()] = '\0';
  return out;
}

std::string safeString(const char *ptr) { return ptr ? std::string(ptr) : ""; }

void appendJsonString(std::ostringstream &out, std::string_view value) {
  out << '"';
  for (unsigned char ch : value) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (ch < 0x20) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
        out << buf;
      } else {
        out << static_cast<char>(ch);
      }
      break;
    }
  }
  out << '"';
}

// Minimal JSON reader for the partial EngineSettings objects sent by native
// UIs. Produces flattened keys ("upscaling.factor") so the merge code stays
// explicit and no new third-party dependency is needed in the launcher.
class FlatJsonReader {
public:
  explicit FlatJsonReader(std::string_view input) : input_(input) {}

  bool parse() {
    skipSpace();
    if (!consume('{'))
      return false;
    skipSpace();
    if (consume('}'))
      return true;
    for (;;) {
      skipSpace();
      std::string key;
      if (!readString(key))
        return false;
      skipSpace();
      if (!consume(':'))
        return false;
      skipSpace();
      if (!readValue(key))
        return false;
      skipSpace();
      if (consume('}'))
        return true;
      if (!consume(','))
        return false;
    }
  }

  const std::map<std::string, std::string> &values() const { return values_; }

private:
  void skipSpace() {
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t' ||
            input_[pos_] == '\r' || input_[pos_] == '\n'))
      ++pos_;
  }
  bool consume(char ch) {
    skipSpace();
    if (pos_ < input_.size() && input_[pos_] == ch) {
      ++pos_;
      return true;
    }
    return false;
  }
  static void appendUtf8(std::string &out, unsigned code) {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }
  bool readHex4(unsigned &code) {
    if (pos_ + 4 > input_.size())
      return false;
    code = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = input_[pos_++];
      code <<= 4;
      if (ch >= '0' && ch <= '9')
        code |= static_cast<unsigned>(ch - '0');
      else if (ch >= 'a' && ch <= 'f')
        code |= static_cast<unsigned>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F')
        code |= static_cast<unsigned>(ch - 'A' + 10);
      else
        return false;
    }
    return true;
  }
  bool readString(std::string &out) {
    if (!consume('"'))
      return false;
    out.clear();
    while (pos_ < input_.size()) {
      const char ch = input_[pos_++];
      if (ch == '"')
        return true;
      if (ch != '\\') {
        out += ch;
        continue;
      }
      if (pos_ >= input_.size())
        return false;
      const char escaped = input_[pos_++];
      switch (escaped) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        unsigned code = 0;
        if (!readHex4(code))
          return false;
        appendUtf8(out, code);
        break;
      }
      default:
        return false;
      }
    }
    return false;
  }
  bool skipComposite() {
    const char open = input_[pos_++];
    const char close = open == '{' ? '}' : ']';
    int depth = 1;
    bool inString = false;
    while (pos_ < input_.size() && depth > 0) {
      const char ch = input_[pos_++];
      if (inString) {
        if (ch == '\\' && pos_ < input_.size())
          ++pos_;
        else if (ch == '"')
          inString = false;
        continue;
      }
      if (ch == '"')
        inString = true;
      else if (ch == open || ch == '{' || ch == '[')
        ++depth;
      else if (ch == close || ch == '}' || ch == ']')
        --depth;
    }
    return depth == 0;
  }
  bool readValue(const std::string &key) {
    if (pos_ >= input_.size())
      return false;
    if (input_[pos_] == '{') {
      ++pos_;
      if (consume('}'))
        return true;
      for (;;) {
        skipSpace();
        std::string child;
        if (!readString(child))
          return false;
        if (!consume(':'))
          return false;
        if (!readValue(key + "." + child))
          return false;
        if (consume('}'))
          return true;
        if (!consume(','))
          return false;
      }
    }
    if (input_[pos_] == '[')
      return skipComposite();
    if (input_[pos_] == '"') {
      std::string text;
      if (!readString(text))
        return false;
      values_[key] = text;
      return true;
    }
    const std::size_t start = pos_;
    while (pos_ < input_.size() && input_[pos_] != ',' &&
           input_[pos_] != '}' && input_[pos_] != ']' && input_[pos_] != ' ' &&
           input_[pos_] != '\t' && input_[pos_] != '\r' && input_[pos_] != '\n')
      ++pos_;
    if (start == pos_)
      return false;
    values_[key] = std::string(input_.substr(start, pos_ - start));
    return true;
  }

  std::string_view input_;
  std::size_t pos_ = 0;
  std::map<std::string, std::string> values_;
};

bool isDiscImagePath(const std::string &path) {
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext == ".iso" || ext == ".bin" || ext == ".img";
}

void appendParamsObject(std::ostringstream &out,
                        const KotonohaLauncher::GameLaunchParameters &params) {
  out << '{';
  out << "\"assetOverlayPath\":";
  appendJsonString(out, params.assetOverlayPath);
  out << ",\"translationPath\":";
  appendJsonString(out, params.translationPath);
  out << ",\"subtitleStylePath\":";
  appendJsonString(out, params.subtitleStylePath);
  out << ",\"customDocumentsDir\":";
  appendJsonString(out, params.customDocumentsDir);
  out << ",\"ktrfPath\":";
  appendJsonString(out, params.ktrfPath);
  out << ",\"scriptsDir\":";
  appendJsonString(out, params.scriptsDir);
  out << ",\"startScenario\":";
  appendJsonString(out, params.startScenario);
  out << ",\"rendererBackend\":";
  appendJsonString(out, params.rendererBackend);
  out << ",\"upscalerBackend\":";
  appendJsonString(out, params.upscalerBackend);
  out << ",\"upscalerPreset\":";
  appendJsonString(out, params.upscalerPreset);
  out << ",\"upscalerFactor\":";
  appendJsonString(out, params.upscalerFactor);
  out << '}';
}

void appendEntryObject(std::ostringstream &out,
                       const KotonohaLauncher::DiscoveredGameEntry &entry,
                       bool splitSavesByGame,
                       const std::string &customSaveRoot) {
  const auto &registry = KotonohaLauncher::EngineLibraryRegistry::instance();
  const bool linked = registry.canInstantiateInProcess(entry.engine.kind);
  const std::string saveDir = KotonohaLauncher::resolveGameSaveDirectory(
      entry, splitSavesByGame, customSaveRoot);
  const bool isDisc = isDiscImagePath(entry.path);
  const bool isSchoolDays =
      entry.engine.kind == Kotonoha::Filesystem::EngineKind::SchoolDays;

  out << '{';
  out << "\"title\":";
  appendJsonString(out, entry.title);
  out << ",\"saveProfileId\":";
  appendJsonString(out, entry.saveProfileId);
  out << ",\"path\":";
  appendJsonString(out, entry.path);
  out << ",\"overlayPath\":";
  appendJsonString(out, entry.overlayPath);
  out << ",\"engineSlug\":";
  appendJsonString(out, entry.engine.slug);
  out << ",\"engineDisplayName\":";
  appendJsonString(out, entry.engine.displayName);
  out << ",\"evidence\":";
  appendJsonString(out, entry.engine.evidence);
  out << ",\"libraryTarget\":";
  appendJsonString(out, entry.libraryTarget);
  out << ",\"targetBinary\":";
  appendJsonString(out, entry.targetBinary);
  out << ",\"packageId\":";
  appendJsonString(out, entry.packageId);
  out << ",\"saveDirectory\":";
  appendJsonString(out, saveDir);
  out << ",\"isDiscImage\":" << (isDisc ? "true" : "false");
  out << ",\"linkedInProcess\":" << (linked ? "true" : "false");
  out << ",\"isSchoolDays\":" << (isSchoolDays ? "true" : "false");
  out << ",\"params\":";
  appendParamsObject(out, entry.params);
  out << '}';
}

} // namespace

extern "C" {

char *KotonohaLauncherScanRootsJson(const char *const *roots, int rootCount,
                                    int splitSavesByGame,
                                    const char *customSaveRoot) {
  try {
    std::vector<std::string> rootList;
    if (roots && rootCount > 0) {
      rootList.reserve(static_cast<std::size_t>(rootCount));
      for (int i = 0; i < rootCount; ++i) {
        if (roots[i] && roots[i][0] != '\0')
          rootList.emplace_back(roots[i]);
      }
    }
    const auto entries = KotonohaLauncher::scanGameLibraryRoots(rootList);
    const std::string saveRootStr = safeString(customSaveRoot);
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (i > 0)
        out << ',';
      appendEntryObject(out, entries[i], splitSavesByGame != 0, saveRootStr);
    }
    out << ']';
    return dupCString(out.str());
  } catch (...) {
    return dupCString("[]");
  }
}

char *KotonohaLauncherDetectPathJson(const char *path, int splitSavesByGame,
                                     const char *customSaveRoot) {
  try {
    const std::string target = safeString(path);
    if (target.empty())
      return dupCString("{}");
    const auto detected =
        KotonohaLauncher::detectKotonohaEngineFromPath(target);
    if (detected.kind == Kotonoha::Filesystem::EngineKind::Unknown)
      return dupCString("{}");
    auto entry = KotonohaLauncher::buildDiscoveredGameEntry(target, detected);
    entry.params =
        KotonohaLauncher::loadGameParamsForProfile(entry.saveProfileId);
    std::ostringstream out;
    appendEntryObject(out, entry, splitSavesByGame != 0,
                      safeString(customSaveRoot));
    return dupCString(out.str());
  } catch (...) {
    return dupCString("{}");
  }
}

char *KotonohaLauncherLoadGameParamsJson(const char *saveProfileId) {
  try {
    const auto params =
        KotonohaLauncher::loadGameParamsForProfile(safeString(saveProfileId));
    std::ostringstream out;
    appendParamsObject(out, params);
    return dupCString(out.str());
  } catch (...) {
    return dupCString("{}");
  }
}

int KotonohaLauncherSaveGameParams(
    const char *saveProfileId, const char *assetOverlayPath,
    const char *translationPath, const char *subtitleStylePath,
    const char *customDocumentsDir, const char *ktrfPath,
    const char *scriptsDir, const char *startScenario,
    const char *rendererBackend, const char *upscalerBackend,
    const char *upscalerPreset, const char *upscalerFactor) {
  try {
    KotonohaLauncher::GameLaunchParameters params;
    params.assetOverlayPath = safeString(assetOverlayPath);
    params.translationPath = safeString(translationPath);
    params.subtitleStylePath = safeString(subtitleStylePath);
    params.customDocumentsDir = safeString(customDocumentsDir);
    params.ktrfPath = safeString(ktrfPath);
    params.scriptsDir = safeString(scriptsDir);
    params.startScenario = safeString(startScenario);
    params.rendererBackend = safeString(rendererBackend);
    params.upscalerBackend = safeString(upscalerBackend);
    params.upscalerPreset = safeString(upscalerPreset);
    params.upscalerFactor = safeString(upscalerFactor);
    return KotonohaLauncher::saveGameParamsForProfile(safeString(saveProfileId),
                                                      params)
               ? 1
               : 0;
  } catch (...) {
    return 0;
  }
}

namespace {

bool readIntValue(const std::string &text, int &out) {
  try {
    std::size_t used = 0;
    out = std::stoi(text, &used);
    return used == text.size();
  } catch (...) {
    return false;
  }
}

bool readFloatValue(const std::string &text, float &out) {
  try {
    std::size_t used = 0;
    out = std::stof(text, &used);
    return used == text.size();
  } catch (...) {
    return false;
  }
}

bool readBoolValue(const std::string &text, bool &out) {
  if (text == "true" || text == "1") {
    out = true;
    return true;
  }
  if (text == "false" || text == "0") {
    out = false;
    return true;
  }
  return false;
}

char *statusJson(bool ok, const std::string &error = {}) {
  std::ostringstream out;
  out << "{\"ok\":" << (ok ? "true" : "false");
  if (!ok) {
    out << ",\"error\":";
    appendJsonString(out, error);
  }
  out << '}';
  return dupCString(out.str());
}

} // namespace

char *KotonohaLauncherSaveProfileEngineSettingsJson(const char *profileId,
                                                     const char *partialJson) {
  try {
    const std::string json = safeString(partialJson);
    FlatJsonReader reader(json);
    if (!reader.parse())
      return statusJson(false, "Malformed JSON object");
    const auto &fields = reader.values();

    const std::string id = safeString(profileId);
    if (id.empty() || id.find('/') != std::string::npos ||
        id.find('\\') != std::string::npos || id.find("..") != std::string::npos)
      return statusJson(false, "Invalid profile id");

    const std::string baseSettings = Kotonoha::EngineSettings::defaultPath();
    if (baseSettings.empty())
      return statusJson(false, "Engine settings path unavailable");
    const fs::path baseDir = fs::path(baseSettings).parent_path();
    const fs::path profileDir = baseDir / "Kotonoha-Profiles" / id;
    const fs::path target = profileDir / "settings.json";

    Kotonoha::EngineSettings defaults;
    if (fs::exists(baseSettings)) {
      try {
        defaults = Kotonoha::EngineSettings::load(baseSettings);
      } catch (...) {
      }
    }
    Kotonoha::EngineSettings settings = defaults;
    if (fs::exists(target)) {
      try {
        settings = Kotonoha::EngineSettings::load(target.string(), defaults);
      } catch (...) {
      }
    }

    const auto integer = [&](const char *name, int &slot) {
      auto it = fields.find(name);
      return it != fields.end() && readIntValue(it->second, slot);
    };
    const auto number = [&](const char *name, float &slot) {
      auto it = fields.find(name);
      return it != fields.end() && readFloatValue(it->second, slot);
    };
    const auto boolean = [&](const char *name, bool &slot) {
      auto it = fields.find(name);
      return it != fields.end() && readBoolValue(it->second, slot);
    };
    const auto text = [&](const char *name, std::string &slot) {
      auto it = fields.find(name);
      if (it == fields.end())
        return false;
      slot = it->second;
      return true;
    };

    integer("width", settings.width);
    integer("height", settings.height);
    integer("display", settings.display);
    integer("fullscreenWidth", settings.fullscreenWidth);
    integer("fullscreenHeight", settings.fullscreenHeight);
    number("refreshRate", settings.refreshRate);
    boolean("resizable", settings.resizable);
    boolean("vsync", settings.vsync);
    boolean("swapControllerAB", settings.swapControllerAB);
    number("masterVolume", settings.masterVolume);
    boolean("masterMuted", settings.masterMuted);
    number("vnTextSpeed", settings.vnTextSpeed);
    boolean("vnTextInstant", settings.vnTextInstant);
    boolean("vnTextColorOverride", settings.vnTextColorOverride);
    number("debugUIScale", settings.debugUIScale);
    number("renderScale", settings.renderScale);
    text("renderer", settings.renderer);
    text("translationPath", settings.translationPath);
    text("fontPath", settings.fontPath);
    text("vnFontPath", settings.vnFontPath);
    text("customCursorPath", settings.customCursorPath);
    text("assetOverlayName", settings.assetOverlayName);

    auto colorField = [&](const char *prefix, SDL_Color &slot) {
      const std::string key = std::string(prefix);
      auto it = fields.find(key);
      if (it != fields.end()) {
        int r = 0, g = 0, b = 0, a = 255;
        if (std::sscanf(it->second.c_str(), "%d,%d,%d,%d", &r, &g, &b, &a) < 3)
          return;
        slot = SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g),
                         static_cast<Uint8>(b), static_cast<Uint8>(a)};
        return;
      }
      int dummy = 0;
      auto channel = [&](const char *name, Uint8 &target) {
        auto found = fields.find(key + "." + name);
        if (found != fields.end() && readIntValue(found->second, dummy))
          target = static_cast<Uint8>(dummy);
      };
      channel("r", slot.r);
      channel("g", slot.g);
      channel("b", slot.b);
      channel("a", slot.a);
    };
    colorField("vnTextColor", settings.vnTextColor);
    colorField("vnSpeakerColor", settings.vnSpeakerColor);

    text("upscaling.shaderFiles", settings.upscaling.shaderFiles);
    number("upscaling.factor", settings.upscaling.factor);
    auto backend = fields.find("upscaling.backend");
    if (backend != fields.end()) {
      const std::string &name = backend->second;
      int value = 0;
      if (readIntValue(name, value)) {
        settings.upscaling.backend =
            static_cast<Kotonoha::UpscaleBackend>(value);
      } else if (name == "None") {
        settings.upscaling.backend = Kotonoha::UpscaleBackend::None;
      } else if (name == "Linear") {
        settings.upscaling.backend = Kotonoha::UpscaleBackend::Linear;
      } else if (name == "MPV") {
        settings.upscaling.backend = Kotonoha::UpscaleBackend::MPV;
      } else if (name == "MetalFX") {
        settings.upscaling.backend = Kotonoha::UpscaleBackend::MetalFX;
      } else if (name == "Auto") {
        settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
      }
    }
    auto preset = fields.find("upscaling.preset");
    if (preset != fields.end()) {
      int value = 0;
      if (readIntValue(preset->second, value)) {
        settings.upscaling.preset = static_cast<Kotonoha::UpscalePreset>(value);
      } else {
        for (const auto &candidate : Kotonoha::UpscalePresets()) {
          if (preset->second == Kotonoha::UpscalePresetName(candidate)) {
            settings.upscaling.preset = candidate;
            break;
          }
        }
      }
    }

    settings.validate();
    fs::create_directories(profileDir);
    settings.save(target.string());
    return statusJson(true);
  } catch (const std::exception &e) {
    return statusJson(false, e.what());
  } catch (...) {
    return statusJson(false, "Unknown error saving engine settings");
  }
}

char *KotonohaLauncherUpscalePresetsJson(void) {
  try {
    const auto &presets = Kotonoha::UpscalePresets();
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < presets.size(); ++i) {
      if (i > 0)
        out << ',';
      out << "{\"index\":" << i << ",\"name\":";
      appendJsonString(out, Kotonoha::UpscalePresetName(presets[i]));
      out << '}';
    }
    out << ']';
    return dupCString(out.str());
  } catch (...) {
    return dupCString("[]");
  }
}

int KotonohaLauncherValidateRoute(const char *ktrfPath, char *infoBuf,
                                  int infoBufSize) {
  auto writeInfo = [&](const std::string &msg) {
    if (infoBuf && infoBufSize > 0) {
      std::snprintf(infoBuf, static_cast<std::size_t>(infoBufSize), "%s",
                    msg.c_str());
    }
  };
  try {
    const std::string path = safeString(ktrfPath);
    if (path.empty()) {
      writeInfo("No route file selected (bundled default will be used).");
      return 1;
    }
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
    std::string info;
    const bool ok = SchoolDays::Launcher::validateRoute(path, info);
    writeInfo(info);
    return ok ? 1 : 0;
#else
    std::error_code ec;
    if (fs::is_regular_file(fs::path(path), ec)) {
      writeInfo("Route file found.");
      return 1;
    }
    writeInfo("Route file not found.");
    return 0;
#endif
  } catch (const std::exception &e) {
    writeInfo(e.what());
    return 0;
  } catch (...) {
    writeInfo("Unknown error validating route file.");
    return 0;
  }
}

char *KotonohaLauncherDefaultSchoolDaysRoute(void) {
  try {
#if defined(KOTONOHA_LAUNCH_SCHOOL_DAYS)
    return dupCString(
        Kotonoha::Providers::SchoolDaysAssets::defaultRoute().string());
#else
    return dupCString("");
#endif
  } catch (...) {
    return dupCString("");
  }
}

char *KotonohaLauncherResolveSaveDirectory(const char *gamePath,
                                           const char *engineSlug,
                                           const char *saveProfileId,
                                           int splitSavesByGame,
                                           const char *customSaveRoot) {
  try {
    KotonohaLauncher::DiscoveredGameEntry entry;
    entry.path = safeString(gamePath);
    entry.title = fs::path(entry.path).filename().string();
    entry.saveProfileId =
        saveProfileId && saveProfileId[0] != '\0'
            ? std::string(saveProfileId)
            : KotonohaLauncher::sanitizeGameProfileId(entry.path);
    if (engineSlug && engineSlug[0] != '\0') {
      if (const auto *desc =
              KotonohaLauncher::EngineLibraryRegistry::instance().findBySlug(
                  engineSlug)) {
        entry.packageId = desc->packageId;
      }
    }
    return dupCString(KotonohaLauncher::resolveGameSaveDirectory(
        entry, splitSavesByGame != 0, safeString(customSaveRoot)));
  } catch (...) {
    return dupCString("");
  }
}

void KotonohaLauncherFreeString(char *ptr) { std::free(ptr); }

} // extern "C"

#if defined(__ANDROID__)
namespace {
std::string jstringToStd(JNIEnv *env, jstring jstr) {
  if (!jstr)
    return {};
  const char *chars = env->GetStringUTFChars(jstr, nullptr);
  if (!chars)
    return {};
  std::string out(chars);
  env->ReleaseStringUTFChars(jstr, chars);
  return out;
}
} // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeScanRootsJson(
    JNIEnv *env, jclass, jobjectArray roots, jboolean splitSavesByGame,
    jstring customSaveRoot) {
  std::vector<std::string> storage;
  std::vector<const char *> ptrs;
  if (roots) {
    const jsize count = env->GetArrayLength(roots);
    storage.reserve(static_cast<std::size_t>(count));
    ptrs.reserve(static_cast<std::size_t>(count));
    for (jsize i = 0; i < count; ++i) {
      auto item = static_cast<jstring>(env->GetObjectArrayElement(roots, i));
      storage.push_back(jstringToStd(env, item));
      env->DeleteLocalRef(item);
    }
    for (const auto &s : storage)
      ptrs.push_back(s.c_str());
  }
  const std::string saveRoot = jstringToStd(env, customSaveRoot);
  char *json = KotonohaLauncherScanRootsJson(
      ptrs.empty() ? nullptr : ptrs.data(), static_cast<int>(ptrs.size()),
      splitSavesByGame ? 1 : 0, saveRoot.c_str());
  jstring result = env->NewStringUTF(json ? json : "[]");
  KotonohaLauncherFreeString(json);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeLoadGameParamsJson(
    JNIEnv *env, jclass, jstring saveProfileId) {
  const std::string profile = jstringToStd(env, saveProfileId);
  char *json = KotonohaLauncherLoadGameParamsJson(profile.c_str());
  jstring result = env->NewStringUTF(json ? json : "{}");
  KotonohaLauncherFreeString(json);
  return result;
}

JNIEXPORT jboolean JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeSaveGameParams(
    JNIEnv *env, jclass, jstring saveProfileId, jstring assetOverlayPath,
    jstring translationPath, jstring subtitleStylePath,
    jstring customDocumentsDir, jstring ktrfPath, jstring scriptsDir,
    jstring startScenario, jstring rendererBackend, jstring upscalerBackend,
    jstring upscalerPreset, jstring upscalerFactor) {
  const std::string profile = jstringToStd(env, saveProfileId);
  const std::string overlay = jstringToStd(env, assetOverlayPath);
  const std::string translation = jstringToStd(env, translationPath);
  const std::string subtitleStyle = jstringToStd(env, subtitleStylePath);
  const std::string customDocs = jstringToStd(env, customDocumentsDir);
  const std::string ktrf = jstringToStd(env, ktrfPath);
  const std::string scripts = jstringToStd(env, scriptsDir);
  const std::string start = jstringToStd(env, startScenario);
  const std::string renderer = jstringToStd(env, rendererBackend);
  const std::string upscaler = jstringToStd(env, upscalerBackend);
  const std::string preset = jstringToStd(env, upscalerPreset);
  const std::string factor = jstringToStd(env, upscalerFactor);
  const int ok = KotonohaLauncherSaveGameParams(
      profile.c_str(), overlay.c_str(), translation.c_str(),
      subtitleStyle.c_str(), customDocs.c_str(), ktrf.c_str(), scripts.c_str(),
      start.c_str(), renderer.c_str(), upscaler.c_str(), preset.c_str(),
      factor.c_str());
  return ok != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeUpscalePresetsJson(JNIEnv *env,
                                                                  jclass) {
  char *json = KotonohaLauncherUpscalePresetsJson();
  jstring result = env->NewStringUTF(json ? json : "[]");
  KotonohaLauncherFreeString(json);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeDefaultSchoolDaysRoute(
    JNIEnv *env, jclass) {
  char *route = KotonohaLauncherDefaultSchoolDaysRoute();
  jstring result = env->NewStringUTF(route ? route : "");
  KotonohaLauncherFreeString(route);
  return result;
}

JNIEXPORT jstring JNICALL
Java_org_kotonoha_android_LauncherBridge_nativeValidateRoute(JNIEnv *env,
                                                             jclass,
                                                             jstring ktrfPath) {
  const std::string path = jstringToStd(env, ktrfPath);
  char buf[512] = {};
  const int ok = KotonohaLauncherValidateRoute(path.c_str(), buf, sizeof(buf));
  const std::string prefix = ok ? "OK|" : "ERR|";
  return env->NewStringUTF((prefix + buf).c_str());
}

} // extern "C"
#endif
