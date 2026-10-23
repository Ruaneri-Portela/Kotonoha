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

    // ---------------------------------------------------------------------
    // EP1 -> EP2 callback_38 handoff checkpoint.
    //
    // This is intentionally a targeted mechanism checkpoint, not a complete
    // historical EP1 snapshot. Route 0 / Scene 20 is the physical L00 scene
    // immediately before t25. t25 only requires callback34 == 0.
    // ---------------------------------------------------------------------

    Require(
        router.DevCheckpointScene("sd-ep1-r0-l00-handoff").value ==
            "00/00-00-L00",
        "EP1 handoff checkpoint SceneKey");

    Require(
        router.RestoreDevCheckpoint("sd-ep1-r0-l00-handoff"),
        "EP1 handoff checkpoint restore");

    {
        const auto& state = router.State();

        Require(state.route == 0, "EP1 handoff ROUTE=0");
        Require(state.scene == 20, "EP1 handoff SCENE=20");
        Require(state.callback34 == 0, "EP1 handoff callback34=0");
        Require(
            router.CurrentScene().value == "00/00-00-L00",
            "EP1 handoff current SceneKey");
    }

    const auto ep1Handoff = router.ResolveNext();

    Require(
        ep1Handoff.kind == SchoolDaysRouter::NextKind::Advanced,
        "EP1 handoff must advance");

    Require(
        ep1Handoff.transitionId == 25,
        "EP1 handoff expected transition t25");

    Require(
        ep1Handoff.destination.value == "01/01-00-A00",
        "t25 destination is EP2 A00");

    Require(
        ep1Handoff.HasCallback("callback_00"),
        "t25 preserves callback_00");

    Require(
        ep1Handoff.HasCallback("callback_2C"),
        "t25 preserves callback_2C");

    Require(
        ep1Handoff.HasCallback("callback_38"),
        "t25 preserves callback_38");

    {
        const auto& state = router.State();

        Require(state.route == 1, "post-t25 ROUTE=1");
        Require(state.scene == 0, "post-t25 SCENE=0");
        Require(
            state.sessionVariables.at("ROUTE") == 1,
            "post-t25 session ROUTE=1");
        Require(
            state.sessionVariables.at("SCENE") == 0,
            "post-t25 session SCENE=0");
    }

    // ---------------------------------------------------------------------
    // Ending 16 terminal handoff checkpoint.
    //
    // This is intentionally a targeted pre-terminal checkpoint rather than
    // a fabricated full historical save state. t2357 has no route predicate.
    // ---------------------------------------------------------------------

    Require(
        router.DevCheckpointScene("sd-ending16-r49-i02-handoff").value ==
            "05/05-SB-I02",
        "Ending16 checkpoint SceneKey");

    Require(
        router.RestoreDevCheckpoint("sd-ending16-r49-i02-handoff"),
        "Ending16 checkpoint restore");

    {
        const auto& state = router.State();

        Require(state.route == 49, "Ending16 checkpoint ROUTE=49");
        Require(state.scene == 28, "Ending16 checkpoint SCENE=28");
        Require(
            router.CurrentScene().value == "05/05-SB-I02",
            "Ending16 checkpoint current SceneKey");
        Require(
            state.endingRegistrations.empty(),
            "Ending16 checkpoint starts without registration");
    }

    const auto terminal = router.ResolveNext();

    Require(
        terminal.kind == SchoolDaysRouter::NextKind::Terminal,
        "Ending16 checkpoint must become Terminal");

    Require(
        terminal.transitionId == 2357,
        "Ending16 expected terminal transition t2357");

    Require(
        terminal.endingId == 16,
        "t2357 registers Ending 16");

    Require(
        terminal.HasCallback("callback_38"),
        "t2357 preserves callback_38");

    {
        const auto& state = router.State();

        Require(
            state.endingRegistrations.size() == 1,
            "Ending16 registration count=1");

        Require(
            state.endingRegistrations[0] == 16,
            "Ending16 registration value=16");

        Require(
            state.globalVariables.at("REP05_SB_I01") == 1,
            "t2357 writes REP05_SB_I01=1");
    }

    // ---------------------------------------------------------------------
    // Existing causal EP3 checkpoint.
    // ---------------------------------------------------------------------

    Require(
        router.DevCheckpointScene("sd-ep3-r4-b00").value ==
            "02/02-2K-B00",
        "EP3 checkpoint SceneKey");

    Require(
        router.RestoreDevCheckpoint("sd-ep3-r4-b00"),
        "EP3 checkpoint restore");

    {
        const auto& state = router.State();

        Require(state.route == 4, "EP3 ROUTE=4");
        Require(state.scene == 1, "EP3 SCENE=1");

        Require(
            router.CurrentScene().value == "02/02-2K-B00",
            "EP3 current SceneKey");

        Require(state.choiceResult == -2, "EP3 choice is pending");
        Require(state.callback34 == 0, "EP3 callback34=0");
        Require(!state.feelingApplied, "EP3 feelingApplied=false");
        Require(
            state.endingRegistrations.empty(),
            "EP3 no ending registrations");

        Require(state.sessionVariables.at("001") == 10, "EP3 001=10");
        Require(state.sessionVariables.at("002") == 47, "EP3 002=47");
        Require(state.sessionVariables.at("998") == 1, "EP3 998=1");
        Require(state.sessionVariables.at("996") == 1, "EP3 996=1");
        Require(state.sessionVariables.at("994") == 1, "EP3 994=1");
        Require(state.sessionVariables.at("992") == 1, "EP3 992=1");
        Require(state.sessionVariables.at("991") == 1, "EP3 991=1");

        Require(
            state.sessionVariables.at("BS011KK07") == 31,
            "EP3 BS011KK07=31");
    }

    const std::string dump = router.DumpState();

    Require(
        dump.find("scene=02/02-2K-B00") != std::string::npos,
        "DumpState EP3 scene");

    Require(
        dump.find("002=47") != std::string::npos,
        "DumpState EP3 feeling");

    const auto ep3Next = router.ResolveNext();

    Require(
        ep3Next.kind == SchoolDaysRouter::NextKind::Advanced,
        "EP3 checkpoint can advance");

    Require(
        ep3Next.transitionId == 263,
        "EP3 expected transition t263");

    // ---------------------------------------------------------------------
    // Existing Route 15 routing-only dispatcher checkpoint.
    // ---------------------------------------------------------------------

    Require(
        router.DevCheckpointScene("sd-ep4-r15-e08-routing-only").value ==
            "03/03-KB-E08",
        "routing-only checkpoint SceneKey");

    Require(
        router.RestoreDevCheckpoint("sd-ep4-r15-e08-routing-only"),
        "routing-only checkpoint restore");

    {
        const auto& state = router.State();

        Require(state.route == 15, "Route15 checkpoint ROUTE=15");
        Require(state.scene == 39, "Route15 checkpoint SCENE=39");

        Require(
            state.sessionVariables.at("001") == 10,
            "Route15 checkpoint 001=10");

        Require(
            state.sessionVariables.at("002") == 84,
            "Route15 checkpoint 002=84");

        Require(
            router.CurrentScene().value == "03/03-KB-E08",
            "Route15 checkpoint current SceneKey");
    }

    const auto intoDispatcher = router.ResolveNext();

    Require(
        intoDispatcher.kind == SchoolDaysRouter::NextKind::Advanced,
        "E08 advances into routing-only dispatcher");

    Require(
        intoDispatcher.transitionId == 917,
        "E08 expected transition is t917");

    Require(
        intoDispatcher.destination.value == "03/03-KB-E00",
        "t917 destination is routing-only E00");

    Require(
        router.IsCurrentSceneRoutingOnly(),
        "E00 recognized as routing-only");

    const auto outOfDispatcher = router.ResolveNext();

    Require(
        outOfDispatcher.kind == SchoolDaysRouter::NextKind::Advanced,
        "routing-only E00 resolves immediately");

    Require(
        outOfDispatcher.transitionId == 907,
        "001<=002 selects t907");

    Require(
        outOfDispatcher.destination.value == "03/03-KB-G00",
        "t907 destination is physical G00");

    Require(
        !router.IsCurrentSceneRoutingOnly(),
        "G00 is not routing-only");

    std::cout << "School Days DEV checkpoint PASS\n";
    std::cout << "EP1 handoff: t"
              << ep1Handoff.transitionId
              << " -> "
              << ep1Handoff.destination.value
              << "\n";

    std::cout << "Ending16 terminal: t"
              << terminal.transitionId
              << " ending="
              << terminal.endingId
              << "\n";

    std::cout << "EP3: t"
              << ep3Next.transitionId
              << " -> "
              << ep3Next.destination.value
              << "\n";

    std::cout << "routing-only: t"
              << intoDispatcher.transitionId
              << " -> "
              << intoDispatcher.destination.value
              << " -> t"
              << outOfDispatcher.transitionId
              << " -> "
              << outOfDispatcher.destination.value
              << "\n";

    return 0;
}
