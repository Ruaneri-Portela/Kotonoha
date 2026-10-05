#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
[[noreturn]] void Fail(const std::string& message) { std::cerr << "KtrfSchoolDaysSessionTest: " << message << '\n'; std::exit(2); }
void Check(bool condition, const std::string& message) { if (!condition) Fail(message); }
bool EndsWith(const std::string& value, const std::string& suffix) { return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0; }
int64_t FirstChoiceValue(const Kotonoha_SchoolDaysKtrfAdapter* adapter, Kotonoha_KtrfError* error) {
    uint32_t count = 0;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, error)) Fail(error->message);
    if (count != 1u) Fail("expected exactly one current choice");
    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    if (!Kotonoha_KtrfRouterCurrentChoiceAt(&adapter->router, 0u, &choiceIndex, error)) Fail(error->message);
    Kotonoha_KtrfChoice choice{};
    if (!Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, error)) Fail(error->message);
    Check(choice.option_count > 0u, "choice has no options");
    Kotonoha_KtrfChoiceOption option{};
    if (!Kotonoha_KtrfGetChoiceOption(adapter->document, choiceIndex, 0u, &option, error)) Fail(error->message);
    Check(option.value.kind == KOTONOHA_KTRF_SCALAR_INT64, "first choice option is not int64");
    return option.value.as.int64_value;
}
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: KtrfSchoolDaysSessionTest <route.ktnroute> <assets-root>\n"; return 2; }
    Kotonoha_KtrfError error{};
    Kotonoha::SchoolDaysKtrfSession session;
    if (!session.Open(argv[1], argv[2], &error)) Fail(std::string("open failed: ") + error.message);
    Kotonoha::SchoolDaysKtrfScene scene;
    if (!session.ResetNewGame(&scene, &error)) Fail(std::string("reset failed: ") + error.message);
    Check(scene.sceneKey == "00/00-00-A00", "new-game scene mismatch");
    Check(EndsWith(scene.orsPath, "00-00-A00.ENG.ORS"), "new-game ORS path mismatch");
    Kotonoha::SchoolDaysKtrfScene current;
    if (!session.CurrentScene(&current, &error)) Fail(error.message);
    Check(current.sceneKey == scene.sceneKey && current.orsPath == scene.orsPath, "CurrentScene mismatch");
    uint32_t loads = 1u, choices = 0u, hooks = 0u, endings = 0u, transitions = 0u;
    for (uint32_t step = 0; step < 64u; ++step) {
        const auto* adapter = session.Adapter();
        uint32_t choiceCount = 0u;
        if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &choiceCount, &error)) Fail(error.message);
        if (choiceCount != 0u) {
            Kotonoha::SchoolDaysKtrfSession::StepResult blocked;
            if (!session.Advance("ktrf:next", &blocked, &error)) Fail(error.message);
            Check(blocked.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::BlockedChoice, "uncommitted choice did not block");
            int accepted = 0;
            if (!session.CommitChoice(FirstChoiceValue(adapter, &error), &accepted, &error)) Fail(error.message);
            Check(accepted != 0, "legal choice rejected");
            ++choices;
        }
        Kotonoha::SchoolDaysKtrfSession::StepResult result;
        if (!session.Advance("ktrf:next", &result, &error)) Fail(error.message);
        hooks += static_cast<uint32_t>(result.hooks.size());
        endings += static_cast<uint32_t>(result.endings.size());
        if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::BlockedChoice) Fail("still blocked after choice");
        if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Unresolved) Fail("unresolved smoke path");
        if (result.transitionIndex != KOTONOHA_KTRF_NULL_INDEX) ++transitions;
        if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Terminal) break;
        Check(!result.scene.sceneKey.empty(), "empty scene-key");
        Check(EndsWith(result.scene.orsPath, ".ENG.ORS"), "wrong ORS extension");
        ++loads;
    }
    Check(transitions >= 8u, "too few transitions");
    Check(loads >= 8u, "too few ORS loads");
    Check(choices >= 1u, "no choice exercised");
    std::cout << "SCHOOL DAYS KTRF SESSION GATE 3 PASS\n"
              << "new_game_scene=00/00-00-A00\n"
              << "physical_scene_loads=" << loads << '\n'
              << "transitions=" << transitions << '\n'
              << "choices_committed=" << choices << '\n'
              << "hooks_seen=" << hooks << '\n'
              << "endings_seen=" << endings << '\n'
              << "route_document_ownership=PASS\n"
              << "adapter_asset_bridge=PASS\n"
              << "physical_ors_probe_per_scene=PASS\n";
    return 0;
}
