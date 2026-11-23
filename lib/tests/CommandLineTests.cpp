#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Engine.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

void caseLauncher(std::vector<std::string> arguments, bool expected,
                  long expectedFrames = -1) {
  Kotonoha::EngineOptions options;
  Kotonoha::CommandLine cli("PortSmoke", options);
  std::vector<char *> argv;
  for (auto &arg : arguments)
    argv.push_back(&arg[0]);
  cli.parse(static_cast<int>(argv.size()), argv.data());
  check(options.launcher == expected,
        "Launcher policy differs from the shared command-line contract");
  check(cli.frames == expectedFrames, "Frame limit was not parsed correctly");
}

void expectBadFrames(const std::string &value) {
  Kotonoha::EngineOptions options;
  Kotonoha::CommandLine cli("PortSmoke", options);
  std::vector<std::string> args{"PortSmoke", "--frames", value};
  std::vector<char *> argv;
  for (auto &arg : args)
    argv.push_back(&arg[0]);
  bool rejected = false;
  try {
    cli.parse(static_cast<int>(argv.size()), argv.data());
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid --frames must be rejected");
}

void expectUnknownOption(std::vector<std::string> arguments,
                         const std::string &name) {
  Kotonoha::EngineOptions options;
  Kotonoha::CommandLine cli("PortSmoke", options);
  std::vector<char *> argv;
  for (auto &arg : arguments)
    argv.push_back(arg.data());

  bool rejected = false;
  try {
    cli.parse(static_cast<int>(argv.size()), argv.data());
  } catch (const std::invalid_argument &failure) {
    rejected = std::string(failure.what()).find("Unknown option: " + name) !=
               std::string::npos;
  }
  check(rejected, "Disabled developer option must be rejected as unknown");
  check(cli.help().find(name) == std::string::npos,
        "Disabled developer option must not appear in help");
  check(!options.enableMCP && options.mcpPort == 0 &&
            options.mcpFileRoot == "." && options.logPath.empty() &&
            options.launcher,
        "Rejected developer option must not modify engine state");
}

#if !defined(KOTONOHA_DISTRIBUTION_BUILD)
void expectDeveloperLog() {
  Kotonoha::EngineOptions options;
  Kotonoha::CommandLine cli("PortSmoke", options);
  std::vector<std::string> args{"PortSmoke", "--log", "synthetic.log"};
  std::vector<char *> argv;
  for (auto &arg : args)
    argv.push_back(arg.data());
  cli.parse(static_cast<int>(argv.size()), argv.data());
  check(options.logPath == "synthetic.log" &&
            cli.help().find("--log FILE") != std::string::npos,
        "DEV must retain the engine log command-line option");
}
#endif
} // namespace

int main() {
  try {
    check(Kotonoha::defaultLauncherEnabled(),
          "All platforms must initialize the launcher by default");
    caseLauncher({"PortSmoke"}, true);
    caseLauncher({"PortSmoke", "--launcher"}, true);
    caseLauncher({"PortSmoke", "--no-launcher"}, false);
    caseLauncher({"PortSmoke", "--no-launcher", "--launcher"}, true);
    caseLauncher({"PortSmoke", "--frames", "4"}, false, 4);
    caseLauncher({"PortSmoke", "--launcher", "--frames", "4"}, false, 4);
    caseLauncher({"PortSmoke", "--frames", "4", "--launcher"}, false, 4);
    caseLauncher({"PortSmoke", "--dump-translation", "synthetic.json"}, false);
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
    expectUnknownOption({"PortSmoke", "--log", "synthetic.log"}, "--log");
    expectUnknownOption({"PortSmoke", "--mcp"}, "--mcp");
    expectUnknownOption({"PortSmoke", "--mcp-root", "synthetic-root"},
                        "--mcp-root");
    expectUnknownOption({"PortSmoke", "--mcp-port=49831"}, "--mcp-port");
#else
    expectDeveloperLog();
#if KOTONOHA_ENABLE_MCP
    caseLauncher({"PortSmoke", "--mcp"}, false);
    caseLauncher({"PortSmoke", "--mcp", "--launcher"}, false);
#else
    expectUnknownOption({"PortSmoke", "--mcp"}, "--mcp");
#endif
#endif
    expectBadFrames("0");
    expectBadFrames("-4");
    expectBadFrames("bad");
    std::cout << "Kotonoha.CommandLine: launcher and headless smoke passed\n";
    return 0;
  } catch (const std::exception &failure) {
    std::cerr << "Kotonoha.CommandLine: " << failure.what() << '\n';
    return 1;
  }
}
