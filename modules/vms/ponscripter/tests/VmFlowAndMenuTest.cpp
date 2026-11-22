// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterVm.hpp>
#include <Ponscripter/PixelEffects.hpp>
#include <Ponscripter/SaveSerializer.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool cond, const char *msg) {
  if (!cond)
    throw std::runtime_error(msg);
}

} // namespace

int main() {
  try {
    Ponscripter::PonscripterVm vm;
    const std::string script =
        "\xEF\xBB\xBF;mode800\n"
        "*define\n"
        "globalon\n"
        "numalias cgset_0,904\n"
        "numalias end1_clear,901\n"
        "effect 10,10,500\n"
        "rmenu \"^~~ Salvar ~~\",save,\"^~~ Carregar ~~\",load,\"^\",,\"^~~ "
        "Voltar ~~\",reset\n"
        "game\n"
        "*start\n"
        "setwindow3 60,70,34,20,20,20,0,4,24,1,1,\":a;data\\f05.png\",40,50\n"
        "mov %cgset_0,1\n"
        "textoff\n"
        "lsp 98,\":a/2,0,3;data\\select.png\",260,300\n"
        "lsp 97,\":a/2,0,3;data\\select.png\",260,350\n"
        "spbtn 98,1\n"
        "spbtn 97,2\n"
        "print 10,2000\n"
        "*select_01l\n"
        "btnwait %111\n"
        "if %111=-1 systemcall rmenu\n"
        "if %111=1 csp -1:print 2000:goto *parting_end\n"
        "if %111=2 csp -1:print 2000:goto *chapter_3\n"
        "goto *select_01l\n"
        "*parting_end\n"
        "^Final 1 alcançado sem texton explícito!\\\n"
        "mov %end1_clear,1\n"
        "reset\n"
        "*chapter_3\n"
        "^Capítulo 3\\\n"
        "end\n";

    vm.appendScriptFile("00.utf", script);
    check(vm.start(), "VM must start and run *define");
    check(vm.resolution().width == 800 && vm.resolution().height == 600,
          "Resolution must be 800x600");
    check(vm.rmenuEntries().size() == 4 && vm.rmenuEntries()[2].command.empty(),
          "rmenu must preserve empty spacer entry (\"^\",,)");

    // Step from *start up to `print 10,2000`, then advance past the 2000ms
    // effect
    vm.tick(16.0);
    check(vm.waitState() == Ponscripter::WaitState::WaitEffect,
          "VM must enter WaitEffect on print 10,2000");
    vm.tick(2500.0);
    check(vm.waitState() == Ponscripter::WaitState::WaitButton,
          "VM must wait at btnwait %111");
    check(!vm.isTextWindowVisible(), "textoff must hide text window");

    // Simulate misclick outside buttons (%111 = 0): must loop to *select_01l
    // WITHOUT losing spbtn 98,1 and spbtn 97,2!
    check(vm.selectButton(0), "Misclick (0) must be accepted");
    vm.tick(16.0);
    check(vm.waitState() == Ponscripter::WaitState::WaitButton,
          "After misclick 0, *select_01l must wait at btnwait again");
    check(vm.activeButtons().size() == 2,
          "Buttons 1 and 2 must remain registered across *select_01l loop");

    // Now click button 1 (`*parting_end`), which executes `print 2000` (must
    // clamp >255 to 0 instant!) and `^Final 1...` (must auto-show text window!)
    check(vm.selectButton(1), "Selecting button 1 must succeed");
    vm.tick(16.0);
    check(
        vm.waitState() == Ponscripter::WaitState::WaitClickPage,
        "VM must reach *parting_end page wait without crashing on print 2000");
    check(vm.isTextWindowVisible(),
          "^ text line after textoff must automatically re-enable text window");

    // Test right-click rmenu -> save systemcall transition & slot save
    // round-trip
    check(vm.rightClick(), "Right-click at page wait must open rmenu");
    check(vm.waitState() == Ponscripter::WaitState::WaitSystemCall &&
              vm.activeSystemCall() == "rmenu",
          "Must be in rmenu systemcall");
    vm.enterSystemCall("save");
    check(vm.waitState() == Ponscripter::WaitState::WaitSystemCall &&
              vm.activeSystemCall() == "save",
          "Must transition from rmenu to save systemcall");

    const auto slotData = vm.exportSlotSave();
    const std::string serializedSlot =
        Ponscripter::SaveSerializer::serializeSlot(slotData);
    Ponscripter::SlotSaveData loadedSlot;
    check(Ponscripter::SaveSerializer::deserializeSlot(serializedSlot,
                                                       loadedSlot),
          "Slot save round-trip must succeed");
    check(loadedSlot.currentLabel == "parting_end",
          "Saved label must be parting_end");
    check(loadedSlot.waitState == Ponscripter::WaitState::WaitClickPage,
          "Saved waitState during rmenu->save must preserve WaitClickPage");

    vm.finishSystemCall();
    check(vm.waitState() == Ponscripter::WaitState::WaitClickPage,
          "Finishing save systemcall must restore WaitClickPage");

    // Advance past `\` page wait -> sets `%end1_clear = 1` and executes `reset`
    vm.advance(); // reveal typewriter if active
    vm.advance(); // advance page
    vm.tick(16.0);
    check(
        vm.variables().getNum("end1_clear") == 1 &&
            vm.variables().getNum("cgset_0") == 1,
        "Global variables %end1_clear and %cgset_0 must persist across reset");

    // Now test importing the saved slot while in systemcall load
    vm.enterSystemCall("load");
    vm.importSlotSave(loadedSlot);
    vm.finishSystemCall();
    check(vm.currentLabel() == "parting_end" &&
              vm.waitState() == Ponscripter::WaitState::WaitClickPage,
          "Loading slot from load menu must restore parting_end and "
          "WaitClickPage");

    {
      Ponscripter::PonscripterVm sub;
      const std::string subScript =
          "*define\n"
          "defsub greet\n"
          "game\n"
          "*start\n"
          "gosub *math,2,\"Alpha\",5\n"
          "greet 7,\"Texto\"\n"
          "for %10=4 to 1\n"
          "mov %99,999\n"
          "for %12=1 to 2\n"
          "mov %99,888\n"
          "next\n"
          "next\n"
          "for %11=1 to 3 step -1\n"
          "mov %99,777\n"
          "next\n"
          "end\n"
          "*math ; inline comment\n"
          "getparam %1,$1\n"
          "wait 15\n"
          "getparam %2\n"
          "mov %20,%1+%2\n"
          "return\n"
          "*greet\n"
          "getparam %3,$3\n"
          "return\n";
      sub.appendScriptFile("0.utf", subScript);
      check(sub.start(), "Subroutine fixture must start");
      sub.tick(0.0);
      check(sub.waitState() == Ponscripter::WaitState::WaitTimer,
            "gosub must enter parameterized subroutine");
      check(sub.variables().getNum(1) == 2 &&
                sub.variables().getStr(1) == "Alpha",
            "getparam must resolve typed numeric and string parameters");
      const auto saved = sub.exportSlotSave();
      check(saved.callStack.size() == 1 &&
                saved.callStack[0].arguments.size() == 3 &&
                saved.callStack[0].nextArgument == 2,
            "Subroutine call frame must preserve remaining parameters");
      Ponscripter::SlotSaveData decoded;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(saved), decoded),
            "Serialized call frame with parameters must be readable");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", subScript);
      check(resumed.start(), "Resumed script must start");
      resumed.importSlotSave(decoded);
      resumed.tick(20.0);
      check(resumed.isHalted(), "Restored program must reach end");
      check(resumed.variables().getNum(2) == 5 &&
                resumed.variables().getNum(20) == 7 &&
                resumed.variables().getNum(3) == 7 &&
                resumed.variables().getStr(3) == "Texto",
            "getparam and defsub must resume correctly across save/load");
      check(resumed.variables().getNum(99) == 0 &&
                resumed.variables().getNum(10) == 4 &&
                resumed.variables().getNum(11) == 1,
            "Initially out-of-range loops must skip their nested bodies");
    }

    {
      Ponscripter::PonscripterVm textVm;
      textVm.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "mov %5,42\n"
          "mov $6,\"A@#~!\"\n"
          "^Score {%5}, {$6}, {|raw@\n"
          "end\n");
      check(textVm.start(), "Interpolation script must start");
      textVm.tick(0.0);
      check(textVm.waitState() == Ponscripter::WaitState::WaitClickInline,
            "Interpolated text must retain its click terminator");
      check(textVm.currentPagePlainText() ==
                "Score 42, A@#~!, {raw",
            "Interpolated values must remain literal text");
    }

    {
      Ponscripter::PonscripterVm compatibility;
      compatibility.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "len %1,\"Olá\"\n"
          "mid $2,\"Aé日Z\",1,2\n"
          "tablegoto 1,*wrong,*chosen,*wrong\n"
          "mov %9,900\n"
          "end\n"
          "*chosen\n"
          "mov %3,13\n"
          "tablegoto1 2,*wrong,*chosen2\n"
          "end\n"
          "*chosen2\n"
          "mov %4,22\n"
          "tablegoto 30,*wrong\n"
          "mov %5,55\n"
          "end\n"
          "*wrong\n"
          "mov %9,999\n"
          "end\n");
      check(compatibility.start(), "String/dispatch script must start");
      compatibility.tick(0.0);
      check(compatibility.isHalted(), "Dispatch script must terminate");
      check(compatibility.variables().getNum(1) == 3 &&
                compatibility.variables().getStr(2) == "é日",
            "len/mid must preserve UTF-8 character boundaries");
      check(compatibility.variables().getNum(3) == 13 &&
                compatibility.variables().getNum(4) == 22 &&
                compatibility.variables().getNum(5) == 55 &&
                compatibility.variables().getNum(9) == 0,
            "tablegoto indexing and unmatched fall-through must work");
    }

    {
      Ponscripter::PonscripterVm numeric;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "selnum %9,\"Zero\",\"Um\",\"Dois\"\n"
          "mov %10,100\n"
          "end\n";
      numeric.appendScriptFile("0.utf", script);
      check(numeric.start(), "selnum fixture must start");
      numeric.tick(0.0);
      check(numeric.waitState() == Ponscripter::WaitState::WaitSelect &&
                numeric.selectOptions().size() == 3,
            "selnum must expose all text-only choices");
      check(!numeric.selectChoice(-1) && !numeric.selectChoice(3),
            "out-of-range choices must be rejected without advancing");
      const auto snapshot = numeric.exportSlotSave();
      check(snapshot.selectionMode == 2 &&
                snapshot.selectionNumberVariable == 9,
            "slot must retain numeric choice destination");

      Ponscripter::SlotSaveData decoded;
      const std::string serialized =
          Ponscripter::SaveSerializer::serializeSlot(snapshot);
      check(Ponscripter::SaveSerializer::deserializeSlot(serialized, decoded),
            "numeric choice slot must deserialize");
      check(decoded.selectionMode == 2 &&
                decoded.selectionNumberVariable == 9,
            "numeric choice mode must survive save serialization");

      Ponscripter::PonscripterVm resumedChoice;
      resumedChoice.appendScriptFile("0.utf", script);
      check(resumedChoice.start(), "resume choice script must start");
      resumedChoice.importSlotSave(decoded);
      check(resumedChoice.waitState() == Ponscripter::WaitState::WaitSelect,
            "restored choice must still wait for input");
      check(resumedChoice.selectChoice(2), "numeric choice must be accepted");
      resumedChoice.tick(0.0);
      check(resumedChoice.isHalted() &&
                resumedChoice.variables().getNum(9) == 2 &&
                resumedChoice.variables().getNum(10) == 100,
            "selnum must write zero-based index then continue without jumping");

      // Existing V1 saves without the optional SELMODE record remain readable.
      std::string legacy = serialized;
      const std::size_t record = legacy.find("SELMODE\t");
      check(record != std::string::npos, "new saves must contain SELMODE");
      const std::size_t recordEnd = legacy.find('\n', record);
      legacy.erase(record, recordEnd - record + 1);
      Ponscripter::SlotSaveData oldSlot;
      check(Ponscripter::SaveSerializer::deserializeSlot(legacy, oldSlot) &&
                oldSlot.selectionMode == 0 &&
                oldSlot.selectionNumberVariable == -1,
            "old V1 slots must default to jump selection");
    }

    {
      Ponscripter::PonscripterVm choice;
      choice.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "mov %1,5\n"
          "selgosub \"Primeira\",*one,\"Segunda\",*two\n"
          "add %1,100\n"
          "end\n"
          "*one\n"
          "add %1,1\n"
          "return\n"
          "*two\n"
          "add %1,2\n"
          "return\n");
      check(choice.start(), "selgosub fixture must start");
      choice.tick(0.0);
      check(choice.waitState() == Ponscripter::WaitState::WaitSelect &&
                choice.selectOptions().size() == 2,
            "selgosub must wait for its text/label options");
      check(choice.selectChoice(1), "selgosub choice must be accepted");
      choice.tick(0.0);
      check(choice.isHalted() && choice.variables().getNum(1) == 107,
            "selgosub must return to the instruction after the choice");
    }

    {
      Ponscripter::PonscripterVm normal;
      normal.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "select \"Primeira\",*missing,\"Segunda\",*valid\n"
          "mov %1,999\n"
          "end\n"
          "*valid\n"
          "mov %1,2\n"
          "end\n");
      check(normal.start(), "select fixture must start");
      normal.tick(0.0);
      check(!normal.selectChoice(0) &&
                normal.waitState() == Ponscripter::WaitState::WaitSelect,
            "missing choice target must not corrupt wait state");
      check(normal.selectChoice(1), "valid choice target must work");
      normal.tick(0.0);
      check(normal.isHalted() && normal.variables().getNum(1) == 2,
            "normal select must jump rather than return to the same line");
    }

    {
      Ponscripter::PonscripterVm arrayVm;
      arrayVm.appendScriptFile(
          "0.utf",
          "*define\n"
          "dim ?5[3]\n"
          "game\n"
          "*start\n"
          "movl ?5[0],1,2,3,4\n"
          "movz ?5[1],9\n"
          "mov %0,?5[0]\n"
          "mov %1,?5[1]\n"
          "mov %2,?5[2]\n"
          "mov %3,?5[3]\n"
          "end\n");
      check(arrayVm.start(), "movz fixture must start");
      arrayVm.tick(0.0);
      check(arrayVm.isHalted(), "movz script must terminate");
      check(arrayVm.variables().getNum(0) == 1 &&
                arrayVm.variables().getNum(1) == 9 &&
                arrayVm.variables().getNum(2) == 0 &&
                arrayVm.variables().getNum(3) == 0,
            "movz must preserve preceding cells and clear remaining cells");
    }

    {
      Ponscripter::PonscripterVm multiline;
      multiline.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "selnum %10,\"Primeiro\",\n"
          "  \"Segundo\",\n"
          "  \"Terceiro\"\n"
          "mov %11,1\n"
          "end\n");
      check(multiline.start(), "multiline selnum fixture must start");
      multiline.tick(0);
      check(multiline.waitState() == Ponscripter::WaitState::WaitSelect &&
                multiline.selectOptions().size() == 3,
            "multiline selnum must combine all three source lines");
      check(multiline.selectChoice(2), "third multiline option must work");
      multiline.tick(0);
      check(multiline.isHalted() &&
                multiline.variables().getNum(10) == 2 &&
                multiline.variables().getNum(11) == 1,
            "selection must advance after the entire multiline statement");
    }

    {
      Ponscripter::PonscripterVm custom;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "csel \"Zero\",*zero,\n"
          "  \"Um\",*one\n"
          "mov %99,999\n"
          "end\n"
          "*customsel\n"
          "getcselnum %0\n"
          "getcselstr $1,1\n"
          "cselbtn 0,20,10,50\n"
          "cselbtn 1,21,100,50\n"
          "btnwait %2\n"
          "sub %2,20\n"
          "cselgoto %2\n"
          "end\n"
          "*zero\n"
          "mov %3,0\n"
          "end\n"
          "*one\n"
          "mov %3,7\n"
          "end\n";
      custom.appendScriptFile("0.utf", script);
      check(custom.start(), "customsel fixture must start");
      custom.tick(0);
      check(custom.waitState() == Ponscripter::WaitState::WaitButton,
            "csel must jump to customsel and wait on cselbtn buttons");
      check(custom.variables().getNum(0) == 2 &&
                custom.variables().getStr(1) == "Um",
            "getcselnum/getcselstr must see all multi-line custom choices");
      check(custom.activeButtons().size() == 2 &&
                custom.activeButtons()[1].kind ==
                    Ponscripter::ButtonDef::Kind::TextBtn &&
                custom.activeButtons()[1].text == "Um",
            "cselbtn must produce a UI-rendered custom text button");
      auto saved = custom.exportSlotSave();
      check(saved.cselOptions.size() == 2,
            "pending custom choices must be serializable");
      Ponscripter::SlotSaveData restored;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(saved), restored),
            "custom select save/load must deserialize");
      check(restored.cselOptions.size() == 2 &&
                restored.buttons.size() == 2 &&
                restored.buttons[1].text == "Um",
            "custom choice labels/buttons must survive a save roundtrip");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", script);
      check(resumed.start(), "customsel reload must start");
      resumed.importSlotSave(restored);
      resumed.updateMousePos(105, 55);
      check(resumed.hoveredButtonId() == 21,
            "custom selection hit-testing must use text button bounds");
      check(resumed.advance(), "custom button click must be accepted");
      resumed.tick(0);
      check(resumed.isHalted() &&
                resumed.variables().getNum(2) == 1 &&
                resumed.variables().getNum(3) == 7 &&
                resumed.variables().getNum(99) == 0,
            "custom button must branch through script-controlled cselgoto");
    }

    {
      Ponscripter::PonscripterVm refs;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "mov %5,91\n"
          "mov $6,\"Referência\"\n"
          "gosub *indirect,%5,$6\n"
          "end\n"
          "*indirect\n"
          "wait 10\n"
          "getparam i%1,s%2\n"
          "return\n";
      refs.appendScriptFile("0.utf", script);
      check(refs.start(), "indirect getparam fixture must start");
      refs.tick(0);
      check(refs.waitState() == Ponscripter::WaitState::WaitTimer,
            "indirect subroutine must reach wait before getparam");
      const auto save = refs.exportSlotSave();
      check(save.callStack.size() == 1 &&
                save.callStack[0].arguments.size() == 2 &&
                save.callStack[0].arguments[0].sourceVariable == 5 &&
                save.callStack[0].arguments[1].sourceVariable == 6,
            "gosub must retain source variable IDs, not just evaluated values");
      Ponscripter::SlotSaveData restored;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(save), restored),
            "indirect parameter save must deserialize");
      check(restored.callStack[0].arguments[0].sourceVariable == 5 &&
                restored.callStack[0].arguments[1].sourceIsString,
            "source-reference metadata must survive save/load");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", script);
      check(resumed.start(), "indirect getparam resume script must start");
      resumed.importSlotSave(restored);
      resumed.tick(20);
      check(resumed.isHalted() &&
                resumed.variables().getNum(1) == 5 &&
                resumed.variables().getNum(2) == 6,
            "getparam i/s must return original numeric/string variable IDs");
    }

    {
      Ponscripter::PonscripterVm hooked;
      const std::string script =
          "*define\n"
          "pretextgosub *before\n"
          "textgosub *after\n"
          "game\n"
          "*start\n"
          "^Primeiro@\n"
          "^Segundo\\\n"
          "end\n"
          "*before\n"
          "inc %1\n"
          "return\n"
          "*after\n"
          "inc %2\n"
          "texec\n"
          "return\n";
      hooked.appendScriptFile("0.utf", script);
      check(hooked.start(), "text hook fixture must start");
      hooked.tick(0);
      check(hooked.isHalted() && hooked.currentLabel() == "start" &&
                hooked.variables().getNum(1) == 2 &&
                hooked.variables().getNum(2) == 2,
            "pretext and click text hooks must be called exactly once per line");
      check(hooked.currentPagePlainText().empty() &&
                !hooked.backlogPages().empty(),
            "texec must clear the pending page after a page-click hook");
    }

    {
      Ponscripter::PonscripterVm interrupted;
      const std::string script =
          "*define\n"
          "pretextgosub *before\n"
          "textgosub *after\n"
          "game\n"
          "*start\n"
          "^Salvar\\\n"
          "end\n"
          "*before\n"
          "wait 10\n"
          "inc %1\n"
          "return\n"
          "*after\n"
          "inc %2\n"
          "texec\n"
          "return\n";
      interrupted.appendScriptFile("0.utf", script);
      check(interrupted.start(), "interrupted hook fixture must start");
      interrupted.tick(0);
      check(interrupted.waitState() == Ponscripter::WaitState::WaitTimer,
            "pretext callback must wait before rendering text");
      const auto snapshot = interrupted.exportSlotSave();
      check(snapshot.pendingPretextStatementIndex !=
                static_cast<std::size_t>(-1),
            "pending pretext execution must be represented in save state");
      Ponscripter::SlotSaveData decoded;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(snapshot), decoded),
            "pretext save must deserialize");
      Ponscripter::PonscripterVm restored;
      restored.appendScriptFile("0.utf", script);
      check(restored.start(), "restored text-hook script must start");
      restored.importSlotSave(decoded);
      restored.tick(20);
      check(restored.isHalted() && restored.currentLabel() == "start" &&
                restored.variables().getNum(1) == 1 &&
                restored.variables().getNum(2) == 1 &&
                restored.currentPagePlainText().empty(),
            "restored pretext hook must return and avoid recursive invocation");
    }

    {
      Ponscripter::PonscripterVm gfx;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "ld l,\"actor.png\"\n"
          "tal l,128\n"
          "lsp 1,\"first.png\",10,10\n"
          "lsp2 2,\"second.png\",30,30\n"
          "strsp 10,\"Ação\",5,7,10,1,18,22,1,0,0,1,#ff0000\n"
          "strsph 11,\"Escondido\",5,40\n"
          "allsphide\n"
          "wait 10\n"
          "allspresume\n"
          "allsp2hide\n"
          "end\n";
      gfx.appendScriptFile("0.utf", script);
      check(gfx.start(), "graphics commands fixture must start");
      gfx.tick(0);
      check(gfx.waitState() == Ponscripter::WaitState::WaitTimer,
            "graphics script must stop at timed visibility checkpoint");
      check(gfx.standingSlots()[0].alpha == 127 &&
                gfx.areSpritesHidden(false) &&
                !gfx.areSpritesHidden(true),
            "tal must change standing opacity and allsphide only main layer");
      check(gfx.sprites()[2].secondaryLayer &&
                !gfx.sprites()[1].secondaryLayer,
            "lsp and lsp2 must have independent layer classification");
      check(gfx.sprites()[10].spec.trans ==
                Ponscripter::TransMode::String &&
                gfx.sprites()[10].spec.text == "Ação" &&
                gfx.sprites()[10].spec.color == 0xFF0000u &&
                !gfx.sprites()[11].visible,
            "strsp/strsph must set GPU-text sprite caption and visibility");
      Ponscripter::SlotSaveData copy;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(
                    gfx.exportSlotSave()), copy),
            "graphics save roundtrip must deserialize");
      check(copy.allSpritesHidden && !copy.allSprites2Hidden &&
                copy.standing[0].alpha == 127 &&
                copy.sprites[2].secondaryLayer &&
                copy.sprites[10].spec.text == "Ação",
            "graphics snapshots must retain layered state, alpha and text");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", script);
      check(resumed.start(), "graphics resume script must start");
      resumed.importSlotSave(copy);
      resumed.tick(20);
      check(resumed.isHalted() &&
                !resumed.areSpritesHidden(false) &&
                resumed.areSpritesHidden(true),
            "allspresume/allsp2hide must resume only the selected layer");
    }

    {
      Ponscripter::PonscripterVm manual;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "bg \"bg.png\"\n"
          "lsp 4,\"sprite.png\",0,0\n"
          "drawclear\n"
          "drawbg\n"
          "drawsp 4,0,256,20,30\n"
          "drawsp2 4,0,128,60,70,125,80,45\n"
          "drawsp3 4,0,256,30,40,1000,200,-200,1000\n"
          "draw\n"
          "wait 10\n"
          "drawfill 12,34,56\n"
          "draw\n"
          "end\n";
      manual.appendScriptFile("0.utf", script);
      check(manual.start(), "manual framebuffer fixture must start");
      manual.tick(0);
      check(manual.waitState() == Ponscripter::WaitState::WaitTimer &&
                manual.hasManualDrawFrame() &&
                manual.manualDrawFrame().size() == 5,
            "draw must commit manual operations before the script waits");
      const auto &ops = manual.manualDrawFrame();
      check(ops[0].kind == Ponscripter::ManualDrawOp::Kind::Clear &&
                ops[1].kind == Ponscripter::ManualDrawOp::Kind::Background &&
                ops[2].kind == Ponscripter::ManualDrawOp::Kind::Sprite &&
                ops[2].x == 20 && ops[2].y == 30 &&
                ops[3].scaleX == 125 && ops[3].scaleY == 80 &&
                ops[3].rotation == 45 && ops[4].affine &&
                ops[4].transformB == 200,
            "manual background, drawsp, scaled and affine commands must be captured");
      Ponscripter::SlotSaveData slot;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(
                    manual.exportSlotSave()), slot),
            "manual GPU draw snapshot must roundtrip");
      check(slot.manualFrameVisible && slot.manualDisplayOps.size() == 5 &&
                slot.manualDisplayOps[2].spec.imagePath == "sprite.png" &&
                slot.manualDisplayOps[4].transformC == -200,
            "manual GPU draw state must survive save/load");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", script);
      check(resumed.start(), "manual framebuffer resumption must start");
      resumed.importSlotSave(slot);
      resumed.tick(20);
      check(resumed.isHalted() && resumed.hasManualDrawFrame() &&
                resumed.manualDrawFrame().size() == 1 &&
                resumed.manualDrawFrame()[0].colorRgb == 0x0C2238u,
            "drawfill must clear the accumulation and commit RGB color");
    }

    {
      Ponscripter::PonscripterVm shake;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "quakex 16,120\n"
          "mov %1,1\n"
          "quakey 12,100\n"
          "mov %2,2\n"
          "end\n";
      shake.appendScriptFile("0.utf", script);
      check(shake.start(), "quake fixture must start");
      shake.tick(0);
      check(shake.waitState() == Ponscripter::WaitState::WaitEffect &&
                shake.activeEffect().builtinEffect == 10002,
            "quakex must start horizontal timed effect");
      shake.tick(31);
      const auto offset = shake.quakeOffset();
      check(offset.first != 0 && offset.second == 0,
            "quakex frame offset must animate only X");
      Ponscripter::SlotSaveData state;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(
                    shake.exportSlotSave()), state),
            "active quake must be savable");
      check(state.activeEffect.active &&
                state.activeEffect.durationMs == 120 &&
                state.activeEffect.elapsedMs == 31 &&
                state.quakeAmplitude == 16,
            "quake state and frame progress must survive save roundtrip");
      Ponscripter::PonscripterVm restored;
      restored.appendScriptFile("0.utf", script);
      check(restored.start(), "quake resumption fixture must start");
      restored.importSlotSave(state);
      restored.tick(100);
      check(restored.waitState() == Ponscripter::WaitState::WaitEffect &&
                restored.activeEffect().builtinEffect == 10001 &&
                restored.variables().getNum(1) == 1,
            "quakex completion must advance to next quakey command");
      restored.tick(25);
      const auto vertical = restored.quakeOffset();
      check(vertical.first == 0 && vertical.second != 0,
            "quakey frame offset must animate only Y");
      restored.tick(110);
      check(restored.isHalted() && restored.variables().getNum(2) == 2 &&
                restored.quakeOffset() == std::pair<int,int>{0,0},
            "completed quake must restore centered scene");
    }

    {
      std::uint8_t pixels[12] = {10, 20, 30, 191, 200, 100, 50, 117,
                                 7, 8, 9, 10};
      Ponscripter::applyScenePixelEffects(pixels, 2, 1, 12, false,
                                           0xFFFFFFu, 1);
      check(pixels[0] == 245 && pixels[1] == 235 &&
                pixels[2] == 225 && pixels[3] == 191 &&
                pixels[4] == 55 && pixels[5] == 155 &&
                pixels[6] == 205 && pixels[7] == 117 &&
                pixels[8] == 7 && pixels[9] == 8,
            "negative mode must invert only RGB and preserve alpha/padding");

      std::uint8_t mono[4] = {10, 20, 30, 127};
      Ponscripter::applyScenePixelEffects(mono, 1, 1, 4, true,
                                           0xFF0000u, 0);
      check(mono[0] == 18 && mono[1] == 0 &&
                mono[2] == 0 && mono[3] == 127,
            "monochrome tint must compute luma and preserve transparency");
      Ponscripter::applyScenePixelEffects(mono, 1, 1, 4, false,
                                           0xFFFFFFu, 2);
      check(mono[0] == 237 && mono[1] == 255 &&
                mono[2] == 255 && mono[3] == 127,
            "negative mode 2 must invert the fully composited tint");
    }

    {
      Ponscripter::PonscripterVm postfx;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "monocro #ffcc00\n"
          "nega 2\n"
          "wait 10\n"
          "monocro off\n"
          "nega 0\n"
          "end\n";
      postfx.appendScriptFile("0.utf", script);
      check(postfx.start(), "postprocess fixture must start");
      postfx.tick(0);
      check(postfx.waitState() == Ponscripter::WaitState::WaitTimer &&
                postfx.isMonochromeEnabled() &&
                postfx.monochromeColor() == 0xFFCC00u &&
                postfx.negativeMode() == 2,
            "monocro and nega must configure active rendering filters");
      Ponscripter::SlotSaveData slot;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(
                    postfx.exportSlotSave()), slot),
            "postprocess state must roundtrip");
      check(slot.monochromeEnabled &&
                slot.monochromeColor == 0xFFCC00u &&
                slot.negativeMode == 2,
            "monochrome/negative state must survive save");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", script);
      check(resumed.start(), "postprocess restore fixture must start");
      resumed.importSlotSave(slot);
      resumed.tick(20);
      check(resumed.isHalted() && !resumed.isMonochromeEnabled() &&
                resumed.negativeMode() == 0,
            "postprocessing should be disabled by script commands");
    }

    {
      Ponscripter::PonscripterVm copied;
      const std::string script =
          "*define\n"
          "game\n"
          "*start\n"
          "bg black\n"
          "lsp 12,\"sprite.png\",35,45\n"
          "bgcopy\n"
          "csp 12\n"
          "bgcpy\n"
          "wait 10\n"
          "bg white\n"
          "end\n";
      copied.appendScriptFile("0.utf", script);
      check(copied.start(), "background copy fixture must start");
      copied.tick(0);
      check(copied.waitState() == Ponscripter::WaitState::WaitTimer &&
                copied.backgroundCopyOps().size() == 2 &&
                copied.backgroundCopyOps()[1].kind ==
                    Ponscripter::ManualDrawOp::Kind::Sprite &&
                copied.backgroundCopyOps()[1].spec.imagePath == "sprite.png",
            "bgcopy/bgcpy must retain prior sprite appearance after csp");
      Ponscripter::SlotSaveData saved;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(
                    copied.exportSlotSave()), saved),
            "composited background should serialize");
      check(saved.backgroundCopyOps.size() == 2 &&
                saved.backgroundCopyOps[1].x == 35,
            "background copy image and geometry must survive save");
      Ponscripter::PonscripterVm resumed;
      resumed.appendScriptFile("0.utf", script);
      check(resumed.start(), "background copy resume fixture must start");
      resumed.importSlotSave(saved);
      resumed.tick(20);
      check(resumed.isHalted() && resumed.backgroundCopyOps().empty(),
            "loading a new background must invalidate the old capture");
    }

    {
      Ponscripter::PonscripterVm exbtn;
      exbtn.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "lsp 6,\":a/3,0,3;icon.png\",0,0\n"
          "lsp 7,\"second.png\",0,0\n"
          "spstr \"P6,2M6,80,-10S4,(audio/click.ogg)\"\n"
          "wait 10\n"
          "spstr \"C6-7\"\n"
          "end\n");
      check(exbtn.start(), "spstr fixture must start");
      exbtn.tick(0);
      check(exbtn.waitState() == Ponscripter::WaitState::WaitTimer &&
                exbtn.sprites()[6].currentCell == 2 &&
                exbtn.sprites()[6].x == 80 &&
                exbtn.sprites()[6].y == -10 &&
                exbtn.sprites()[6].visible &&
                exbtn.lastSePath() == "audio/click.ogg",
            "spstr must set the sprite cell, position and sample path");
      exbtn.tick(20);
      check(exbtn.isHalted() && !exbtn.sprites()[6].visible &&
                !exbtn.sprites()[7].visible,
            "spstr Cn-m must hide each sprite in the requested range");
    }

    {
      Ponscripter::PonscripterVm pretext;
      pretext.appendScriptFile(
          "0.utf",
          "*define\n"
          "pretextgosub *before\n"
          "game\n"
          "*start\n"
          "^Main@\n"
          "end\n"
          "*before\n"
          "^Text inside pretext hook\n"
          "inc %1\n"
          "return\n");
      check(pretext.start(), "nested pretext guard fixture must start");
      pretext.tick(0);
      check(pretext.waitState() == Ponscripter::WaitState::WaitClickInline &&
                pretext.variables().getNum(1) == 1,
            "pretext text must not recursively re-invoke pretextgosub");
    }

    {
      Ponscripter::PonscripterVm textHook;
      textHook.appendScriptFile(
          "0.utf",
          "*define\n"
          "textgosub *after\n"
          "game\n"
          "*start\n"
          "^Main@\n"
          "end\n"
          "*after\n"
          "^Click within textgosub@\n"
          "inc %1\n"
          "return\n");
      check(textHook.start(), "nested text hook fixture must start");
      textHook.tick(0);
      check(textHook.waitState() == Ponscripter::WaitState::WaitClickInline,
            "nested text hook must use normal wait without recursive gosub");
      for (int i = 0; i < 5 && !textHook.isHalted(); ++i) {
        if (textHook.waitState() ==
            Ponscripter::WaitState::WaitClickInline)
          textHook.advance();
        textHook.tick(0);
      }
      check(textHook.isHalted() && textHook.variables().getNum(1) == 1,
            "nested hook should return to main script without recursion");
    }

    {
      Ponscripter::PonscripterVm hud;
      hud.appendScriptFile(
          "0.utf",
          "*define\n"
          "game\n"
          "*start\n"
          "bar 1,50,10,20,200,12,100,#ff0000\n"
          "prnum 1,123,30,40,20,22,#ffffff\n"
          "bgcopy\n"
          "wait 10\n"
          "end\n");
      check(hud.start(), "HUD background capture fixture must start");
      hud.tick(0);
      const auto &captured = hud.backgroundCopyOps();
      check(captured.size() == 3 &&
                captured[1].kind == Ponscripter::ManualDrawOp::Kind::Rect &&
                captured[1].width == 100 &&
                captured[1].colorRgb == 0xFF0000u &&
                captured[2].kind == Ponscripter::ManualDrawOp::Kind::Text &&
                captured[2].text == "123",
            "bgcopy must freeze bars and GPU-rendered number labels");
      Ponscripter::SlotSaveData data;
      check(Ponscripter::SaveSerializer::deserializeSlot(
                Ponscripter::SaveSerializer::serializeSlot(
                    hud.exportSlotSave()), data) &&
                data.backgroundCopyOps.size() == 3 &&
                data.backgroundCopyOps[1].width == 100,
            "background HUD snapshots must survive saved slot serialization");
    }

    std::cout << "Ponscripter.VmFlowAndMenu passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Ponscripter.VmFlowAndMenu failed: " << e.what() << '\n';
    return 1;
  }
}
