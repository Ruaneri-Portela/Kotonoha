#pragma once

// School Days-specific, opt-in native translation packs. Generic Kotonoha
// EngineSettings and its JSON translation format remain untouched. The
// launcher stores only a locale ID in the game profile; no absolute paths to
// a player's installation or retail GPK are serialized.

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Kotonoha::SchoolDaysGpkLanguageProfile {

namespace fs = std::filesystem;

inline constexpr const char *Version = "SchoolDaysTranslationPack.v1";

inline bool Supported(const std::string &locale) {
  return locale == "en" || locale == "pt-BR" ||
         locale == "es" || locale == "zh-CN" || locale == "ru";
}

inline fs::path Sidecar(const std::string &settingsFile) {
  return fs::path(settingsFile).parent_path() / "translation-pack.txt";
}

// An absent file means a legacy native/JSON profile and is not a failure.
// An invalid existing file always fails closed rather than silently returning
// to the previous language.
inline std::string Read(const std::string &settingsFile) {
  if (settingsFile.empty()) return {};
  const fs::path file = Sidecar(settingsFile);
  std::error_code ec;
  if (!fs::exists(file, ec) && !ec) return {};
  if (!fs::is_regular_file(file, ec) || ec ||
      fs::file_size(file, ec) > 256 || ec)
    throw std::runtime_error("Invalid School Days native-language profile");
  std::ifstream input(file, std::ios::binary);
  std::string marker, locale, extra;
  if (!std::getline(input, marker) || !std::getline(input, locale))
    throw std::runtime_error("Malformed translation-pack.txt (native GPK profile)");
  if (!marker.empty() && marker.back() == '\r') marker.pop_back();
  if (!locale.empty() && locale.back() == '\r') locale.pop_back();
  if (marker != Version || !Supported(locale) || std::getline(input, extra))
    throw std::runtime_error("Malformed translation-pack.txt (native GPK profile)");
  return locale;
}

namespace detail {

inline std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return value;
}

inline bool CheckedDirectory(const fs::path &directory) {
  std::error_code ec;
  const auto status = fs::symlink_status(directory, ec);
  if (ec) {
    if (ec == std::errc::no_such_file_or_directory) return false;
    throw std::runtime_error("Cannot inspect School Days translation directory: " +
                             directory.string());
  }
  if (status.type() == fs::file_type::not_found) return false;
  if (!fs::is_directory(status))
    throw std::runtime_error("Invalid School Days translation directory: " +
                             directory.string());
  return true;
}

// Directory enumeration is intentional: on Windows an ordinary exists() call
// silently accepts differently-cased paths. A selected pt-BR-lxh.GPK must
// never resolve to a different locale by case-folding or legacy heuristics.
inline std::optional<fs::path> ExactFile(const fs::path &directory,
                                         const std::string &filename) {
  if (!CheckedDirectory(directory)) return std::nullopt;
  std::error_code ec;
  const fs::directory_iterator iterator(directory, ec);
  if (ec)
    throw std::runtime_error("Cannot inspect School Days translation files: " +
                             directory.string());
  bool wrongCase = false;
  for (const auto &entry : iterator) {
    const std::string actual = entry.path().filename().string();
    if (actual != filename) {
      if (LowerAscii(actual) == LowerAscii(filename)) wrongCase = true;
      continue;
    }
    const auto status = entry.symlink_status(ec);
    if (ec || !fs::is_regular_file(status))
      throw std::runtime_error("School Days translation is not a regular file: " +
                               entry.path().string());
    return entry.path();
  }
  if (wrongCase)
    throw std::runtime_error("School Days translation filename casing mismatch: " +
                             (directory / filename).string());
  return std::nullopt;
}

inline void ValidateBasename(const std::string &filename,
                             const std::string &extension) {
  const bool controls = std::any_of(
      filename.begin(), filename.end(),
      [](unsigned char c) { return c < 0x20 || c == 0x7f; });
  if (filename.empty() || filename == "." || filename == ".." ||
      controls ||
      filename.find_first_of("/\\:") != std::string::npos ||
      fs::path(filename).filename().string() != filename ||
      fs::path(filename).extension().string() != extension)
    throw std::invalid_argument("Invalid School Days " + extension +
                                " translation filename");
}

inline fs::path Translations(const std::string &settingsFile) {
  if (settingsFile.empty())
    throw std::invalid_argument("School Days translation requires settings path");
  return fs::path(settingsFile).parent_path() / "translations";
}

inline fs::path Locate(const fs::path &root, const std::string &subfolder,
                       const std::string &name) {
  // Prefer the new organized layout, then an old flat Release/translations.
  // An unsafe primary path fails closed, never silently selects a flat pack.
  const auto organized = root / subfolder;
  if (auto file = ExactFile(organized, name)) return *file;
  if (auto file = ExactFile(root, name)) return *file;
  // Keep a deterministic *missing* preferred path. Launcher/main must reject
  // the absent selected language rather than choosing English.
  return organized / name;
}

inline std::vector<fs::path> Discover(const fs::path &root,
                                     const std::string &subfolder,
                                     const std::string &extension,
                                     bool baseOnly) {
  std::map<std::string, fs::path> result;
  std::map<std::string, std::string> folded;
  for (const auto &directory : {root / subfolder, root}) {
    if (!CheckedDirectory(directory)) continue;
    std::error_code ec;
    const fs::directory_iterator iterator(directory, ec);
    if (ec)
      throw std::runtime_error("Cannot enumerate translations: " +
                               directory.string());
    for (const auto &entry : iterator) {
      const auto filename = entry.path().filename().string();
      const auto normalized = LowerAscii(filename);
      if (LowerAscii(fs::path(filename).extension().string()) !=
          LowerAscii(extension))
        continue;
      if (baseOnly &&
          (normalized.size() >= 9 &&
           normalized.compare(normalized.size() - 9, 9, "-lxh.json") == 0))
        continue;
      const std::string reviewed = "-reviewed-identities.json";
      if (baseOnly && normalized.size() >= reviewed.size() &&
          normalized.compare(normalized.size() - reviewed.size(),
                             reviewed.size(), reviewed) == 0)
        continue;
      const auto status = entry.symlink_status(ec);
      if (ec || !fs::is_regular_file(status))
        throw std::runtime_error("Invalid School Days translation file: " +
                                 entry.path().string());
      if (const auto it = folded.find(normalized); it != folded.end() &&
          it->second != filename)
        throw std::runtime_error("Ambiguous case-colliding School Days locale: " +
                                 filename);
      folded[normalized] = filename;
      // Exact duplicate filename in legacy flat root: new folder wins.
      result.emplace(filename, entry.path());
    }
  }
  std::vector<fs::path> paths;
  paths.reserve(result.size());
  for (const auto &[filename, path] : result) paths.push_back(path);
  return paths;
}

} // namespace detail

