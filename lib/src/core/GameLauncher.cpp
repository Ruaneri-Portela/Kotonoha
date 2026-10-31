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

void GameLauncher::loadPath(std::string path) {
  setPath(path.empty() ? readFirstLine(game.rememberPath) : std::move(path));
}

const std::string &GameLauncher::path() const { return game.current; }

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
  return additionalPaths.at(index)->current;
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
  if (field.current.empty()) {
    field.info = "Choose the " + field.options.label;
    return;
  }
  field.info.clear();
  field.valid = field.validator(field.current, field.info);
  if (!field.valid && field.info.empty())
    field.info = "Invalid " + field.options.label;
}

void GameLauncher::drawPathField(PathField &field) {
  constexpr const char *Browse = "Browse...";
  const ImGuiStyle &style = ImGui::GetStyle();
  ImGui::TextUnformatted(field.options.label.c_str());
  ImGui::SetNextItemWidth(-ImGui::CalcTextSize(Browse).x -
                          style.ItemSpacing.x * 2 - style.FramePadding.x * 2);
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
      browseFolder(field.current, chosen);
    } else {
      const std::string initial =
          field.current.empty()
              ? std::string()
              : std::filesystem::path(field.current).parent_path().string();
      browseFile(field.options.filters, initial, chosen);
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
  check(game);
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
    if (!field.rememberPath.empty())
      writeTextFileAtomically(field.rememberPath, field.current + "\n");
  };
  remember(game);
  for (const auto &field : additionalPaths)
    remember(*field);
}

} // namespace Kotonoha
