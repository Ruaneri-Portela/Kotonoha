#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {
CommandLine::CommandLine(std::string program, EngineOptions &engine)
    : program(std::move(program)) {
  flag("--help", "Show engine and game options without opening the game", helpRequested);
  alias("--help", "-h");
  option("--config", "FILE", "Load and save engine settings", engine.settingsPath);
  flag("--no-config", "Do not persist engine settings", [&engine] { engine.persistSettings = false; });
  option("--renderer", "NAME", "SDL renderer override (requires restart)", engine.rendererOverride);
  option("--log", "FILE", "Write engine logs to this file", engine.logPath);
  flag("--mcp", "Enable the stdio MCP interface", engine.enableMCP);
  option("--mcp-root", "DIRECTORY", "Restrict MCP files to this root", engine.mcpFileRoot);
  option("--translation", "FILE", "Load a translation dictionary", engine.translationPath);
  option("--subtitle-style", "FILE.ass",
         "Use ASS script info/styles only; discard its dialogue events", engine.subtitleStylePath);
  alias("--subtitle-style", "--ass-style");
  option("--frames", "N", "Exit after N rendered frames (positive integer)",
         [this](const std::string &value) {
           long count = 0;
           const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
           if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || count <= 0)
             throw std::invalid_argument("--frames must be a positive integer");
           frames = count;
         });
}

void CommandLine::option(std::string name, std::string value, std::string description,
                         std::function<void(const std::string &)> action) {
  if (name.size() < 2 || name[0] != '-' || name.find_first_of("= \t\r\n") != std::string::npos ||
      !action || std::any_of(options.begin(), options.end(),
          [&](const Option &entry) { return entry.name == name; }))
    throw std::invalid_argument("Invalid or duplicate command-line option: " + name);
  options.push_back({std::move(name), std::move(value), std::move(description), std::move(action)});
}
void CommandLine::option(std::string name, std::string value, std::string description,
                         std::string &target) {
  if (value.empty())
    throw std::invalid_argument("A value option needs a value name");
  option(std::move(name), std::move(value), std::move(description),
         [&target](const std::string &text) { target = text; });
}
void CommandLine::flag(std::string name, std::string description, std::function<void()> action) {
  if (!action)
    throw std::invalid_argument("A flag needs an action");
  option(std::move(name), {}, std::move(description),
         [action = std::move(action)](const std::string &) { action(); });
}
void CommandLine::flag(std::string name, std::string description, bool &target) {
  flag(std::move(name), std::move(description), [&target] { target = true; });
}
void CommandLine::alias(const std::string &name, const std::string &alias) {
  const auto found = std::find_if(options.begin(), options.end(),
      [&](const Option &entry) { return entry.name == name; });
  if (found == options.end())
    throw std::invalid_argument("Unknown aliased command-line option: " + name);
  const auto original = *found;
  option(alias, original.value, "Alias for " + name, original.action);
}
void CommandLine::parse(int argc, char **argv) {
  if (argc < 1 || !argv)
    throw std::invalid_argument("Invalid command-line arguments");
  for (int i = 1; i < argc; ++i) {
    if (!argv[i])
      throw std::invalid_argument("Null command-line argument");
    const std::string argument = argv[i];
    const auto equals = argument.find('=');
    const auto name = argument.substr(0, equals);
    const auto found = std::find_if(options.begin(), options.end(),
        [&](const Option &entry) { return entry.name == name; });
    if (found == options.end())
      throw std::invalid_argument("Unknown option: " + name + "\n" + help());
    std::string value;
    if (found->value.empty()) {
      if (equals != std::string::npos)
        throw std::invalid_argument("Flag does not accept a value: " + name);
    } else {
      if (equals != std::string::npos)
        value = argument.substr(equals + 1);
      else if (++i < argc && argv[i])
        value = argv[i];
      if (value.empty() || value.front() == '-')
        throw std::invalid_argument("Missing value for " + name + " " + found->value);
    }
    found->action(value);
  }
}
std::string CommandLine::help() const {
  std::ostringstream result;
  result << "Usage: " << program << " [options]\n\n";
  for (const auto &entry : options) {
    const auto label = entry.name + (entry.value.empty() ? "" : " " + entry.value);
    result << "  " << std::left << std::setw(34) << label << entry.description << '\n';
  }
  return result.str();
}
} // namespace Kotonoha
