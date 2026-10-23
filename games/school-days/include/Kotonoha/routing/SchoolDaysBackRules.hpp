#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Kotonoha/routing/SchoolDaysHistory.hpp>

namespace Kotonoha {

// Generated-data backed semantic projection of the authenticated
// RouteProcSDHQ.dll Back handlers.  The data itself is generated from the R1
// REA/Ghidra normalization; this class contains only engine-side application
// logic.
class SchoolDaysBackRules {
public:
    enum class PredecessorKind {
        Resolved,
        TrialPv,
        Boundary,
        NoScript,
        Unresolved,
    };

    struct PredecessorResult {
        PredecessorKind kind = PredecessorKind::Unresolved;
        int route = 0;
        int scene = 0;
    };

    struct Counts {
        size_t knownScenes = 0;
        size_t inverseFeelingEffects = 0;
        size_t spMappings = 0;
        size_t conditionalEvents = 0;
        size_t allowedDestinationRoutes = 0;
        size_t predecessorActions = 0;
    };

    struct PersistenceWrite {
        std::string symbol;
        uint8_t type = 3u;
    };

    static Counts GetCounts();

    static bool IsKnownScene(int route, int scene);
    static const char* SceneKeyFor(int route, int scene);
    static bool CoordinateForSceneKey(const std::string& sceneKey,
                                      int* route, int* scene);
    static bool DestinationRouteAllowed(int sourceRoute, int sourceScene,
                                        int destinationRoute);

    // Returns the exact state delta the original Back handler applies for the
    // current scene. Values are already signed for Back (normally negative).
    static std::array<int64_t, SchoolDaysHistory::kFeelingCount>
    InverseFeelingDelta(int route, int scene);

    // Positive SP id for a route mapper/scene, or -1 when the original mapper
    // did not associate an SP marker with that scene.
    static int SpForScene(int route, int scene);

    static bool ResolvePredecessor(int sourceRoute, int sourceScene,
                                   const SchoolDaysStateStore* state,
                                   PredecessorResult* result,
                                   Kotonoha_KtrfError* error);
    static bool ApplyBoundary(SchoolDaysStateStore* state,
                              std::vector<std::string>* hooks,
                              Kotonoha_KtrfError* error,
                              std::vector<PersistenceWrite>* persistenceWrites = nullptr);
    static bool ApplyTrialPv(SchoolDaysStateStore* state,
                             std::vector<std::string>* hooks,
                             Kotonoha_KtrfError* error,
                             std::vector<PersistenceWrite>* persistenceWrites = nullptr);
    static bool ApplySourceOnly(const SchoolDaysCoordinate& source,
                                SchoolDaysStateStore* state,
                                std::vector<std::string>* hooks,
                                Kotonoha_KtrfError* error,
                                std::vector<PersistenceWrite>* persistenceWrites = nullptr);
    static bool ApplyDestinationOnly(const SchoolDaysCoordinate& destination,
                                     SchoolDaysStateStore* state,
                                     Kotonoha_KtrfError* error,
                                     std::vector<PersistenceWrite>* persistenceWrites = nullptr);

    // Apply source-scene effects in original ordering after route validation:
    // inverse feelings, conditional clears/int-zero/callback, then the
    // predecessor mapper's reverse SP clear.  Caller owns transaction rollback.
    static bool Apply(const SchoolDaysCoordinate& source,
                      const SchoolDaysCoordinate& destination,
                      SchoolDaysStateStore* state,
                      std::vector<std::string>* hooks,
                      Kotonoha_KtrfError* error,
                      std::vector<PersistenceWrite>* persistenceWrites = nullptr);
};

} // namespace Kotonoha
