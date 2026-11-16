// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/SaveState.hpp>
#include <LucaSystem/DialogueFlow.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
template <typename F>
void rejects(F fn, const char *message) {
  bool caught = false;
  try {
    fn();
  } catch (const std::exception &) {
    caught = true;
  }
  require(caught, message);
}
} // namespace

int main() {
  try {
    // One input reveals pending glyphs; only a subsequent confirm advances
    // the VM. No input can pull arbitrary extracted dialogue out of order.
    using LucaSystem::DialogueAdvance;
    require(LucaSystem::nextDialogueAction(false, false) ==
                DialogueAdvance::None, "Hidden dialogue is not advanceable");
    require(LucaSystem::nextDialogueAction(true, false) ==
                DialogueAdvance::Reveal, "First confirm reveals text");
    require(LucaSystem::nextDialogueAction(true, true) ==
                DialogueAdvance::ResumeScript,
            "Second confirm resumes the script without skipping commands");

    LucaSystem::SaveState state;
    state.scenario = "seen163";
    state.instructionIndex = 1234;
    state.dialogueIndex = 12;
    state.dialogueVisible = true;
    state.dialogueText = "Hello \"world\"\n日本語";
    state.waiting = true;
    state.waitRemainingMs = 4321;
    state.numbers[3] = -2147483647;
    state.numbers[7] = 777;
    state.strings[1] = "A value with spaces, \t tabs and\nnewlines";
    state.strings[2] = std::string("x\0y", 3);
    state.strings[4] = "";
    state.flags[1] = false;
    state.flags[42] = true;
    state.callStack = {3, 17, 1023};
    auto encoded = LucaSystem::encodeSaveState(state);
    const auto decoded = LucaSystem::decodeSaveState(encoded);
    require(decoded.scenario == state.scenario &&
                decoded.instructionIndex == state.instructionIndex &&
                decoded.dialogueIndex == state.dialogueIndex &&
                decoded.dialogueVisible == state.dialogueVisible &&
                decoded.dialogueText == state.dialogueText &&
                decoded.waiting == state.waiting &&
                decoded.waitRemainingMs == state.waitRemainingMs &&
                decoded.numbers == state.numbers &&
                decoded.strings == state.strings &&
                decoded.flags == state.flags &&
                decoded.callStack == state.callStack,
            "LucaSystem V3 round-trip must preserve all VM fields");
    require(LucaSystem::encodeSaveState(decoded) == encoded,
            "LucaSystem V3 saves must be stable on a repeat encode");

    rejects([&] { LucaSystem::decodeSaveState(encoded.substr(0, encoded.size() - 1)); },
            "Truncated V3 payload must be rejected");
    rejects([&] { LucaSystem::decodeSaveState(encoded + "junk"); },
            "Trailing V3 data must be rejected");
    auto oversized = encoded;
    const auto offset = LucaSystem::SaveCodec::Magic.size();
    oversized.replace(offset, 4, 4, '\xff');
    rejects([&] { LucaSystem::decodeSaveState(oversized); },
            "Huge V3 string length must be rejected");
    auto invalidBool = encoded;
    const auto firstBoolean = offset + 4 + state.scenario.size() + 8 + 8;
    invalidBool.replace(firstBoolean, 4, 4, '\xff');
    rejects([&] { LucaSystem::decodeSaveState(invalidBool); },
            "Invalid boolean must be rejected");
    rejects([&] { LucaSystem::decodeSaveState("LUCA_SYSTEM_SAVE_99\n"); },
            "Unknown save version must be rejected");
    rejects([&] { LucaSystem::encodeSaveState({}); },
            "Save without an active scenario must be rejected");

    // Old whitespace-separated V2 files remain readable if their original
    // string fields contain no whitespace (V2 could not preserve it).
    const std::string v2 =
        "LUCA_SYSTEM_SAVE_2\nseen23\n10\n1\n1\n"
        "1\n5 -22\n"
        "1\n7 String\n"
        "1\n9 1\n";
    const auto legacy = LucaSystem::decodeSaveState(v2);
    require(legacy.scenario == "seen23" &&
                legacy.instructionIndex == 10 &&
                legacy.dialogueIndex == 1 && legacy.dialogueVisible &&
                legacy.numbers.at(5) == -22 &&
                legacy.strings.at(7) == "String" &&
                legacy.flags.at(9) && legacy.callStack.empty(),
            "Legacy V2 saved state is readable");
    const auto oldest = LucaSystem::decodeSaveState("seen99\n");
    require(oldest.scenario == "seen99" && oldest.instructionIndex == 0,
            "Legacy scenario-only save is readable");
    rejects([&] {
      LucaSystem::decodeSaveState(
          "LUCA_SYSTEM_SAVE_2\nseen23\n0\n0\n0\n65537\n");
    }, "Invalid V2 collection bound must be rejected");

    std::cout << "LucaSystem save codec regression tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LucaSystem save codec regression failed: "
              << error.what() << '\n';
    return 1;
  }
}
