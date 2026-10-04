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

    Require(router.DevCheckpointScene("sd-ep4-r15-e08-routing-only").value ==
            "03/03-KB-E08",
        "routing-only checkpoint SceneKey");
    Require(router.RestoreDevCheckpoint("sd-ep4-r15-e08-routing-only"),
        "routing-only checkpoint restore");

    const auto& route15State = router.State();
    Require(route15State.route == 15, "Route15 checkpoint ROUTE=15");
    Require(route15State.scene == 39, "Route15 checkpoint SCENE=39");
    Require(route15State.sessionVariables.at("001") == 10,
        "Route15 checkpoint 001=10");
    Require(route15State.sessionVariables.at("002") == 84,
        "Route15 checkpoint 002=84");
    Require(router.CurrentScene().value == "03/03-KB-E08",
        "Route15 checkpoint current SceneKey");

    const auto intoDispatcher = router.ResolveNext();
    Require(intoDispatcher.kind == SchoolDaysRouter::NextKind::Advanced,
        "E08 advances into routing-only dispatcher");
    Require(intoDispatcher.transitionId == 917,
        "E08 expected transition is t917");
    Require(intoDispatcher.destination.value == "03/03-KB-E00",
        "t917 destination is routing-only E00");
    Require(router.IsCurrentSceneRoutingOnly(),
        "E00 recognized as routing-only");

    const auto outOfDispatcher = router.ResolveNext();
    Require(outOfDispatcher.kind == SchoolDaysRouter::NextKind::Advanced,
        "routing-only E00 resolves immediately");
    Require(outOfDispatcher.transitionId == 907,
        "001<=002 selects t907");
    Require(outOfDispatcher.destination.value == "03/03-KB-G00",
        "t907 destination is physical G00");
    Require(!router.IsCurrentSceneRoutingOnly(),
        "G00 is not routing-only");

    std::cout << "School Days DEV checkpoint PASS\n";
    std::cout << "restored=02/02-2K-B00 route=4 scene=1\n";
    std::cout << "next=t" << next.transitionId
              << " -> " << next.destination.value << "\n";
    std::cout << "routing-only=t" << intoDispatcher.transitionId
              << " -> " << intoDispatcher.destination.value
              << " -> t" << outOfDispatcher.transitionId
              << " -> " << outOfDispatcher.destination.value << "\n";
    return 0;
}
