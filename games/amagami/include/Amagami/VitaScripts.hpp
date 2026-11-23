#pragma once

#include <Amagami/Formats.hpp>
#include <Amagami/VitaCpk.hpp>
#include <Amagami/Vm.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Amagami::Vita {

// Reads SCF classes from a separately obtained decrypted PS Vita copy.
// The PS2-only Amagami::Source remains unchanged. All script parsing and
// execution reuse Kotonoha's shared Amagami SCF parser and VM.
class VitaScripts {
public:
  explicit VitaScripts(const std::filesystem::path &root);

  std::size_t systemCount() const { return system_.size(); }
  std::size_t scenarioCount() const { return scenarioNames_.size(); }
  const std::vector<std::string> &scenarioNames() const {
    return scenarioNames_;
  }
  bool hasScenario(const std::string &name) const {
    return scenarioFiles_.count(name) != 0;
  }

  // Loads the complete system image into the VM.
  void installSystem(Vm &vm) const;
  // Eagerly installs all scenario classes for static inspections and tests.
  // The interactive Vita adapter instead calls installSystem() and installs
  // Vm::resolveClass for K2X_Script.loadClass / explicit class literals.
  void installAll(Vm &vm) const;
  Script scenario(const std::string &name) const;

private:
  std::string scenarioCpk_;
  CpkIndex scenarioIndex_;
  std::vector<Script> system_;
  std::unordered_map<std::string, std::string> scenarioFiles_;
  std::vector<std::string> scenarioNames_;
};

} // namespace Amagami::Vita
