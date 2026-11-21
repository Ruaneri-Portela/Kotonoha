#include <LucaSystem/DebugCatalog.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace LucaSystem {
namespace {
std::uint32_t u16(const ScriptInstruction &ins, std::size_t off) {
  if (off + 2 > ins.paramBytes.size())
    return 0;
  return std::uint32_t(ins.paramBytes[off]) |
         (std::uint32_t(ins.paramBytes[off + 1]) << 8);
}
std::uint32_t u32(const ScriptInstruction &ins, std::size_t off) {
  if (off + 4 > ins.paramBytes.size())
    return 0;
  return u16(ins, off) | (u16(ins, off + 2) << 16);
}
void add(std::vector<std::string> &assets, std::string id) {
  if (!id.empty() &&
      std::find(assets.begin(), assets.end(), id) == assets.end())
    assets.push_back(std::move(id));
}
const char *kind(std::uint8_t opcode) {
  switch (opcode) {
  case 21:
    return "JUMP";
  case 22:
    return "FARCALL";
  case 24:
    return "JUMPPOINT";
  case 25:
    return "END";
  case 36:
    return "MESSAGE";
  case 40:
    return "SELECT";
  case 43:
    return "LOG_BEGIN";
  case 46:
    return "VOICE";
  case 55:
    return "IMAGELOAD";
  case 74:
    return "DRAW";
  case 75:
    return "WIPE";
  case 107:
    return "BGM";
  case 113:
    return "SE";
  case 119:
    return "MOVIE";
  case 138:
    return "HAIKEI_SET";
  default:
    return nullptr;
  }
}
void referencedAssets(const ScriptInstruction &ins,
                      std::vector<std::string> &assets) {
  std::uint32_t id = 0;
  switch (ins.opcode) {
  case 36: // MESSAGE: uint16 voice ID at 0
  case 43: // LOG_BEGIN: uint16 voice ID at 0
  case 46: // VOICE: uint16 voice ID at 0
    if (ins.paramBytes.size() >= 2)
      id = u16(ins, 0);
    if (id)
      add(assets, "voice:" + std::to_string(id));
    break;
  case 55: // IMAGELOAD: [group, slot, id] or legacy [id, x, y]
    if (ins.paramBytes.size() == 4 || ins.paramBytes.size() >= 14)
      id = u16(ins, 2);
    else if (ins.paramBytes.size() >= 6)
      id = u16(ins, 0);
    if (id)
      add(assets, "image:" + std::to_string(id));
    break;
  case 138: // HAIKEI_SET
    if (ins.paramBytes.size() >= 2)
      id = u16(ins, 0);
    if (id)
      add(assets, "image:" + std::to_string(id));
    break;
  case 107: // BGM
    if (ins.paramBytes.size() >= 4)
      id = u32(ins, 0);
    else if (ins.paramBytes.size() >= 2)
      id = u16(ins, 0);
    if (id)
      add(assets, "bgm:" + std::to_string(id));
    break;
  case 113: // SE
    if (ins.paramBytes.size() >= 5)
      id = u32(ins, 1);
    else if (ins.paramBytes.size() >= 2)
      id = u16(ins, 0);
    if (id)
      add(assets, "se:" + std::to_string(id));
    break;
  case 119: { // MOVIE: source name may be a loose file, not an archive entry.
    if (!ins.text.empty())
      add(assets, ins.text);
    const auto name = ins.text.empty() ? "AIR_OP_A" : ins.text;
    for (const auto &candidate :
         {"movie/" + name + "_EN.webm", "movie/" + name + ".webm",
          name + "_EN.webm", name + ".webm", "movie/" + name + ".mp4",
          "movie/" + name + ".ogv", "movie/" + name + ".wmv", name + ".mp4",
          name + ".ogv", name + ".wmv"})
      add(assets, candidate);
    break;
  }
  default:
    break;
  }
}
} // namespace

std::vector<DebugInstruction>
buildDebugInstructions(const std::string &script,
                       const std::vector<ScriptInstruction> &instructions,
                       std::size_t currentPc) {
  std::vector<DebugInstruction> result;
  for (std::size_t pc = 0; pc < instructions.size(); ++pc) {
    const auto &ins = instructions[pc];
    const char *type = kind(ins.opcode);
    if (!type && pc != currentPc)
      continue;
    DebugInstruction item;
    item.pc = pc;
    std::ostringstream label;
    label << (type ? type : "CURRENT") << " | PC " << pc << " | @0x" << std::hex
          << ins.pos;
    if (!ins.text.empty()) {
      auto summary = ins.text.substr(0, 70);
      std::replace(summary.begin(), summary.end(), '\n', ' ');
      std::replace(summary.begin(), summary.end(), '\r', ' ');
      label << " | " << summary;
    }
    item.label = label.str();
    add(item.assets, script);
    referencedAssets(ins, item.assets);
    result.push_back(std::move(item));
  }
  // An end-of-script PC is a valid debug position but cannot be executed.
  if (currentPc >= instructions.size() && !instructions.empty()) {
    DebugInstruction end;
    end.pc = instructions.size();
    end.label = "End of scenario | PC " + std::to_string(end.pc);
    end.assets = {script};
    result.push_back(std::move(end));
  }
  return result;
}
} // namespace LucaSystem
