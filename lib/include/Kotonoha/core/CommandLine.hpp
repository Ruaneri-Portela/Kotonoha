#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Kotonoha {
struct EngineOptions;

class CommandLine {
  struct Option {
    std::string name, value, description;
    std::function<void(const std::string &)> action;
  };
  std::string program;
  std::vector<Option> options;
  bool helpRequested = false;

public:
  explicit CommandLine(std::string program, EngineOptions &engine);
  CommandLine(const CommandLine &) = delete;
  CommandLine &operator=(const CommandLine &) = delete;
  void option(std::string name, std::string valueName, std::string description,
              std::function<void(const std::string &)> action);
  void option(std::string name, std::string valueName, std::string description,
              std::string &target);
  void flag(std::string name, std::string description, std::function<void()> action);
  void flag(std::string name, std::string description, bool &target);
  void alias(const std::string &name, const std::string &alias);
  void parse(int argc, char **argv);
  bool wantsHelp() const { return helpRequested; }
  std::string help() const;
  long frames = -1;
};
} // namespace Kotonoha
