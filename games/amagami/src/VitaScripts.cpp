#include <Amagami/VitaScripts.hpp>
#include <algorithm>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <stdexcept>
#include <utility>

namespace Amagami::Vita {
namespace {
Bytes readImage(const std::filesystem::path &path) {
  auto bytes = Kotonoha::Filesystem::readLocalFile(path, 64u * 1024u * 1024u);
  if (bytes.size() < 4)
    throw std::runtime_error("Amagami Vita: invalid system image size");
  return bytes;
}
bool endsWith(const std::string &path, const std::string &suffix) {
  return path.size() > suffix.size() &&
         path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

VitaScripts::VitaScripts(const std::filesystem::path &root)
    : scenarioCpk_((root / "SCENARIO.CPK").string()),
      scenarioIndex_(cpk_read_index(scenarioCpk_)) {
  const auto unpacked = Unpack(readImage(root / "AMAGAMI.IMG"));
  const auto classes = ParseClassImage(unpacked);
  if (classes.empty())
    throw std::runtime_error("Amagami Vita: empty SCF system image");
  system_.reserve(classes.size());
  for (const auto &klass : classes) {
    Script s = ParseScf(klass.bytes);
    if (s.name != klass.name)
      throw std::runtime_error("Amagami Vita: system class name mismatch");
    system_.push_back(std::move(s));
  }

  scenarioFiles_.reserve(scenarioIndex_.size());
  for (const auto &entry : scenarioIndex_) {
    if (!endsWith(entry.first, ".scf"))
      continue;
    // CPK filenames can have directories; preserve the full path for reads,
    // but identify classes by basename for the SCF VM.
    const auto slash = entry.first.find_last_of("/\\");
    const std::string stem = entry.first.substr(
        slash == std::string::npos ? 0 : slash + 1,
        entry.first.size() - (slash == std::string::npos ? 0 : slash + 1) - 4);
    if (stem.empty() || !scenarioFiles_.emplace(stem, entry.first).second)
      throw std::runtime_error("Amagami Vita: duplicate scenario " + stem);
    scenarioNames_.push_back(stem);
  }
  if (scenarioNames_.empty())
    throw std::runtime_error("Amagami Vita: no SCF scenarios");
  std::sort(scenarioNames_.begin(), scenarioNames_.end());
}

Script VitaScripts::scenario(const std::string &name) const {
  auto i = scenarioFiles_.find(name);
  if (i == scenarioFiles_.end())
    throw std::out_of_range("Amagami Vita: unknown scene " + name);
  std::string content;
  if (!cpk_read(scenarioCpk_, scenarioIndex_, i->second, content))
    throw std::runtime_error("Amagami Vita: SCF entry disappeared: " + name);
  const Bytes bytes(content.begin(), content.end());
  auto script = ParseScf(bytes);
  if (script.name != name)
    throw std::runtime_error(
        "Amagami Vita: SCF name does not match CPK path: " + name + " versus " +
        script.name);
  return script;
}

void VitaScripts::installSystem(Vm &vm) const {
  for (const auto &script : system_)
    vm.add(script);
}
void VitaScripts::installAll(Vm &vm) const {
  installSystem(vm);
  for (const auto &name : scenarioNames_)
    vm.add(scenario(name));
}
} // namespace Amagami::Vita
