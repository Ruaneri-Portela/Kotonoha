#include <Kotonoha/SchoolDaysRouter.hpp>

#include <cctype>
#include <cstdint>
#include <sstream>
#include <utility>

namespace Kotonoha {
namespace {

enum class ConditionKind : uint8_t {
    Choice,
    SessionValue,
    SessionComparison,
    GlobalValue,
    Callback34,
    FlagOr,
};

enum class CompareOp : uint8_t { Eq, Ne, Gt, Le };

enum class EffectKind : uint8_t {
    SetSessionConst,
    SetSessionFromSession,
    SetGlobalConst,
    RegisterEnding,
    Callback,
};

struct NodeData {
    const char* sceneKey;
    uint32_t transitionStart;
    uint16_t transitionCount;
    uint8_t choiceMask;
};

struct ConditionData {
    ConditionKind kind;
    CompareOp op;
    const char* sourceA;
    const char* sourceB;
    int value;
};

struct EffectData {
    EffectKind kind;
    const char* target;
    const char* source;
    int value;
};

struct TransitionData {
    uint16_t id;
    int16_t destinationRoute;
    int16_t destinationScene;
    uint32_t conditionStart;
    uint16_t conditionCount;
    uint32_t effectStart;
    uint16_t effectCount;
    bool terminal;
};

struct FeelingDelta {
    const char* variable;
    int delta;
};

struct FeelingResolution {
    uint16_t nodeIndex;
    int8_t choice;
    uint32_t deltaStart;
    uint16_t deltaCount;
};

#include "SchoolDaysRouteData.generated.inc"

constexpr int kRouteCount = 55;
constexpr int kPendingChoice = -2;
constexpr int kTimeoutChoice = -1;

bool Compare(int left, CompareOp op, int right) {
    switch (op) {
    case CompareOp::Eq: return left == right;
    case CompareOp::Ne: return left != right;
    case CompareOp::Gt: return left > right;
    case CompareOp::Le: return left <= right;
    }
    return false;
}

} // namespace

SceneKey SceneKey::FromScriptPath(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos || slash < 2) return {};
    const std::string directory = path.substr(0, slash);
    const size_t parentSlash = directory.find_last_of("/\\");
    const std::string group = directory.substr(parentSlash == std::string::npos ? 0 : parentSlash + 1);
    if (group.size() != 2 || !std::isdigit(static_cast<unsigned char>(group[0])) ||
        !std::isdigit(static_cast<unsigned char>(group[1]))) return {};
    const std::string filename = path.substr(slash + 1);
    constexpr const char* extension = ".ENG.ORS";
    if (filename.size() <= 8 || filename.substr(filename.size() - 8) != extension ||
        filename.substr(0, 2) != group || filename[2] != '-') return {};
    return { group + "/" + filename.substr(0, filename.size() - 8) };
}

SchoolDaysRouter::SchoolDaysRouter(std::function<void(const std::string&)> logger)
    : log(std::move(logger)) {
    Reset();
}

void SchoolDaysRouter::Reset() {
    state = SchoolDaysRouteState{};
    state.route = 0;
    state.scene = 0;
    state.choiceResult = kPendingChoice;
    state.sessionVariables = {
        {"ROUTE", 0}, {"SCENE", 0}, {"000", 0}, {"001", 0},
        {"002", 0}, {"003", 0}, {"004", 0}
    };
    state.globalVariables = {
        {"dword_3A6F40", 0},
        {"dword_3A2294", 1},
    };
    state.callback34 = 0;
    state.feelingApplied = false;
    state.endingRegistrations.clear();
}

int SchoolDaysRouter::CurrentNodeIndex() const {
    if (state.route < 0 || state.route >= kRouteCount || state.scene < 0) return -1;
    const uint32_t begin = kRouteOffsets[state.route];
    const uint32_t end = kRouteOffsets[state.route + 1];
    const uint32_t index = begin + static_cast<uint32_t>(state.scene);
    if (index >= end) return -1;
    return static_cast<int>(index);
}

SceneKey SchoolDaysRouter::CurrentScene() const {
    const int node = CurrentNodeIndex();
    if (node < 0) return {};
    return { kNodes[node].sceneKey };
}

bool SchoolDaysRouter::IsCurrentSceneRoutingOnly() const {
    const SceneKey scene = CurrentScene();

    // These two RouteProc nodes are present in the recovered DLL route tables
    // but have no physical ORS script in the School Days HQ script set.
    // They are dispatcher nodes: RouteProc evaluates them immediately and
    // forwards to the next real script based on the accumulated state.
    return scene.value == "03/03-B2-A00" ||
        scene.value == "03/03-KB-E00";
}