inline fs::path ScriptPath(const std::string &settingsFile,
                           const std::string &locale) {
  if (!Supported(locale)) throw std::invalid_argument("Unsupported native GPK locale");
  // ENGLISH HQ ALWAYS USES Packs/Script.GPK; never duplicate or load an
  // English Script archive from translations/. Only its added LxH text has
  // a separate locale GPK.
  if (locale == "en") return {};
  const char *name = locale == "pt-BR" ? "Script - Brazilian.GPK"
                      : locale == "es" ? "Script - Spanish.GPK"
                      : locale == "ru" ? "Script - Russian.GPK"
                                       : "Script - chinese-compatible.GPK";
  // The downloaded Chinese pack is retained byte-for-byte as
  // "Script - chinese.GPK". It uses older GB18030/quoted-comma ORS records
  // and different paths; the compatible derivative must be validated before
  // enabling native playback (never parse the incompatible raw pack as EN).
  return detail::Locate(detail::Translations(settingsFile), "scripts", name);
}

inline fs::path LxhPath(const std::string &settingsFile,
                        const std::string &locale) {
  if (!Supported(locale)) throw std::invalid_argument("Unsupported native GPK locale");
  return detail::Locate(detail::Translations(settingsFile), "scripts",
                        locale + "-lxh.GPK");
}

// Accept only a literal filename, never a path or an inferred language. The
// caller must select its intended locale explicitly (e.g. "zh-CN.json").
// Used for legacy JSON migrations and custom JSON selection.
inline fs::path JsonPath(const std::string &settingsFile,
                         const std::string &filename) {
  detail::ValidateBasename(filename, ".json");
  return detail::Locate(detail::Translations(settingsFile), "jsons", filename);
}

// Enumerate installed translations without assuming that every JSON is a base
// language. Sidecars/review manifests are deliberately omitted unless asked.
inline std::vector<fs::path> DiscoverJsonFiles(const std::string &settingsFile,
                                               bool includeAuxiliary = false) {
  return detail::Discover(detail::Translations(settingsFile), "jsons", ".json",
                          !includeAuxiliary);
}

inline std::vector<fs::path> DiscoverScriptFiles(const std::string &settingsFile) {
  return detail::Discover(detail::Translations(settingsFile), "scripts", ".GPK",
                          false);
}

} // namespace Kotonoha::SchoolDaysGpkLanguageProfile
