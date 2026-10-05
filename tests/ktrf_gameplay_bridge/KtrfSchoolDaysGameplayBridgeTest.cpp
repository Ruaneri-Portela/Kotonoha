#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/parsers/KtrfTyped.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

struct Kotonoha_Game {};

namespace Kotonoha {
class Gameplay {
public:
    explicit Gameplay(std::string p) : path(std::move(p)) {}
    std::string path;
};
}

namespace {

struct FactoryState {
    uint32_t creates = 0;
    uint32_t destroys = 0;
    uint32_t live = 0;
    uint32_t maxLive = 0;
    std::vector<std::string> createdPaths;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "KtrfSchoolDaysGameplayBridgeTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

Kotonoha::Gameplay* FakeCreate(const char* path, Kotonoha_Game*, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    if (state == nullptr || path == nullptr || *path == '\0') return nullptr;
    ++state->creates;
    ++state->live;
    if (state->live > state->maxLive) state->maxLive = state->live;
    state->createdPaths.emplace_back(path);
    return new Kotonoha::Gameplay(path);
}

void FakeDestroy(Kotonoha::Gameplay* gameplay, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    if (gameplay == nullptr || state == nullptr) return;
    delete gameplay;
    ++state->destroys;
    if (state->live == 0u) Fail("factory live-count underflow");
    --state->live;
}

int64_t FirstChoiceValue(const Kotonoha_SchoolDaysKtrfAdapter* adapter,
                         Kotonoha_KtrfError* error) {
    uint32_t count = 0;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, error))
        Fail(std::string("choice count failed: ") + error->message);
    if (count != 1u) Fail("expected exactly one current choice");

    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    if (!Kotonoha_KtrfRouterCurrentChoiceAt(
            &adapter->router, 0u, &choiceIndex, error))
        Fail(std::string("choice lookup failed: ") + error->message);

