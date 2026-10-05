#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/parsers/KtrfTyped.h>
#include <Kotonoha/routing/SchoolDaysKtrfAdapter.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct CallbackState {
    int ending_calls = 0;
    int64_t ending_code = -1;
    int hook_calls = 0;
    std::string hook_symbol;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "KtrfSchoolDaysAdapterTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

int OnEnding(int64_t code, void* userdata, Kotonoha_KtrfError*) {
    auto* state = static_cast<CallbackState*>(userdata);
    ++state->ending_calls;
    state->ending_code = code;
    return 1;
}

int OnHook(const char* symbol, size_t size, void* userdata,
           Kotonoha_KtrfError*) {
    auto* state = static_cast<CallbackState*>(userdata);
    ++state->hook_calls;
    state->hook_symbol.assign(symbol, size);
    return 1;
}

uint32_t Count(const Kotonoha_KtrfDocument& doc, const char type[4]) {
    const auto* section = Kotonoha_KtrfFindSection(&doc, type);
    return section ? section->item_count : 0u;
}

std::string Slice(const char* data, size_t size) {
    return data ? std::string(data, size) : std::string();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: KtrfSchoolDaysAdapterTest <school-days-hq.ktnroute>\n";
        return 2;
    }

    Kotonoha_KtrfDocument document{};
    Kotonoha_KtrfError error{};
    Kotonoha_KtrfInit(&document);
    if (!Kotonoha_KtrfLoadFile(argv[1], &document, &error))
        Fail(std::string("load failed: ") + error.message);

    CallbackState callback_state;
    Kotonoha_SchoolDaysKtrfCallbacks callbacks{};
    callbacks.register_ending = OnEnding;
    callbacks.call_hook = OnHook;
    callbacks.userdata = &callback_state;

    Kotonoha_SchoolDaysKtrfAdapter adapter{};
    Kotonoha_SchoolDaysKtrfAdapterInit(&adapter);
    if (!Kotonoha_SchoolDaysKtrfAdapterBind(
            &adapter, &document, &callbacks, &error))
        Fail(std::string("bind failed: ") + error.message);

    Check(Count(document, "NODE") == 1857u, "NODE count mismatch");
    Check(Count(document, "RSRC") == 1855u, "RSRC count mismatch");
    Check(Count(document, "CHOI") == 287u, "CHOI count mismatch");
    Check(Count(document, "ENDG") == 22u, "ENDG count mismatch");
    Check(Count(document, "HOOK") == 3u, "HOOK count mismatch");

    uint32_t scenes = 0u;
    uint32_t dispatchers = 0u;
    for (uint32_t i = 0; i < Count(document, "NODE"); ++i) {
        Kotonoha_SchoolDaysKtrfNodeView view{};
        if (!Kotonoha_SchoolDaysKtrfGetNodeView(
                &adapter, i, &view, &error))
            Fail(std::string("node view failed: ") + error.message);
        if (view.kind == KOTONOHA_SDHQ_NODE_SCENE) {
            ++scenes;
            Check(view.resource_index != KOTONOHA_KTRF_NULL_INDEX,
                  "scene missing resource index");
            Check(view.scene_key != nullptr && view.scene_key_length != 0u,
                  "scene missing scene-key");
        } else if (view.kind == KOTONOHA_SDHQ_NODE_DISPATCHER) {
            ++dispatchers;
            Check(view.resource_index == KOTONOHA_KTRF_NULL_INDEX,
                  "dispatcher unexpectedly has resource");
            Check(view.scene_key == nullptr && view.scene_key_length == 0u,
                  "dispatcher unexpectedly has scene-key");
        } else {
            Fail("unknown adapter node kind");
        }
    }
    Check(scenes == 1855u, "physical scene inventory mismatch");
    Check(dispatchers == 2u, "dispatcher inventory mismatch");

    Kotonoha_SchoolDaysKtrfNodeView initial{};
    if (!Kotonoha_SchoolDaysKtrfResetNewGame(&adapter, &initial, &error))
        Fail(std::string("new-game reset failed: ") + error.message);
    Check(initial.kind == KOTONOHA_SDHQ_NODE_SCENE,
          "new-game entry is not a physical scene");
    Check(Slice(initial.scene_key, initial.scene_key_length) == "00/00-00-A00",
          "new-game scene-key mismatch");

    {
        Kotonoha_SchoolDaysKtrfAdvanceResult result{};
        if (!Kotonoha_SchoolDaysKtrfAdvance(
                &adapter, "ktrf:next", &result, &error))
            Fail(std::string("single-step advance failed: ") + error.message);
        Check(result.status == KOTONOHA_SDHQ_ADVANCE_ADVANCED,
              "first routing step did not advance");
        Check(result.hop_count == 1u, "single-step advance hop count mismatch");
        Check(result.route.transition_index != KOTONOHA_KTRF_NULL_INDEX,
              "first routing step has no transition");
        Check(result.scene.kind == KOTONOHA_SDHQ_NODE_SCENE ||
                  result.scene.kind == KOTONOHA_SDHQ_NODE_DISPATCHER,
              "advanced node has invalid adapter kind");
    }

    uint32_t ending_effect = KOTONOHA_KTRF_NULL_INDEX;
    uint32_t hook_effect = KOTONOHA_KTRF_NULL_INDEX;
    for (uint32_t i = 0; i < Count(document, "EFFT"); ++i) {
        Kotonoha_KtrfEffect effect{};
        if (!Kotonoha_KtrfGetEffect(&document, i, &effect, &error))
            Fail(std::string("effect read failed: ") + error.message);
        if (effect.shape == KOTONOHA_KTRF_EFFECT_REGISTER_ENDING &&
            ending_effect == KOTONOHA_KTRF_NULL_INDEX)
            ending_effect = i;
        if (effect.shape == KOTONOHA_KTRF_EFFECT_CALL_HOOK &&
            hook_effect == KOTONOHA_KTRF_NULL_INDEX)
            hook_effect = i;
    }
    Check(ending_effect != KOTONOHA_KTRF_NULL_INDEX,
          "no register-ending effect found");
    Check(hook_effect != KOTONOHA_KTRF_NULL_INDEX,
          "no call-hook effect found");

    if (!Kotonoha_KtrfRuntimeApplyEffect(
            &adapter.router.runtime, ending_effect, &error))
        Fail(std::string("ending callback effect failed: ") + error.message);
    Check(callback_state.ending_calls == 1, "ending callback was not forwarded");
    Check(callback_state.ending_code >= 0 && callback_state.ending_code < 22,
          "adapter forwarded invalid ending code");

    if (!Kotonoha_KtrfRuntimeApplyEffect(
            &adapter.router.runtime, hook_effect, &error))
        Fail(std::string("hook callback effect failed: ") + error.message);
    Check(callback_state.hook_calls == 1, "hook callback was not forwarded");
    Check(callback_state.hook_symbol.rfind("overflow.sdhq:", 0) == 0,
          "adapter forwarded invalid hook symbol");

    std::cout << "KTRF SCHOOL DAYS ADAPTER GATE 1 PASS\n";
    std::cout << "profile=overflow.school-days-hq/1.0.0\n";
    std::cout << "scene_nodes=" << scenes << '\n';
    std::cout << "dispatcher_nodes=" << dispatchers << '\n';
    std::cout << "resources=" << Count(document, "RSRC") << '\n';
    std::cout << "choices=" << Count(document, "CHOI") << '\n';
    std::cout << "endings=" << Count(document, "ENDG") << '\n';
    std::cout << "hooks=" << Count(document, "HOOK") << '\n';
    std::cout << "new_game_scene=00/00-00-A00\n";
    std::cout << "resource_resolution=PASS\n";
    std::cout << "callback_bridge=PASS\n";

    Kotonoha_SchoolDaysKtrfAdapterClean(&adapter);
    Kotonoha_KtrfClean(&document);
    return 0;
}
