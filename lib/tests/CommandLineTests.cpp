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

#if defined(KOTONOHA_DISTRIBUTION_BUILD) || !KOTONOHA_ENABLE_MCP
void expectMcpUnavailable() {
  Kotonoha::EngineOptions options;
  Kotonoha::CommandLine cli("PortSmoke", options);
  std::vector<std::string> args{"PortSmoke", "--mcp"};
  std::vector<char *> argv;
  for (auto &arg : args)
    argv.push_back(arg.data());

  bool rejectedAsUnknown = false;
  try {
    cli.parse(static_cast<int>(argv.size()), argv.data());
  } catch (const std::invalid_argument &failure) {
    rejectedAsUnknown =
        std::string(failure.what()).find("Unknown option: --mcp") !=
        std::string::npos;
  }
  check(rejectedAsUnknown, "Disabled MCP must be rejected as unknown");
  check(!options.enableMCP && options.launcher && cli.frames == -1,
        "Rejected MCP must not alter launcher or engine state");
  check(cli.help().find("--mcp") == std::string::npos,
        "Disabled MCP must not appear in command-line help");
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
#if !defined(KOTONOHA_DISTRIBUTION_BUILD) && KOTONOHA_ENABLE_MCP
    caseLauncher({"PortSmoke", "--mcp"}, false);
    caseLauncher({"PortSmoke", "--mcp", "--launcher"}, false);
#else
    expectMcpUnavailable();
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
