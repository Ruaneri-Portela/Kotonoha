#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <algorithm>
#include <cstdio>

namespace Kotonoha {
namespace {

struct KnownScene { int16_t route; int16_t scene; const char* sceneKey; };
struct FeelingEffect { int16_t route; int16_t scene; uint8_t variable; int16_t delta; };
struct SpMapping { int16_t route; int16_t scene; int16_t spId; };
struct AllowedRoute { int16_t route; int16_t scene; int16_t destinationRoute; };
struct BackEvent {
    int16_t route;
    int16_t scene;
    uint8_t kind; // 1=flag-clear, 2=int-zero, 3=callback2c, 4=trial/PV marker
    int16_t target;
    uint16_t predicate;
};
struct PredecessorAction {
    int16_t route;
    int16_t scene;
    int16_t destinationRoute;
    uint8_t sceneKind; // 0=literal, 1=session symbol
    int16_t literalScene;
    const char* sceneSymbol;
    uint16_t predicate;
};

#include "generated/SchoolDaysBackRulesData.inc"

template <typename T, size_t N>
constexpr size_t CountOf(const T (&)[N]) { return N; }

bool Fail(Kotonoha_KtrfError* error, const char* message) {
    if (error != nullptr) {
        error->code = KOTONOHA_KTRF_ERROR_FORMAT;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

} // namespace

SchoolDaysBackRules::Counts SchoolDaysBackRules::GetCounts() {
    return {CountOf(kKnownScenes), CountOf(kFeelingEffects), CountOf(kSpMappings),
            CountOf(kBackEvents), CountOf(kAllowedRoutes),
            CountOf(kPredecessorActions)};
}

bool SchoolDaysBackRules::IsKnownScene(int route, int scene) {
    return std::any_of(std::begin(kKnownScenes), std::end(kKnownScenes),
                       [=](const KnownScene& x) {
                           return x.route == route && x.scene == scene;
                       });
}

const char* SchoolDaysBackRules::SceneKeyFor(int route, int scene) {
    for (const auto& x : kKnownScenes) {
        if (x.route == route && x.scene == scene) return x.sceneKey;
    }
    return nullptr;
}

bool SchoolDaysBackRules::CoordinateForSceneKey(
    const std::string& sceneKey, int* route, int* scene) {
    if (route == nullptr || scene == nullptr) return false;
    for (const auto& x : kKnownScenes) {
        if (sceneKey == x.sceneKey) {
            *route = x.route;
            *scene = x.scene;
            return true;
        }
    }
    return false;
}

bool SchoolDaysBackRules::DestinationRouteAllowed(int sourceRoute, int sourceScene,
                                                  int destinationRoute) {
    return std::any_of(std::begin(kAllowedRoutes), std::end(kAllowedRoutes),
                       [=](const AllowedRoute& x) {
                           return x.route == sourceRoute && x.scene == sourceScene &&
                                  x.destinationRoute == destinationRoute;
                       });
}

std::array<int64_t, SchoolDaysHistory::kFeelingCount>
SchoolDaysBackRules::InverseFeelingDelta(int route, int scene) {
    std::array<int64_t, SchoolDaysHistory::kFeelingCount> result{};
    for (const auto& effect : kFeelingEffects) {
        if (effect.route == route && effect.scene == scene &&
            effect.variable < result.size()) {
            result[effect.variable] += effect.delta;
        }
    }
    return result;
}

int SchoolDaysBackRules::SpForScene(int route, int scene) {
    for (const auto& mapping : kSpMappings) {
        if (mapping.route == route && mapping.scene == scene) return mapping.spId;
    }
    return -1;
}

bool SchoolDaysBackRules::ResolvePredecessor(
    int sourceRoute, int sourceScene, const SchoolDaysStateStore* state,
    PredecessorResult* result, Kotonoha_KtrfError* error) {
    if (state == nullptr || result == nullptr) {
        return Fail(error, "School Days Back predecessor argument is null");
    }
    *result = PredecessorResult{};

    // GetBackScriptFile only dispatches ROUTE 0..54. An out-of-range ROUTE
    // falls through the outer switch and returns 0 without entering a route
    // handler, so it must not inherit a handler's default/boundary effects.
    if (sourceRoute < 0 || sourceRoute > 54) {
        result->kind = PredecessorKind::Unresolved;
        return true;
    }

    // Route 0 / SCENE 0xD2 is the only concrete handler case that calls the
    // Trial/PV helper rather than a predecessor route mapper.
    if (sourceRoute == 0 && sourceScene == 0xd2) {
        result->kind = PredecessorKind::TrialPv;
        result->route = 0;
        result->scene = 0xd2;
        return true;
    }

    bool coordinateHasActions = false;
    for (const auto& action : kPredecessorActions) {
        if (action.route != sourceRoute || action.scene != sourceScene) continue;
        coordinateHasActions = true;
        bool active = false;
        if (!EvaluateGeneratedPredicate(action.predicate, state, &active, error))
            return false;
        if (!active) continue;

        int64_t destinationScene = action.literalScene;
        if (action.sceneKind == 1u) {
            if (!state->ReadSessionInt(action.sceneSymbol, &destinationScene, error))
                return false;
        }
        else if (action.sceneKind != 0u) {
            return Fail(error, "School Days Back predecessor scene kind is invalid");
        }
        if (destinationScene < 0 || destinationScene > 0x7fffffffLL) {
            return Fail(error, "School Days Back predecessor scene is out of range");
        }
        result->kind = PredecessorKind::Resolved;
        result->route = action.destinationRoute;
        result->scene = static_cast<int>(destinationScene);
        return true;
    }

    // Exactly one authenticated coordinate currently has a reachable-looking
    // predicate domain not covered by a predecessor mapper (Route17/Scene8).
    // The handler still returns success after the common source-side phase, so
    // represent that explicitly rather than guessing a destination.
    result->kind = coordinateHasActions ? PredecessorKind::NoScript
                                        : PredecessorKind::Boundary;
    return true;
}

bool SchoolDaysBackRules::ApplyBoundary(SchoolDaysStateStore* state,
                                        std::vector<std::string>* hooks,
                                        Kotonoha_KtrfError* error,
                                        std::vector<PersistenceWrite>* persistenceWrites) {
    if (state == nullptr || hooks == nullptr) {
        return Fail(error, "School Days Back boundary argument is null");
    }
    // FUN_10006760 mode=0: +0x2C, ROUTE=-1, empty script output.  The empty
    // script is represented by the caller not swapping Gameplay.
    hooks->emplace_back("overflow.sdhq:callback_2c");
    if (!state->WriteSessionInt("ROUTE", -1, error)) return false;
    if (persistenceWrites != nullptr)
        persistenceWrites->push_back({"ROUTE", 3u});
    return true;
}

bool SchoolDaysBackRules::ApplyTrialPv(SchoolDaysStateStore* state,
                                       std::vector<std::string>* hooks,
                                       Kotonoha_KtrfError* error,
                                       std::vector<PersistenceWrite>* persistenceWrites) {
    if (state == nullptr || hooks == nullptr) {
        return Fail(error, "School Days Trial/PV Back argument is null");
    }
    // FUN_100066C0 stores the supplied scene and returns PV/Trial_PV.
    if (!state->WriteSessionInt("SCENE", 0xd2, error)) return false;
    if (persistenceWrites != nullptr)
        persistenceWrites->push_back({"SCENE", 3u});
    hooks->emplace_back("overflow.sdhq:trial_pv_back");
    return true;
}

bool SchoolDaysBackRules::ApplySourceOnly(
    const SchoolDaysCoordinate& source, SchoolDaysStateStore* state,
    std::vector<std::string>* hooks, Kotonoha_KtrfError* error,
    std::vector<PersistenceWrite>* persistenceWrites) {
    if (state == nullptr || hooks == nullptr) {
        return Fail(error, "School Days Back source-effect argument is null");
    }

    static constexpr const char* kFeelingNames[SchoolDaysHistory::kFeelingCount] = {
        "000", "001", "002", "003", "004"
    };
    const auto feeling = InverseFeelingDelta(source.route, source.scene);
    for (size_t i = 0; i < feeling.size(); ++i) {
        if (feeling[i] == 0) continue;
        if (!state->AddSessionInt(kFeelingNames[i], feeling[i], error)) return false;
        if (persistenceWrites != nullptr)
            persistenceWrites->push_back({kFeelingNames[i], 3u});
        // FUN_10005CE0 performs the session write first, then invokes interface
        // slot +0x30 with argument 1 for non-zero writes to feelings 001/002.
        // Surface that side effect explicitly so presentation code can mirror
        // the original meter/update notification without coupling it here.
        if (i == 1u || i == 2u) {
            hooks->emplace_back("overflow.sdhq:callback_30");
        }
    }

    for (const auto& event : kBackEvents) {
        if (event.route != source.route || event.scene != source.scene) continue;
        bool active = false;
        if (!EvaluateGeneratedPredicate(event.predicate, state, &active, error))
            return false;
        if (!active) continue;
        switch (event.kind) {
        case 1:
            if (!state->WriteSessionInt(std::to_string(event.target), 0, error))
                return false;
            if (persistenceWrites != nullptr)
                persistenceWrites->push_back({std::to_string(event.target), 11u});
            break;
        case 2:
            if (!state->WriteSessionInt(std::to_string(event.target), 0, error))
                return false;
            if (persistenceWrites != nullptr)
                persistenceWrites->push_back({std::to_string(event.target), 3u});
            break;
        case 3:
            hooks->emplace_back("overflow.sdhq:callback_2c");
            break;
        case 4:
            hooks->emplace_back("overflow.sdhq:trial_pv_back");
            break;
        default:
            return Fail(error, "School Days Back event kind is invalid");
        }
    }
    return true;
}

bool SchoolDaysBackRules::Apply(const SchoolDaysCoordinate& source,
                                const SchoolDaysCoordinate& destination,
                                SchoolDaysStateStore* state,
                                std::vector<std::string>* hooks,
                                Kotonoha_KtrfError* error,
                                std::vector<PersistenceWrite>* persistenceWrites) {
    if (state == nullptr || hooks == nullptr) {
        return Fail(error, "School Days Back rule application argument is null");
    }
    if (!DestinationRouteAllowed(source.route, source.scene, destination.route)) {
        return Fail(error, "School Days Back destination route contradicts RouteProc oracle");
    }

    // RouteProc performs reverse feeling application before route-specific
    // side effects and predecessor mapper resolution.
    if (!ApplySourceOnly(source, state, hooks, error, persistenceWrites)) return false;

    return ApplyDestinationOnly(destination, state, error, persistenceWrites);
}

bool SchoolDaysBackRules::ApplyDestinationOnly(
    const SchoolDaysCoordinate& destination, SchoolDaysStateStore* state,
    Kotonoha_KtrfError* error,
    std::vector<PersistenceWrite>* persistenceWrites) {
    if (state == nullptr) {
        return Fail(error,
                    "School Days Back destination application state is null");
    }

    // Reverse mapper behavior: if the predecessor scene has an SP id, the
    // common 0x100067C0 helper clears SP<id> through the session flag setter.
    const int sp = SpForScene(destination.route, destination.scene);
    if (sp > 0) {
        const std::string key = "SP" + std::to_string(sp);
        if (!state->WriteSessionInt(key, 0, error)) return false;
        if (persistenceWrites != nullptr)
            persistenceWrites->push_back({key, 11u});
    }

    return true;
}

} // namespace Kotonoha
