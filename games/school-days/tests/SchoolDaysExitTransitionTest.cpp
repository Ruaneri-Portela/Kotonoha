#include <Kotonoha/routing/SchoolDaysExitTransition.hpp>

#include <iostream>
#include <stdexcept>

namespace {
using Transition = Kotonoha::SchoolDaysExitTransition;

void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
} // namespace

int main() {
  try {
    Transition exit;
    exit.Begin(Transition::Target::Quit, 1000);
    require(exit.InputLocked() && exit.RevealedFraction(1250) == 0.5f,
            "SYSBASE opening wipe");
    require(exit.Update(1499) == Transition::Event::None &&
                exit.Update(1500) == Transition::Event::OpenDialog &&
                !exit.InputLocked(),
            "Quit dialog opens after 500 ms");
    exit.Cancel(1600);
    require(exit.InputLocked() && exit.Alpha(1850) == 128 &&
                exit.Update(2100) == Transition::Event::Closed &&
                !exit.Active(),
            "Cancel fades SYSBASE for 500 ms");

    exit.Begin(Transition::Target::Quit, 3000);
    require(exit.Update(3500) == Transition::Event::OpenDialog,
            "Quit dialog reopens");
    exit.Confirm(3600);
    require(exit.ConfirmMs() == 750 &&
                exit.Update(4349) == Transition::Event::None &&
                exit.Update(4350) == Transition::Event::Confirmed,
            "Quit confirms after its 750 ms fade");

    exit.Begin(Transition::Target::Title, 5000);
    require(exit.Update(5500) == Transition::Event::OpenDialog &&
                exit.CurrentTarget() == Transition::Target::Title,
            "Main Menu uses the title confirmation");
    exit.Confirm(5600);
    require(exit.ConfirmMs() == 500 &&
                exit.Update(6099) == Transition::Event::None &&
                exit.Update(6100) == Transition::Event::Confirmed,
            "Main Menu confirms after its 500 ms fade");
    std::cout << "School Days retail exit transitions passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
