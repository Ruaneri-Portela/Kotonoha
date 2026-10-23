#include <Kotonoha/SchoolDaysRouter.hpp>

#include <cstdlib>
#include <iostream>

using Kotonoha::SchoolDaysRouter;

namespace {

struct Step {
    int choice;
    int transition;
};

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    SchoolDaysRouter router;

    // Causal EP1 witness to t25. -99 means the source node has no choice.
    const Step steps[] = {
        {-99, 0}, {-99, 1}, {-99, 2}, {0, 5}, {-99, 6},
        {-99, 10}, {-99, 11}, {-99, 12}, {-99, 13}, {-99, 14},
        {-99, 15}, {-1, 16}, {-99, 19}, {-99, 20}, {-99, 21},
        {-99, 22}, {-99, 25},
    };

    SchoolDaysRouter::NextResult next;
    for (const auto& step : steps) {
        if (step.choice != -99) {
            Require(router.AcceptChoice(step.choice), "choice accepted");
        }

        next = router.ResolveNext();
        Require(next.kind == SchoolDaysRouter::NextKind::Advanced,
            "EP1 witness must advance");
        Require(next.transitionId == step.transition,
            "unexpected transition in EP1 witness");
    }

    Require(next.transitionId == 25, "handoff transition is t25");
    Require(next.destination.value == "01/01-00-A00",
        "t25 destination is first EP2 SceneKey");
    Require(next.HasCallback("callback_38"),
        "t25 exposes callback_38");

    const auto& state = router.State();
    Require(state.route == 1, "router state is post-t25 ROUTE=1");
    Require(state.scene == 0, "router state is post-t25 SCENE=0");
    Require(state.sessionVariables.at("ROUTE") == 1,
        "session ROUTE is post-t25");
    Require(state.sessionVariables.at("SCENE") == 0,
        "session SCENE is post-t25");

    std::cout << "School Days handoff router PASS\n";
    std::cout << "t25 -> " << next.destination.value
              << " callbacks=" << next.callbacks.size()
              << " route=" << state.route
              << " scene=" << state.scene << "\n";
    return 0;
}