#ifdef KOTONOHA_DEV_CHECKPOINTS
SceneKey SchoolDaysRouter::DevCheckpointScene(const std::string& name) const {
    if (name == "sd-ep3-r4-b00") return { "02/02-2K-B00" };
    return {};
}

bool SchoolDaysRouter::RestoreDevCheckpoint(const std::string& name) {
    if (name != "sd-ep3-r4-b00") return false;

    Reset();

    state.route = 4;
    state.scene = 1;
    state.choiceResult = kPendingChoice;
    state.callback34 = 0;
    state.feelingApplied = false;
    state.endingRegistrations.clear();

    state.sessionVariables = {
        {"ROUTE", 4}, {"SCENE", 1},
        {"000", 0}, {"001", 10}, {"002", 47}, {"003", 0}, {"004", 0},

        {"BS0000B00", 4},
        {"BS0000H02", 15},

        {"BS0100B04", 2},
        {"BS0100B05", 5},
        {"BS0100D00", 9},
        {"BS0100E01", 21},
        {"BS0100E05", 24},
        {"BS0100F00", 28},
        {"BS0100G00", 33},
        {"BS0100I00", 38},
        {"BS0100K00", 52},
        {"BS0100N00", 67},
        {"BS0100N04", 76},
        {"BS0100Q00", 79},
        {"BS0100U00", 69},

        {"BS011KD00", 3},
        {"BS011KE06", 18},
        {"BS011KF00", 19},
        {"BS011KK03", 27},
        {"BS011KK07", 31},

        {"998", 1},
        {"996", 1},
        {"994", 1},
        {"992", 1},
        {"991", 1},
    };

    state.globalVariables = {
        {"dword_3A6F40", 0},
        {"dword_3A2294", 1},
    };

    const SceneKey scene = CurrentScene();
    const bool valid = scene.value == "02/02-2K-B00";
    if (log) {
        log(std::string("DEV checkpoint ") + (valid ? "restored: " : "invalid: ") +
            name + " scene=" + scene.value);
    }
    return valid;
}

std::string SchoolDaysRouter::DumpState() const {
    std::ostringstream out;
    out << "scene=" << CurrentScene().value
        << "\nroute=" << state.route
        << "\nscene_index=" << state.scene
        << "\nchoice=" << state.choiceResult
        << "\ncallback34=" << state.callback34
        << "\nfeeling_applied=" << (state.feelingApplied ? 1 : 0);

    out << "\nsession:";
    for (const auto& entry : state.sessionVariables) {
        out << "\n  " << entry.first << "=" << entry.second;
    }

    out << "\nglobal:";
    for (const auto& entry : state.globalVariables) {
        out << "\n  " << entry.first << "=" << entry.second;
    }

    out << "\nendings:";
    if (state.endingRegistrations.empty()) {
        out << " []";
    }
    else {
        for (int ending : state.endingRegistrations) {
            out << " " << ending;
        }
    }
    return out.str();
}
#endif

int SchoolDaysRouter::ReadSession(const char* name) const {
    if (name == nullptr) return 0;
    const auto found = state.sessionVariables.find(name);
    return found == state.sessionVariables.end() ? 0 : found->second;
}

int SchoolDaysRouter::ReadGlobal(const char* name) const {
    if (name == nullptr) return 0;
    const auto found = state.globalVariables.find(name);
    return found == state.globalVariables.end() ? 0 : found->second;
}

void SchoolDaysRouter::WriteSession(const char* name, int value) {
    if (name == nullptr) return;
    state.sessionVariables[name] = value;
    if (std::string(name) == "ROUTE") state.route = value;
    else if (std::string(name) == "SCENE") state.scene = value;
    if (log) log(std::string(name) + "=" + std::to_string(value));
}

void SchoolDaysRouter::WriteGlobal(const char* name, int value) {
    if (name == nullptr) return;
    state.globalVariables[name] = value;
    if (log) log(std::string("global ") + name + "=" + std::to_string(value));
}

void SchoolDaysRouter::AddFeeling(const char* name, int delta) {
    const int value = ReadSession(name) + delta;
    WriteSession(name, value);
    if (log) log(std::string("feeling ") + name + " += " + std::to_string(delta) + " -> " + std::to_string(value));
}

bool SchoolDaysRouter::HasChoice() const {
    const int node = CurrentNodeIndex();
    return node >= 0 && kNodes[node].choiceMask != 0;
}

