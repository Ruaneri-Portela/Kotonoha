#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/AssetProvider.hpp>
#include <SDL3/SDL_iostream.h>
#include <limits>

namespace Kotonoha::Debug {

void SetEditorStatus(const char *text) {
  SDL_snprintf(editorStatusBuf, sizeof(editorStatusBuf), "%s",
               text ? text : "");
}

bool IsValidGameplayIndex(::Kotonoha::Kotonoha *game, int index) {
  return game != nullptr && index >= 0 &&
         index < static_cast<int>(game->gameplays.size());
}

::Kotonoha::Gameplay *GetGameplayAt(::Kotonoha::Kotonoha *game, int index) {
  if (!IsValidGameplayIndex(game, index)) {
    return nullptr;
  }
  return game->gameplays[index];
}

static std::string JoinPath(const std::string &dir, const std::string &name) {
  if (dir.empty())
    return name;
  const char last = dir.back();
  if (last == '/' || last == '\\') {
    return dir + name;
  }
#ifdef _WIN32
  return dir + "\\" + name;
#else
  return dir + "/" + name;
#endif
}

std::vector<std::string> SplitTabs(const std::string &text) {
  std::vector<std::string> fields;
  size_t start = 0;

  while (true) {
    const size_t tab = text.find('\t', start);
    if (tab == std::string::npos) {
      fields.push_back(text.substr(start));
      break;
    }
    fields.push_back(text.substr(start, tab - start));
    start = tab + 1;
  }
  return fields;
}

std::string JoinLines(const std::vector<std::string> &lines,
                      bool finalNewline) {
  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      result.push_back('\n');
    }
    result += lines[i];
  }
  if (finalNewline) {
    result.push_back('\n');
  }
  return result;
}

std::string FormatTime(Uint64 milliseconds) {
  const Uint64 totalCentiseconds = milliseconds / 10;
  const Uint64 minutes = totalCentiseconds / 6000;
  const Uint64 seconds = (totalCentiseconds / 100) % 60;
  const Uint64 centiseconds = totalCentiseconds % 100;

  char formatted[64];
  SDL_snprintf(formatted, sizeof(formatted), "%02llu:%02llu:%02llu",
               static_cast<unsigned long long>(minutes),
               static_cast<unsigned long long>(seconds),
               static_cast<unsigned long long>(centiseconds));

  return formatted;
}

std::string FormatScriptTime(const std::string &timestamp) {
  Uint64 parts[3] = {0, 0, 0};
  size_t cursor = 0;

  for (size_t part = 0; part < 3; ++part) {
    const size_t start = cursor;

    while (cursor < timestamp.size() && timestamp[cursor] >= '0' &&
           timestamp[cursor] <= '9') {
      const Uint64 digit = static_cast<Uint64>(timestamp[cursor] - '0');

      if (parts[part] > (std::numeric_limits<Uint64>::max() - digit) / 10) {
        return timestamp;
      }
      parts[part] = parts[part] * 10 + digit;
      ++cursor;
    }

    if (cursor == start ||
        (part < 2 &&
         (cursor >= timestamp.size() || timestamp[cursor++] != ':')) ||
        (part == 2 && cursor != timestamp.size())) {
      return timestamp;
    }
  }

  if (parts[0] > std::numeric_limits<Uint64>::max() / 60000) {
    return timestamp;
  }

  Uint64 milliseconds = parts[0] * 60000;

  if (parts[1] > (std::numeric_limits<Uint64>::max() - milliseconds) / 1000) {
    return timestamp;
  }
  milliseconds += parts[1] * 1000;

  if (parts[2] > (std::numeric_limits<Uint64>::max() - milliseconds) / 10) {
    return timestamp;
  }

  return FormatTime(milliseconds + parts[2] * 10);
}

bool LoadTextFile(const std::string &path, std::string &outText) {
  if (path.empty()) {
    SetEditorStatus("Empty path.");
    return false;
  }

  const AssetProvider *provider = GetAssetProvider();
  if (provider != nullptr && provider->IsVirtualReference(path)) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!provider->ReadFile(path, &bytes, &error)) {
      SetEditorStatus(error.empty() ? "Failed to read virtual scene asset."
                                    : error.c_str());
      return false;
    }
    if (bytes.empty()) {
      outText.clear();
    } else {
      outText.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    return true;
  }

  size_t dataSize = 0;
  void *data = SDL_LoadFile(path.c_str(), &dataSize);
  if (data == nullptr) {
    SetEditorStatus(SDL_GetError());
    return false;
  }

  outText.assign(static_cast<const char *>(data), dataSize);
  SDL_free(data);
  return true;
}

bool IsReadOnlyAssetPath(const std::string &path) {
  const AssetProvider *provider = GetAssetProvider();
  return provider != nullptr && provider->IsVirtualReference(path);
}

bool SaveTextFile(const std::string &path, const std::string &text) {
  if (path.empty()) {
    SetEditorStatus("Empty path.");
    return false;
  }
  if (IsReadOnlyAssetPath(path)) {
    SetEditorStatus("Assets inside a mounted archive are read-only.");
    return false;
  }

  SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "wb");
  if (io == nullptr) {
    SetEditorStatus(SDL_GetError());
    return false;
  }

  const size_t written = SDL_WriteIO(io, text.data(), text.size());
  const bool closeOk = SDL_CloseIO(io);

  if (written != text.size() || !closeOk) {
    SetEditorStatus("Failed to write full file.");
    return false;
  }

  return true;
}

std::string GetTemporaryScenePath(const std::string &path) {
  if (path.empty())
    return "";

  char *prefPath = SDL_GetPrefPath("Kotonoha", "Engine");
  if (prefPath == nullptr) {
    SetEditorStatus(SDL_GetError());
    return "";
  }

  std::string baseDir(prefPath);
  SDL_free(prefPath);

  std::string fileName = path;
  const size_t slashPos = fileName.find_last_of("/\\");
  if (slashPos != std::string::npos) {
    fileName = fileName.substr(slashPos + 1);
  }

  const std::string keepFileName = fileName + ".tmp.ks";

  struct TempCleanupData {
    std::string keepFileName;
  } cleanupData{keepFileName};

  SDL_EnumerateDirectory(
      baseDir.c_str(),
      [](void *userdata, const char *dirname,
         const char *fname) -> SDL_EnumerationResult {
        if (!userdata || !dirname || !fname)
          return SDL_ENUM_CONTINUE;

        const auto *data = static_cast<TempCleanupData *>(userdata);
        const std::string currentName = fname;

        if (currentName == "." || currentName == ".." ||
            currentName == data->keepFileName) {
          return SDL_ENUM_CONTINUE;
        }

        if (currentName.size() >= 7 &&
            currentName.substr(currentName.size() - 7) == ".tmp.ks") {
          const std::string fullPath = JoinPath(dirname, currentName);
          SDL_RemovePath(fullPath.c_str());
        }

        return SDL_ENUM_CONTINUE;
      },
      &cleanupData);

  return JoinPath(baseDir, keepFileName);
}

} // namespace Kotonoha::Debug
