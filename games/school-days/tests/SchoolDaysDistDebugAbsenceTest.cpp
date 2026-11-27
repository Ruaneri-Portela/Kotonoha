#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include "SchoolDaysRuntime.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void MustRejectDebugOption(const std::vector<std::string> &input) {
  Kotonoha::EngineOptions options;
  const Kotonoha::EngineOptions baseline;
  Kotonoha::CommandLine parser("School Days DIST", options);
  std::vector<std::string> args = {"school-days-test"};
  args.insert(args.end(), input.begin(), input.end());
  std::vector<char *> argv;
  for (auto &arg : args)
    argv.push_back(&arg[0]);
  bool rejected = false;
  try {
    parser.parse(static_cast<int>(argv.size()), argv.data());
  } catch (const std::exception &error) {
    rejected = std::string(error.what()).find("Unknown option") !=
               std::string::npos;
  }
  Require(rejected, "DIST unexpectedly recognized a developer-only option");
  Require(options.enableMCP == baseline.enableMCP &&
              options.mcpPort == baseline.mcpPort &&
              options.mcpFileRoot == baseline.mcpFileRoot &&
              options.logPath == baseline.logPath,
          "DIST debug option mutated engine configuration");
}

} // namespace

int main() {
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  // This is a compile-time build contract: a DIST must not own, construct,
  // register or open the generic "Dialogue history" scene at all, nor compile
  // the Android DEV-only extra MENU button and its touch hit area.
  static_assert(!SchoolDays::Runtime::GenericDialogueHistoryCompiled,
                "School Days DIST must compile out generic Dialogue history");
  static_assert(!SchoolDays::Runtime::AndroidExtraMenuButtonCompiled,
                "School Days DIST must compile out Android extra MENU button");
  try {
    Kotonoha::EngineOptions options;
    Kotonoha::CommandLine command("School Days DIST", options);
    const auto help = command.help();
    Require(help.find("--mcp") == std::string::npos &&
                help.find("--log") == std::string::npos,
            "DIST command-line help exposed developer MCP/log options");
    MustRejectDebugOption({"--mcp"});
    MustRejectDebugOption({"--mcp-port=49831"});
    MustRejectDebugOption({"--mcp-root", "synthetic-root"});
    MustRejectDebugOption({"--log", "synthetic-log"});
    std::cout << "SchoolDaysDistDebugAbsenceTest: PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysDistDebugAbsenceTest: " << error.what()
              << '\n';
    return 2;
  }
#else
  // The build system must never register this DIST-only negative test in DEV.
  return 2;
#endif
}
