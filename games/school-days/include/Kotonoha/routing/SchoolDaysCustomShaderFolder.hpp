#pragma once

#include <Kotonoha/routing/SchoolDaysShaderPresetCatalog.hpp>
#include <Kotonoha/core/FileSystem.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Kotonoha::SchoolDaysCustomShaderFolder {

namespace fs = std::filesystem;

struct Folder {
  fs::path directory;
  std::vector<fs::path> shaders;
};

// Browsing a folder lists its immediate regular shader files. The MPV backend
// expects a single shader file path; the directory is never passed to MPV.
inline std::optional<Folder> Scan(const fs::path &directory,
                                  std::string *error = nullptr) {
  if (error) error->clear();
  std::error_code ec;
  auto absolute = fs::absolute(directory, ec);
  if (ec || !fs::is_directory(absolute, ec) || ec) {
    if (error) *error = "Select an existing shader folder";
    return std::nullopt;
  }
  absolute = absolute.lexically_normal();
  Folder result{absolute, {}};
  fs::directory_iterator it(absolute, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    if (error) *error = "Cannot enumerate this shader folder";
    return std::nullopt;
  }
  for (const fs::directory_iterator end; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code fileError;
    if (!it->is_regular_file(fileError) || fileError ||
        !SchoolDaysShaderPresetCatalog::SupportedShaderFile(
            it->path().filename().u8string()))
      continue;
    result.shaders.push_back(it->path());
    if (result.shaders.size() > 512) {
      if (error) *error = "Folder has too many shader files (maximum 512)";
      return std::nullopt;
    }
  }
  if (ec) {
    if (error) *error = "Cannot finish reading this shader folder";
    return std::nullopt;
  }
  std::sort(result.shaders.begin(), result.shaders.end(),
            [](const fs::path &a, const fs::path &b) {
              auto nameA = a.filename().u8string();
              auto nameB = b.filename().u8string();
              std::transform(nameA.begin(), nameA.end(), nameA.begin(),
                             [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
              std::transform(nameB.begin(), nameB.end(), nameB.begin(),
                             [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
              return nameA == nameB ? a.u8string() < b.u8string() : nameA < nameB;
            });
  if (result.shaders.empty()) {
    if (error) *error = "Folder contains no .glsl, .hook or .frag files";
    return std::nullopt;
  }
  return result;
}

inline float RecommendedFactor(const fs::path &file, float currentFactor) {
  auto filename = file.filename().u8string();
  std::transform(filename.begin(), filename.end(), filename.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (int factor = 2; factor <= 4; ++factor) {
    const std::string suffix = "x" + std::to_string(factor);
    for (const char separator : {'_', '-'}) {
      const std::string signature = std::string(1, separator) + suffix;
      const auto position = filename.find(signature);
      if (position != std::string::npos &&
          (position + signature.size() == filename.size() ||
           !std::isdigit(static_cast<unsigned char>(filename[position + signature.size()]))))
        return static_cast<float>(factor);
    }
  }
  return std::max(2.0f, currentFactor);
}

inline fs::path Sidecar(const std::string &settingsPath) {
  return fs::u8path(settingsPath).parent_path() / "shader-folders.txt";
}

inline fs::path ReadSidecar(const std::string &settingsPath) {
  if (settingsPath.empty()) return {};
  std::error_code ec;
  const auto sidecar = Sidecar(settingsPath);
  if (!fs::is_regular_file(sidecar, ec) || ec ||
      fs::file_size(sidecar, ec) > 4096 || ec)
    return {};
  std::ifstream input(sidecar, std::ios::binary);
  std::string header, directory, trailing;
  if (!std::getline(input, header) || header != "SchoolDaysShaderFolders.v1" ||
      !std::getline(input, directory) || std::getline(input, trailing) ||
      directory.empty() || directory.find('\r') != std::string::npos ||
      directory.find('\0') != std::string::npos)
    return {};
  try { return fs::u8path(directory); }
  catch (const std::exception &) { return {}; }
}

inline bool SaveSidecar(const std::string &settingsPath,
                        const fs::path &directory, std::string *error = nullptr) {
  if (error) error->clear();
  if (settingsPath.empty()) return true;
  try {
    const auto value = directory.u8string();
    if (value.size() > 3900 || value.find('\n') != std::string::npos ||
        value.find('\r') != std::string::npos ||
        value.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid shader folder path");
    // FileSystem's atomic writer keeps the old folder if a write fails.
    Kotonoha::writeTextFileAtomically(
        Sidecar(settingsPath).string(),
        "SchoolDaysShaderFolders.v1\n" + value + "\n");
    return true;
  } catch (const std::exception &failure) {
    if (error) *error = failure.what();
    return false;
  }
}

struct Row {
  enum class Type { BuiltinFolder, BuiltinPreset, CustomFolder, CustomFile };
  Type type = Type::BuiltinFolder;
  SchoolDaysShaderPresetCatalog::Tier tier =
      SchoolDaysShaderPresetCatalog::Tier::Low;
  UpscalePreset preset = UpscalePreset::Anime4K;
  fs::path shader;
};

inline std::vector<Row> VisibleRows(const std::array<bool, 3> &expanded,
                                    bool customExpanded, const Folder *folder) {
  namespace Base = SchoolDaysShaderPresetCatalog;
  std::vector<Row> rows;
  for (const auto &row : Base::VisibleRows(expanded))
    rows.push_back({row.heading ? Row::Type::BuiltinFolder : Row::Type::BuiltinPreset,
                    row.tier, row.preset, {}});
  if (folder) {
    rows.push_back({Row::Type::CustomFolder, Base::Tier::Low,
                    UpscalePreset::Anime4K, {}});
    if (customExpanded)
      for (const auto &shader : folder->shaders)
        rows.push_back({Row::Type::CustomFile, Base::Tier::Low,
                        UpscalePreset::Anime4K, shader});
  }
  return rows;
}

} // namespace Kotonoha::SchoolDaysCustomShaderFolder
