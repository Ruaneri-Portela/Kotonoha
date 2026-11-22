// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/PonscripterVm.hpp>
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

    std::cout << "Ponscripter.VmFlowAndMenu passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Ponscripter.VmFlowAndMenu failed: " << e.what() << '\n';
    return 1;
  }
}
