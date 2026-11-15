#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace Kotonoha;

void check(bool condition, const char *reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

void parse(CommandLine &command, const std::vector<std::string> &arguments) {
  std::vector<std::string> values = arguments;
  std::vector<char *> argv;
  argv.reserve(values.size());
  for (auto &value : values)
    argv.push_back(value.data());
  command.parse(static_cast<int>(argv.size()), argv.data());
}

void checkPortRejected(const std::string &port) {
  EngineOptions options;
  CommandLine command("engine", options);
  bool rejected = false;
  try {
    parse(command, {"engine", "--mcp-port", port});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && !options.enableMCP && options.mcpPort == 0,
        "Invalid MCP port must be rejected without enabling the server");
}
} // namespace

int main() {
  try {
    EngineOptions stdio;
    CommandLine stdioCommand("engine", stdio);
    parse(stdioCommand, {"engine", "--mcp", "--mcp-root", "."});
    check(stdio.enableMCP && stdio.mcpPort == 0,
          "--mcp alone must keep the existing stdio transport");

    EngineOptions socket;
    CommandLine socketCommand("engine", socket);
    parse(socketCommand, {"engine", "--mcp-port=49831"});
    check(socket.enableMCP && socket.mcpPort == 49831,
          "--mcp-port must select the TCP transport and enable MCP");
    check(socketCommand.help().find("--mcp-port PORT") != std::string::npos,
          "MCP TCP port must appear in command-line help");

    EngineOptions maxPort;
    CommandLine maxCommand("engine", maxPort);
    parse(maxCommand, {"engine", "--mcp-port", "65535"});
    check(maxPort.enableMCP && maxPort.mcpPort == 65535,
          "Highest valid TCP port must be accepted");

    for (const auto &value : {"0", "-1", "65536", "1junk", "1.5", "+5",
                              "999999999999999999999999"})
      checkPortRejected(value);
    return 0;
  } catch (const std::exception &) {
    return 1;
  }
}
