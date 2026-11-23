// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisVm.hpp>
#include <Artemis/AstParser.hpp>

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

void testIptParser() {
  const std::string iptSrc = R"(
ipt = {
  mode = "anime_full",
  --base = { x=0, y=0, w=1600, h=900, ax=800, ay=450, time=62.5, max=16, loop=-1 },
  base = { x=0, y=0, w=1600, h=900, ax=800, ay=450, time=33.3, max=6, loop=0 },
  {"em_face/em_face_00000"},
  {"em_face/em_face_00001"},
}
)";
  const auto spec = Artemis::AstParser::parseIpt(iptSrc);
  assert(spec.mode == "anime_full");
  assert(spec.w == 1600 && spec.h == 900);
  assert(spec.frames.size() == 2);
  assert(spec.frames[0] == "em_face/em_face_00000");
  assert(spec.frames[1] == "em_face/em_face_00001");
}

void testVmFlowChoicesAndSaveLoad() {
  std::unordered_map<std::string, std::string> scripts;
  scripts["gamestart"] = R"(
astver = 2.0
ast = {
  block_00000 = {
    {"bg", file="black", path=":bg/"},
    {"eval", exp="g.flag=1"},
    {"excall", file="01_c_00", cond="g.flag==1"},
    {"text"},
  },
  label = {
    top = { block="block_00000", label=1 },
  },
}
)";

  scripts["01_c_00"] = R"(
astver = 2.0
ast = {
  ["0001_00001"] = {
    {"savetitle", text="Nukitashi"},
    {"bg", id=1, file="bg20_1_00000", path=":bg/"},
    {"fg", file="jun_01f_00002", file1="jun_01f_00113", path=":fg/jun_01f/", id=30, ch="淳之介", lv=5, mode=1, x=-5, y=-5},
    {"bgm", file="bgm01", vol=80},
    {"se", id=1, loop=1, file="se_d016", vol=50},
    {"vo", file="jun0001", path=":vo/", ch="jun"},
    {"text"},
    text = {
      ja = {
        {
          name = {"淳之介"},
          "*テキスト*",
          {"rt2"},
        },
      },
      en = {
        {
          name = {"淳之介", "Junnosuke"},
          "Hello from Artemis!",
          {"rt2"},
        },
      },
    },
    linknext = "0001_00002",
  },
  ["0001_00002"] = {
    {"select"},
    select = {
      en = {
        { text="Go left", block="branch_a", exp="f.choice=1" },
        { text="Go right", block="branch_b", exp="f.choice=2" },
      },
    },
  },
  branch_a = {
    {"text"},
    text = {
      en = { { "Chose left.", {"rt2"} } },
    },
  },
  branch_b = {
    {"text"},
    text = {
      en = { { "Chose right.", {"rt2"} } },
    },
  },
}
)";

  Artemis::ArtemisVm vm;
  vm.setScriptLoader([&](const std::string &id) -> std::string {
    auto it = scripts.find(id);
    return it != scripts.end() ? it->second : std::string{};
  });
  // Even when preferred language is "ja", "*テキスト*" placeholder must fall
  // back to "en".
  vm.setPreferredLanguage("ja");
  assert(vm.loadScript("gamestart"));

  const auto reason1 = vm.step();
  assert(reason1 == Artemis::VmWaitReason::Dialogue);
  assert(vm.currentScript() == "01_c_00");
  assert(vm.saveTitle() == "Nukitashi");
  assert(vm.bg().file == "bg20_1_00000");
  assert(vm.sprites().size() == 1);
  assert(vm.sprites()[0].file == "jun_01f_00002");
  assert(vm.sprites()[0].file1 == "jun_01f_00113");
  assert(vm.bgm().playing && vm.bgm().file == "bgm01");
  assert(vm.dialogue().speaker == "Junnosuke");
  assert(vm.dialogue().text == "Hello from Artemis!");

  const auto seEvents = vm.consumeSeEvents();
  assert(seEvents.size() == 1 && seEvents[0].file == "se_d016");
  const auto voEvents = vm.consumeVoiceEvents();
  assert(voEvents.size() == 1 && voEvents[0].file == "jun0001");

  // Save & restore round-trip.
  const std::string saved = vm.serializeState();
  Artemis::ArtemisVm restored;
  restored.setScriptLoader([&](const std::string &id) -> std::string {
    auto it = scripts.find(id);
    return it != scripts.end() ? it->second : std::string{};
  });
  assert(restored.deserializeState(saved));
  assert(restored.currentScript() == "01_c_00");
  assert(restored.currentBlock() == "0001_00001");
  assert(restored.getVar("g.flag") == 1.0);
  assert(restored.dialogue().text == "Hello from Artemis!");

  // Advance to choices.
  assert(restored.advance());
  assert(restored.waitReason() == Artemis::VmWaitReason::Choice);
  assert(restored.choices().size() == 2);
  assert(restored.selectChoice(1));
  assert(restored.waitReason() == Artemis::VmWaitReason::Dialogue);
  assert(restored.getVar("f.choice") == 2.0);
  assert(restored.dialogue().text == "Chose right.");
}

} // namespace

int main() {
  testIptParser();
  testVmFlowChoicesAndSaveLoad();
  std::cout << "[AstParserAndVmTest] All tests passed!\n";
  return 0;
}
