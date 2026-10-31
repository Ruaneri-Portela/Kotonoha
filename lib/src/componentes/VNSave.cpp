#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {
namespace {
std::string saveFileName(std::string name) {
  if (name.empty() || name.find_first_not_of(' ') == std::string::npos ||
      name.find_first_of("/\\:*?\"<>|") != std::string::npos ||
      std::any_of(name.begin(), name.end(),
                  [](unsigned char ch) { return ch < 32 || ch == 127; }) ||
      name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
    throw std::invalid_argument(
        "Invalid save name; use a file name, not a path");
  if (std::filesystem::path(name).extension() != ".vn")
    name += ".vn";
  if (name.size() > 255)
    throw std::invalid_argument("Save file name exceeds 255 UTF-8 bytes");
  return name;
}

std::filesystem::path existingSlot(const std::string &directory,
                                   const std::string &name) {
  if (saveFileName(name) != name)
    throw std::invalid_argument("Expected the existing .vn save file name");
  const auto path = std::filesystem::path(directory) / name;
  if (std::filesystem::is_symlink(path) ||
      !std::filesystem::is_regular_file(path))
    throw std::runtime_error("Save is missing or is not a regular file: " +
                             path.string());
  return path;
}
} // namespace

VNSaveSlots::VNSaveSlots(std::string directory)
    : directory(std::move(directory)) {
  if (this->directory.empty())
    throw std::invalid_argument("Save directory cannot be empty");
}

std::vector<VNSaveSlot> VNSaveSlots::list() const {
  std::vector<VNSaveSlot> slots;
  if (!std::filesystem::exists(directory))
    return slots;
  for (const auto &entry : std::filesystem::directory_iterator(directory))
    if (entry.is_regular_file() && entry.path().extension() == ".vn")
      slots.push_back(
          {entry.path().string(), entry.path().filename().string()});
  std::sort(slots.begin(), slots.end(),
            [](const VNSaveSlot &left, const VNSaveSlot &right) {
              if (left.name.size() != right.name.size())
                return left.name.size() < right.name.size();
              return left.name < right.name;
            });
  return slots;
}

std::string VNSaveSlots::nextPath() const {
  std::filesystem::create_directories(directory);
  for (std::uint64_t id = 1;; ++id) {
    const auto path = std::filesystem::path(directory) /
                      ("save-" + std::to_string(id) + ".vn");
    if (!std::filesystem::exists(path))
      return path.string();
    if (id == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("No save file name is available");
  }
}

std::string VNSaveSlots::rename(const std::string &name,
                                const std::string &newName) const {
  const auto source = existingSlot(directory, name);
  const auto target = std::filesystem::path(directory) / saveFileName(newName);
  if (source == target)
    return target.string();
  if (std::filesystem::exists(std::filesystem::symlink_status(target)))
    throw std::runtime_error("A file with this save name already exists");
  if (!SDL_RenamePath(source.string().c_str(), target.string().c_str()))
    throw std::runtime_error("Cannot rename save: " +
                             std::string(SDL_GetError()));
  return target.string();
}

void VNSaveSlots::remove(const std::string &name) const {
  const auto path = existingSlot(directory, name);
  if (!std::filesystem::remove(path))
    throw std::runtime_error("Cannot delete missing save: " + path.string());
}

namespace {
void validate(const VNSave &save) {
  if (save.nodeId < 0 ||
      (save.mode != ScriptMode::Standard && save.mode != ScriptMode::Timed) ||
      !std::isfinite(save.timelinePosition) || save.timelinePosition < 0 ||
      (save.pendingBranch && *save.pendingBranch < -1) ||
      (save.mode == ScriptMode::Standard &&
       (save.timelinePosition != 0 || !save.resolvedChoices.empty() ||
        save.pendingBranch)))
    throw std::invalid_argument("Invalid VN save state");
  if (save.resolvedChoices.size() > 10000 ||
      std::set<std::size_t>(save.resolvedChoices.begin(),
                            save.resolvedChoices.end())
              .size() != save.resolvedChoices.size())
    throw std::invalid_argument("Invalid resolved save choices");
}
} // namespace

void VNSave::write(const std::string &path) const {
  validate(*this);
  std::ostringstream output;
  output << "KOTONOHA_SAVE 1\n"
         << nodeId << ' ' << static_cast<int>(mode) << ' '
         << std::setprecision(17) << timelinePosition << '\n'
         << resolvedChoices.size();
  for (auto index : resolvedChoices)
    output << ' ' << index;
  output << '\n'
         << (pendingBranch ? 1 : 0) << ' ' << pendingBranch.value_or(-1) << '\n'
         << std::quoted(gameData) << '\n';
  writeTextFileAtomically(path, output.str());
}

VNSave VNSave::read(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open save: " + path);
  VNSave save;
  std::string magic;
  int version = 0, mode = 0, hasBranch = 0, branch = -1;
  std::size_t count = 0;
  auto fail = [&]() {
    throw std::runtime_error("Invalid or incompatible save: " + path);
  };
  if (!(input >> magic >> version) || magic != "KOTONOHA_SAVE" ||
      version != 1 ||
      !(input >> save.nodeId >> mode >> save.timelinePosition >> count) ||
      mode < 0 || mode > 1 || count > 10000)
    fail();
  save.mode = static_cast<ScriptMode>(mode);
  for (std::size_t i = 0; i < count; ++i) {
    std::size_t index = 0;
    if (!(input >> index))
      fail();
    save.resolvedChoices.push_back(index);
  }
  if (!(input >> hasBranch >> branch) || (hasBranch != 0 && hasBranch != 1))
    fail();
  if (hasBranch)
    save.pendingBranch = branch;
  input >> std::ws;
  if (input.peek() != '"' || !(input >> std::quoted(save.gameData)))
    fail();
  input >> std::ws;
  if (!input.eof() || input.bad())
    fail();
  validate(save);
  return save;
}

} // namespace Kotonoha