bool SchoolDaysRouter::AcceptChoice(int result) {
    const int node = CurrentNodeIndex();
    if (node < 0 || kNodes[node].choiceMask == 0) return false;
    const bool optionAllowed = result >= 0 && result < 8 &&
        (kNodes[node].choiceMask & static_cast<uint8_t>(1u << result)) != 0;
    if (result != kTimeoutChoice && !optionAllowed) return false;
    if (state.choiceResult != kPendingChoice) return state.choiceResult == result;

    state.choiceResult = result;
    if (log) log("scene=" + CurrentScene().value + " choice=" + std::to_string(result));
    if (!state.feelingApplied) {
        for (const auto& resolution : kFeelingResolutions) {
            if (resolution.nodeIndex != static_cast<uint16_t>(node) || resolution.choice != result) continue;
            for (uint32_t i = 0; i < resolution.deltaCount; ++i) {
                const auto& delta = kFeelingDeltas[resolution.deltaStart + i];
                AddFeeling(delta.variable, delta.delta);
            }
            break;
        }
        state.feelingApplied = true;
    }
    return true;
}

SchoolDaysRouter::NextResult SchoolDaysRouter::ResolveNext() {
    const int nodeIndex = CurrentNodeIndex();
    if (nodeIndex < 0) return {};
    if (HasChoice() && state.choiceResult == kPendingChoice) {
        if (log) log("next blocked scene=" + CurrentScene().value + " choice=pending(-2)");
        return {};
    }

    auto eval = [&](const ConditionData& condition) {
        int left = 0;
        int right = condition.value;
        switch (condition.kind) {
        case ConditionKind::Choice:
            left = state.choiceResult;
            break;
        case ConditionKind::SessionValue:
            left = ReadSession(condition.sourceA);
            break;
        case ConditionKind::SessionComparison:
            left = ReadSession(condition.sourceA);
            right = ReadSession(condition.sourceB);
            break;
        case ConditionKind::GlobalValue:
            left = ReadGlobal(condition.sourceA);
            break;
        case ConditionKind::Callback34:
            left = state.callback34;
            break;
        case ConditionKind::FlagOr:
            left = (ReadSession(condition.sourceA) != 0 || ReadSession(condition.sourceB) != 0) ? 1 : 0;
            break;
        }
        return Compare(left, condition.op, right);
    };

    const NodeData& node = kNodes[nodeIndex];
    const TransitionData* selected = nullptr;
    for (uint32_t i = 0; i < node.transitionCount; ++i) {
        const TransitionData& transition = kTransitions[node.transitionStart + i];
        bool match = true;
        for (uint32_t c = 0; c < transition.conditionCount; ++c) {
            if (!eval(kConditions[transition.conditionStart + c])) {
                match = false;
                break;
            }
        }
        if (match) {
            selected = &transition;
            break;
        }
    }
    if (selected == nullptr) {
        if (log) log("no normal-new-game transition from " + CurrentScene().value);
        return {};
    }

    int newlyRegisteredEnding = -1;
    for (uint32_t i = 0; i < selected->effectCount; ++i) {
        const EffectData& effect = kEffects[selected->effectStart + i];
        switch (effect.kind) {
        case EffectKind::SetSessionConst:
            WriteSession(effect.target, effect.value);
            break;
        case EffectKind::SetSessionFromSession:
            WriteSession(effect.target, ReadSession(effect.source));
            break;
        case EffectKind::SetGlobalConst:
            WriteGlobal(effect.target, effect.value);
            break;
        case EffectKind::RegisterEnding:
            state.endingRegistrations.push_back(effect.value);
            newlyRegisteredEnding = effect.value;
            if (log) log("ending=" + std::to_string(effect.value));
            break;
        case EffectKind::Callback:
            if (log && effect.target != nullptr) log(std::string("callback ignored in normal-new-game router: ") + effect.target);
            break;
        }
    }

    const int transitionId = static_cast<int>(selected->id);
    state.choiceResult = kPendingChoice;
    state.feelingApplied = false;

    if (selected->terminal) {
        return { NextKind::Terminal, {}, transitionId, newlyRegisteredEnding };
    }

    state.route = selected->destinationRoute;
    state.scene = selected->destinationScene;
    state.sessionVariables["ROUTE"] = state.route;
    state.sessionVariables["SCENE"] = state.scene;
    const SceneKey destination = CurrentScene();
    if (destination.value.empty()) {
        if (log) log("invalid destination after transition t" + std::to_string(transitionId));
        return {};
    }
    if (log) log("next t" + std::to_string(transitionId) + " -> " + destination.value);
    return { NextKind::Advanced, destination, transitionId, newlyRegisteredEnding };
}

} // namespace Kotonoha
