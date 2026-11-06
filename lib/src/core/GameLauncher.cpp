#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/GameLauncher.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "../debug/ImGuiTheme.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Kotonoha {

namespace {
std::string rememberFilePath(const std::string &settingsPath,
                             const std::string &rememberFile) {
  if (settingsPath.empty() || rememberFile.empty())
    return {};
  return (std::filesystem::path(settingsPath).parent_path() / rememberFile)
      .string();
}

std::string readFirstLine(const std::string &file) {
  std::string line;
  if (!file.empty()) {
    std::ifstream input(file);
    std::getline(input, line);
  }
  return line;
}
} // namespace

GameLauncher::GameLauncher(std::string title, std::string settingsPath,
                           PathOptions options, EngineSettings defaults)
    : Launcher(std::move(title), settingsPath, std::move(defaults)) {
  game.options = std::move(options);
  game.rememberPath = rememberFilePath(settingsPath, game.options.rememberFile);
  game.validator = [this](const std::string &path, std::string &info) {
    return checkPath(path, info);
  };
}

std::string GameLauncher::rememberedPath(const std::string &settingsPath,
                                         const std::string &rememberFile) {
  return readFirstLine(rememberFilePath(settingsPath, rememberFile));
}

std::string GameLauncher::findGamePath(
    const std::string &game, const std::vector<std::string> &preferred,
    const std::function<bool(const std::string &, std::string &)> &validator,
    const std::vector<std::string> &relative, bool directISO) {
  auto valid = [&](const std::string &path, bool required) {
    if (path.empty())
      return false;
    std::string info;
    try {
      if (validator(path, info))
        return true;
      if (!directISO && isISOImage(path) &&
          validator(gameDataDirectory(path).string(), info))
        return true;
      if (required)
        throw std::runtime_error("Invalid game ISO " + path + ": " + info);
      return false;
    } catch (const std::exception &error) {
      if (required)
        throw;
      if (isISOImage(path))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ISO %s: %s", path.c_str(),
                    error.what());
      return false;
    }
  };
  for (std::size_t i = 0; i < preferred.size(); ++i) {
    const bool required = i == 0 && isISOImage(preferred[i]);
    for (const auto &path : FileResolver::candidates(preferred[i], game)) {
      if (required && !std::filesystem::is_regular_file(path))
        continue;
      if (valid(path.string(), required))
        return path.string();
    }
    if (required)
      throw std::runtime_error("ISO image not found: " + preferred[i]);
  }
  for (const auto &root : applicationDirectories(game).gameAssets)
    for (const auto &tail : relative) {
      const auto path = (root / tail).lexically_normal().string();
      if (valid(path, false))
        return path;
    }
  return {};
}

void GameLauncher::loadPath(std::string path) {
  setPath(path.empty() ? readFirstLine(game.rememberPath) : std::move(path));
  if (!pathValid())
    setPath({});
}

const std::string &GameLauncher::path() const { return game.resolved; }

void GameLauncher::setPath(std::string path) {
  setFieldPath(game, std::move(path));
}

bool GameLauncher::pathValid() const { return game.valid; }

const std::string &GameLauncher::pathInfo() const { return game.info; }

std::size_t GameLauncher::addPathOption(PathOptions options,
                                        PathValidator validator,
                                        std::string path) {
  if (!validator)
    throw std::invalid_argument("A launcher path validator is required");
  auto field = std::make_unique<PathField>();
  field->options = std::move(options);
  field->validator = std::move(validator);
  field->rememberPath =
      rememberFilePath(getSettingsPath(), field->options.rememberFile);
  setFieldPath(*field, path.empty() ? readFirstLine(field->rememberPath)
                                    : std::move(path));
  additionalPaths.push_back(std::move(field));
  return additionalPaths.size() - 1;
}

const std::string &GameLauncher::optionPath(std::size_t index) const {
  return additionalPaths.at(index)->resolved;
}

void GameLauncher::setOptionPath(std::size_t index, std::string path) {
  setFieldPath(*additionalPaths.at(index), std::move(path));
}

bool GameLauncher::optionPathValid(std::size_t index) const {
  return additionalPaths.at(index)->valid;
}

const std::string &GameLauncher::optionPathInfo(std::size_t index) const {
  return additionalPaths.at(index)->info;
}

void GameLauncher::setFieldPath(PathField &field, std::string path) {
  field.current = std::move(path);
  check(field);
}