    Kotonoha_KtrfChoice choice{};
    if (!Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, error))
        Fail(std::string("choice read failed: ") + error->message);
    Check(choice.option_count > 0u, "choice has no options");

    Kotonoha_KtrfChoiceOption option{};
    if (!Kotonoha_KtrfGetChoiceOption(
            adapter->document, choiceIndex, 0u, &option, error))
        Fail(std::string("choice option read failed: ") + error->message);
    Check(option.value.kind == KOTONOHA_KTRF_SCALAR_INT64,
          "first choice option is not int64");
    return option.value.as.int64_value;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: KtrfSchoolDaysGameplayBridgeTest <route.ktnroute> <assets-root>\n";
        return 2;
    }

    FactoryState factoryState;
    Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &FakeCreate;
    factory.destroy = &FakeDestroy;
    factory.userdata = &factoryState;

    Kotonoha_Game gameContext{};
    Kotonoha_KtrfError error{};
    Kotonoha::SchoolDaysKtrfGameplayBridge bridge;
    if (!bridge.Open(argv[1], argv[2], &gameContext, factory, &error))
        Fail(std::string("open failed: ") + error.message);

    Check(bridge.IsOpen(), "bridge did not enter open state");
    Check(bridge.CurrentGameplay() != nullptr, "initial Gameplay was not created");
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A00",
          "initial scene mismatch");
    Check(EndsWith(bridge.CurrentScene().orsPath, "00-00-A00.ENG.ORS"),
          "initial ORS path mismatch");
    Check(factoryState.creates == 1u && factoryState.destroys == 0u &&
              factoryState.live == 1u,
          "initial Gameplay ownership mismatch");

    uint32_t swaps = 0u;
    uint32_t choices = 0u;
    uint32_t blockedChecks = 0u;
    uint32_t pendingHandoffs = 0u;
    uint32_t terminals = 0u;
    uint32_t transitions = 0u;

    for (uint32_t stepIndex = 0u; stepIndex < 64u; ++stepIndex) {
        const auto* adapter = bridge.Session().Adapter();
        uint32_t choiceCount = 0u;
        if (!Kotonoha_KtrfRouterCurrentChoiceCount(
                &adapter->router, &choiceCount, &error))
            Fail(std::string("choice count failed: ") + error.message);

        if (choiceCount != 0u) {
            auto* before = bridge.CurrentGameplay();
            const uint32_t createsBefore = factoryState.creates;
            const uint32_t destroysBefore = factoryState.destroys;

            Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult blocked;
            if (!bridge.Advance("ktrf:next", &blocked, &error))
                Fail(std::string("blocked-choice probe failed: ") + error.message);
            Check(blocked.kind ==
                      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice,
                  "uncommitted choice did not block bridge advance");
            Check(bridge.CurrentGameplay() == before,
                  "blocked choice replaced active Gameplay");
            Check(factoryState.creates == createsBefore &&
                      factoryState.destroys == destroysBefore,
                  "blocked choice touched Gameplay ownership");
            ++blockedChecks;

            const int64_t value = FirstChoiceValue(adapter, &error);
            int accepted = 0;
            if (!bridge.CommitChoice(value, &accepted, &error))
                Fail(std::string("choice commit failed: ") + error.message);
            Check(accepted != 0, "legal choice was rejected");
            ++choices;
        }

        auto* before = bridge.CurrentGameplay();
        const std::string beforeScene = bridge.CurrentScene().sceneKey;
        const uint32_t createsBefore = factoryState.creates;
        const uint32_t destroysBefore = factoryState.destroys;

        Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
        if (!bridge.Advance("ktrf:next", &result, &error))
            Fail(std::string("advance failed: ") + error.message);

        if (result.transitionIndex != KOTONOHA_KTRF_NULL_INDEX) ++transitions;

        switch (result.kind) {
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Unresolved:
            Fail("deterministic bridge smoke path became unresolved");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice:
            Fail("bridge remained blocked after committing legal choice");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene:
            Check(bridge.CurrentGameplay() != nullptr &&
                      bridge.CurrentGameplay() != before,
                  "scene advance did not replace Gameplay");
            Check(bridge.CurrentScene().sceneKey == result.scene.sceneKey,
                  "bridge current scene does not match advance result");
            Check(bridge.CurrentScene().sceneKey != beforeScene,
                  "scene advance did not change scene key");
            Check(factoryState.creates == createsBefore + 1u &&
                      factoryState.destroys == destroysBefore + 1u &&
                      factoryState.live == 1u,
                  "scene swap ownership counts are wrong");
            ++swaps;
            break;
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff: {
            Check(bridge.HasPendingHandoff(), "pending handoff flag not set");
            Check(bridge.CurrentGameplay() == before,
                  "handoff replaced Gameplay before acknowledgement");
            Check(factoryState.creates == createsBefore &&
                      factoryState.destroys == destroysBefore,
                  "pending handoff touched Gameplay ownership");

            Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult continued;
            if (!bridge.ContinueHandoff(&continued, &error))
                Fail(std::string("continue handoff failed: ") + error.message);
            Check(continued.kind ==
                      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene,
                  "episode handoff did not swap to scene");
            Check(!bridge.HasPendingHandoff(), "handoff flag was not cleared");
            Check(factoryState.creates == createsBefore + 1u &&
                      factoryState.destroys == destroysBefore + 1u &&
                      factoryState.live == 1u,
                  "handoff scene swap ownership counts are wrong");
            ++pendingHandoffs;
            ++swaps;
            break;
        }
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal: {
            Check(bridge.HasPendingHandoff(), "terminal did not enter pending state");
            Check(bridge.CurrentGameplay() == before,
                  "terminal destroyed/replaced active Gameplay");

            Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult acknowledged;
            if (!bridge.ContinueHandoff(&acknowledged, &error))
                Fail(std::string("terminal acknowledgement failed: ") + error.message);
            Check(acknowledged.kind ==
                      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal,
                  "terminal acknowledgement changed kind");
            Check(!bridge.HasPendingHandoff(), "terminal pending flag was not cleared");
            Check(factoryState.creates == createsBefore &&
                      factoryState.destroys == destroysBefore &&
                      factoryState.live == 1u,
                  "terminal acknowledgement touched Gameplay ownership");
            ++terminals;
            stepIndex = 64u;
            break;
        }
        }
    }

    Check(transitions >= 8u, "bridge smoke path executed too few transitions");
    Check(swaps >= 8u, "bridge smoke path performed too few Gameplay swaps");
    Check(choices >= 1u, "bridge smoke path did not commit any choice");
    Check(blockedChecks >= 1u, "bridge smoke path did not prove deferred choice blocking");
    Check(pendingHandoffs >= 1u, "bridge smoke path did not exercise callback_38 handoff");
    Check(factoryState.live == 1u, "bridge does not own exactly one active Gameplay");
    Check(factoryState.maxLive <= 2u,
          "transactional swap held more than previous+next Gameplay");

    bridge.Close();
    Check(!bridge.IsOpen(), "bridge remained open after Close");
    Check(bridge.CurrentGameplay() == nullptr, "bridge retained Gameplay after Close");
    Check(factoryState.live == 0u, "Gameplay leaked after Close");
    Check(factoryState.creates == factoryState.destroys,
          "Gameplay create/destroy totals differ after Close");

    std::cout << "SCHOOL DAYS KTRF GAMEPLAY BRIDGE GATE 4 PASS\n";
    std::cout << "transitions=" << transitions << '\n';
    std::cout << "gameplay_swaps=" << swaps << '\n';
    std::cout << "choices_committed=" << choices << '\n';
    std::cout << "blocked_choice_checks=" << blockedChecks << '\n';
    std::cout << "episode_handoffs=" << pendingHandoffs << '\n';
    std::cout << "terminals=" << terminals << '\n';
    std::cout << "factory_creates=" << factoryState.creates << '\n';
    std::cout << "factory_destroys=" << factoryState.destroys << '\n';
    std::cout << "max_live_gameplays=" << factoryState.maxLive << '\n';
    std::cout << "single_active_gameplay_ownership=PASS\n";
    std::cout << "deferred_choice_preservation=PASS\n";
    std::cout << "callback38_handoff_preservation=PASS\n";
    return 0;
}
