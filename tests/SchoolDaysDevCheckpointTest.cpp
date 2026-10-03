#include <Kotonoha/SchoolDaysRouter.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef KOTONOHA_DEV_CHECKPOINTS
#error "Compile this test with -DKOTONOHA_DEV_CHECKPOINTS=1"
#endif

using Kotonoha::SchoolDaysRouter;

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

int main() {
    SchoolDaysRouter router;

    Require(router.DevCheckpointScene("sd-ep3-r4-b00").value == "02/02-2K-B00",
        "checkpoint SceneKey");
    Require(router.RestoreDevCheckpoint("sd-ep3-r4-b00"),
        "checkpoint restore");

    const auto& state = router.State();
    Require(state.route == 4, "ROUTE=4");
    Require(state.scene == 1, "SCENE=1");
    Require(router.CurrentScene().value == "02/02-2K-B00",
        "current SceneKey");
    Require(state.choiceResult == -2, "choice is pending");
    Require(state.callback34 == 0, "callback34=0");
    Require(!state.feelingApplied, "feelingApplied=false");
    Require(state.endingRegistrations.empty(), "no ending registrations");

    Require(state.sessionVariables.at("001") == 10, "001=10");
    Require(state.sessionVariables.at("002") == 47, "002=47");
    Require(state.sessionVariables.at("998") == 1, "998=1");
    Require(state.sessionVariables.at("996") == 1, "996=1");
    Require(state.sessionVariables.at("994") == 1, "994=1");
    Require(state.sessionVariables.at("992") == 1, "992=1");
    Require(state.sessionVariables.at("991") == 1, "991=1");
    Require(state.sessionVariables.at("BS011KK07") == 31, "BS011KK07=31");

    const std::string dump = router.DumpState();
    Require(dump.find("scene=02/02-2K-B00") != std::string::npos,
        "DumpState scene");
    Require(dump.find("002=47") != std::string::npos,
        "DumpState feeling");

    const auto next = router.ResolveNext();
    Require(next.kind == SchoolDaysRouter::NextKind::Advanced,
        "checkpoint can advance");
    Require(next.transitionId == 263,
        "expected next transition is t263");

    std::cout << "School Days DEV checkpoint PASS\n";
    std::cout << "restored=02/02-2K-B00 route=4 scene=1\n";
    std::cout << "next=t" << next.transitionId
              << " -> " << next.destination.value << "\n";
    return 0;
}