void GameLauncher::check(PathField &field, bool force) {
  if (!force && field.checkedOnce && field.checked == field.current)
    return;
  field.checkedOnce = true;
  field.checked = field.current;
  field.valid = false;
  field.resolved.clear();
  if (field.current.empty()) {
    field.info = "Choose the " + field.options.label;
    return;
  }
  field.info.clear();
  for (const auto &candidate : FileResolver::candidates(field.current)) {
    try {
      const auto input =
          field.options.kind == PathKind::Folder && !field.options.directISO
              ? gameDataDirectory(candidate)
              : candidate;
      if (field.validator(input.string(), field.info)) {
        field.valid = true;
        field.resolved = candidate.string();
        break;
      }
    } catch (const std::exception &error) {
      field.info = error.what();
    }
  }
  if (!field.valid && field.info.empty())
    field.info = "Invalid " + field.options.label;
}

void GameLauncher::drawPathField(PathField &field) {
  const char *Browse =
      field.options.kind == PathKind::Folder ? "Folder..." : "Browse...";
  const ImGuiStyle &style = ImGui::GetStyle();
  ImGui::TextUnformatted(field.options.label.c_str());
  const float isoWidth = field.options.kind == PathKind::Folder
                             ? ImGui::CalcTextSize("ISO...").x +
                                   style.FramePadding.x * 2 +
                                   style.ItemSpacing.x
                             : 0;
  ImGui::SetNextItemWidth(-ImGui::CalcTextSize(Browse).x -
                          style.ItemSpacing.x * 2 - style.FramePadding.x * 2 -
                          isoWidth);
  if (ImGui::InputText("##path", &field.current,
                       ImGuiInputTextFlags_EnterReturnsTrue) ||
      !ImGui::IsItemActive())
    check(field);
  ImGui::SameLine();
  if (ImGui::Button(Browse)) {
    auto chosen = [this, target = &field](const std::string &path) {
      setFieldPath(*target, path);
    };
    if (field.options.kind == PathKind::Folder) {
      browseFolder(
          isISOImage(field.current)
              ? std::filesystem::path(field.current).parent_path().string()
              : field.current,
          chosen);
    } else {
      const std::string initial =
          field.current.empty()
              ? std::string()
              : std::filesystem::path(field.current).parent_path().string();
      browseFile(field.options.filters, initial, chosen);
    }
  }
  if (field.options.kind == PathKind::Folder) {
    ImGui::SameLine();
    if (ImGui::Button("ISO...")) {
      browseFile(
          {{"ISO disc image", "iso"}, {"All files", "*"}},
          std::filesystem::is_directory(field.current)
              ? field.current
              : std::filesystem::path(field.current).parent_path().string(),
          [this, target = &field](const std::string &path) {
            setFieldPath(*target, path);
          });
    }
  }
  ImGui::PushStyleColor(ImGuiCol_Text, field.valid ? ImGuiTheme::success()
                                                   : ImGuiTheme::warning());
  ImGui::TextWrapped("%s", field.info.c_str());
  ImGui::PopStyleColor();
  if (!field.options.hint.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", field.options.hint.c_str());
  }
}

void GameLauncher::drawGameOptions() {
  ImGui::PushID("game");
  drawPathField(game);
  ImGui::PopID();
  for (std::size_t i = 0; i < additionalPaths.size(); ++i) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushID(static_cast<int>(i));
    drawPathField(*additionalPaths[i]);
    ImGui::PopID();
  }
}

bool GameLauncher::validate(std::string &error) {
  if (!Launcher::validate(error))
    return false;
  check(game, true);
  if (!game.valid) {
    error = game.info;
    return false;
  }
  for (const auto &field : additionalPaths) {
    check(*field, true);
    if (!field->valid) {
      error = field->info;
      return false;
    }
  }
  return true;
}

void GameLauncher::save() {
  const auto remember = [](const PathField &field) {
    if (field.rememberPath.empty()) return;
    std::string reference = field.resolved;
    if (!isISOImage(field.resolved)) {
      const auto portable = FileResolver::referenceName(field.resolved);
      for (const auto &candidate : FileResolver::candidates(portable)) {
        std::error_code error;
        if (std::filesystem::equivalent(candidate, field.resolved, error) &&
            !error) {
          reference = portable;
          break;
        }
      }
    }
    writeTextFileAtomically(field.rememberPath, reference + "\n");
  };
  remember(game);
  for (const auto &field : additionalPaths)
    remember(*field);
}

FolderGameLauncher::FolderGameLauncher(std::string title,
                                       std::string settingsPath,
                                       std::string path, Validator check,
                                       EngineSettings defaults, bool directISO)
    : GameLauncher(
          std::move(title), std::move(settingsPath),
          {"Game data", PathKind::Folder, {}, "dir.txt", {}, directISO},
          std::move(defaults)),
      validator(std::move(check)) {
  if (!validator)
    throw std::invalid_argument("A game data validator is required");
  loadPath(std::move(path));
}

bool FolderGameLauncher::checkPath(const std::string &path, std::string &info) {
  return validator(path, info);
}

} // namespace Kotonoha
