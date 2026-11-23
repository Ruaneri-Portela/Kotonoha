// Dependency-free C++17 regression tests for the LucaSystem Debug UI catalog.
#include <LucaSystem/DebugCatalog.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using LucaSystem::ScriptInstruction;
void require(bool ok, const char *label) {
  if (!ok)
    throw std::runtime_error(std::string("Luca debug catalog: ") + label);
}
ScriptInstruction instruction(std::uint8_t opcode,
                              std::vector<std::uint8_t> payload = {},
                              std::string text = {}) {
  ScriptInstruction value;
  value.opcode = opcode;
  value.paramBytes = std::move(payload);
  value.text = std::move(text);
  return value;
}
bool has(const std::vector<std::string> &items, const std::string &id) {
  return std::find(items.begin(), items.end(), id) != items.end();
}
} // namespace

int main() {
  try {
    std::vector<ScriptInstruction> script{
        instruction(36, {73, 0}, "@Misuzu@Hello"), // voice:73
        instruction(55, {2, 1, 103, 4}),           // image:1127
        instruction(107, {0xa1, 0x86, 0x01, 0}),   // bgm:100001
        instruction(113, {0, 42, 0, 0, 0}),        // se:42
        instruction(46, {74, 0}),                  // voice:74
        instruction(138, {104, 4}),                // image:1128
        instruction(119, {}, "AIR_OP_A"),          // loose video
        instruction(40),                           // choice
        instruction(99),                           // irrelevant
    };
    for (std::size_t i = 0; i < script.size(); ++i)
      script[i].pos = 100 + i * 8;

    const auto nodes = LucaSystem::buildDebugInstructions("seen170", script, 8);
    require(nodes.size() == script.size(), "all events plus current PC");
    require(nodes.front().pc == 0 && has(nodes[0].assets, "seen170") &&
                has(nodes[0].assets, "voice:73") &&
                nodes[0].label.find("MESSAGE") != std::string::npos,
            "message voice and script ID");
    require(has(nodes[1].assets, "image:1127"), "IMAGELOAD resource ID");
    require(has(nodes[2].assets, "bgm:100001"), "BGM resource ID");
    require(has(nodes[3].assets, "se:42"), "SE resource ID");
    require(has(nodes[4].assets, "voice:74"), "standalone voice");
    require(has(nodes[5].assets, "image:1128"), "background resource");
    require(has(nodes[6].assets, "movie/AIR_OP_A.webm") &&
                has(nodes[6].assets, "AIR_OP_A.wmv"),
            "movie asset candidates match the VM");
    require(nodes.back().pc == 8 &&
                nodes.back().label.find("CURRENT") != std::string::npos,
            "current instruction always remains visible");

    const auto selective =
        LucaSystem::buildDebugInstructions("seen170", script, 1);
    require(selective.size() == script.size() - 1 && selective.back().pc == 7,
            "irrelevant opcodes are not indexed off current position");

    const auto after =
        LucaSystem::buildDebugInstructions("seen170", script, script.size());
    require(after.back().pc == script.size() &&
                after.back().label.find("End of scenario") != std::string::npos,
            "end-of-script debug position remains visible");

    const auto empty = LucaSystem::buildDebugInstructions("seen170", {}, 0);
    require(empty.empty(), "empty script is harmless");

    std::cout << "LucaSystem debug catalog passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
