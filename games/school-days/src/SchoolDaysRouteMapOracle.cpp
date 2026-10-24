#include <Kotonoha/routing/SchoolDaysRouteMapOracle.hpp>

extern "C" {
#include <Kotonoha/parsers/KtrfTyped.h>
}

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Kotonoha {
namespace {

constexpr int kRouteCount = 55;

struct SceneOverride {
    uint8_t scene;
    uint8_t value;
};

struct RouteRule {
    uint8_t spGroup;
    uint8_t overrideCount;
};

struct SpGroup {
    int first;
    int count;
};

// Six native fallback helpers at 0x10003BA0, 0x10003C40, 0x10003D80,
// 0x10003F10, 0x100043D0 and 0x10004B70. Each scans descending and therefore
// the numerically highest active SP wins.
constexpr SpGroup kSpGroups[6] = {
    {100, 4}, {200, 9}, {300, 11}, {400, 35}, {500, 57}, {600, 45},
};

// CheckScript's ROUTE dispatch selects one of the fallback groups above plus a
// small SCENE override set generated in each route-specific helper.
constexpr RouteRule kRouteRules[kRouteCount] = {
    {0, 4}, {1, 6}, {1, 2}, {1, 1}, {2, 5}, {2, 6}, {3, 1}, {3, 2},
    {3, 1}, {3, 0}, {3, 1}, {2, 1}, {3, 1}, {3, 2}, {3, 3}, {3, 6},
    {3, 0}, {3, 9}, {3, 9}, {4, 4}, {4, 2}, {4, 0}, {4, 7}, {4, 6},
    {4, 8}, {4, 5}, {4, 12}, {4, 4}, {4, 2}, {2, 5}, {4, 1}, {4, 1},
    {5, 4}, {5, 6}, {5, 0}, {5, 0}, {5, 0}, {5, 0}, {5, 0}, {5, 2},
    {5, 1}, {5, 2}, {5, 1}, {5, 2}, {5, 4}, {5, 2}, {5, 1}, {5, 2},
    {5, 3}, {5, 3}, {5, 2}, {5, 5}, {5, 3}, {5, 1}, {5, 1},
};

// Exact SCENE -> CheckScript values from the 55 route helpers dispatched by
// _CheckScript@8. Unused cells are zero-initialized and ignored by count.
constexpr SceneOverride kSceneOverrides[kRouteCount][12] = {
    {{0,11},{3,12},{14,13},{20,14}},
    {{0,11},{36,13},{53,12},{62,15},{77,16},{108,14}},
    {{2,17},{35,19}},
    {{19,18}},
    {{0,12},{41,21},{45,18},{57,19},{90,20}},
    {{0,11},{19,13},{39,15},{51,14},{87,17},{90,16}},
    {{0,23}},
    {{1,24},{34,25}},
    {{3,26}},
    {},
    {{2,38}},
    {{3,35}},
    {{4,40}},
    {{21,44},{44,45}},
    {{0,20},{22,22},{61,21}},
    {{0,28},{24,42},{53,41},{70,43},{85,39},{108,39}},
    {},
    {{0,19},{10,15},{16,16},{19,11},{36,13},{51,12},{55,14},{61,18},{67,17}},
    {{0,27},{8,36},{15,30},{21,29},{28,31},{44,33},{45,34},{48,32},{68,37}},
    {{0,47},{4,50},{11,56},{12,49}},
    {{4,46},{5,55}},
    {},
    {{0,11},{10,16},{19,13},{28,14},{30,15},{43,17},{51,18}},
    {{0,23},{1,25},{5,27},{24,36},{25,35},{36,33}},
    {{0,40},{2,44},{15,45},{26,53},{27,52},{40,48},{42,51},{48,54}},
    {{0,60},{1,61},{23,63},{24,67},{26,64}},
    {{0,22},{14,24},{22,26},{26,29},{36,28},{40,34},{46,30},{68,31},{78,32},{90,38},{94,39},{98,37}},
    {{0,12},{23,20},{43,19},{44,21}},
    {{0,42},{16,58}},
    {{0,59},{3,62},{34,66},{35,57},{36,65}},
    {{0,41}},
    {{0,43}},
    {{0,13},{5,15},{14,18},{15,19}},
    {{0,44},{6,53},{8,54},{17,52},{18,47},{58,50}},
    {},
    {},
    {},
    {},
    {},
    {{5,17},{6,16}},
    {{9,43}},
    {{39,20},{45,21}},
    {{0,11}},
    {{0,31},{47,38}},
    {{0,34},{7,37},{12,40},{13,39}},
    {{0,46},{24,49}},
    {{0,12}},
    {{0,23},{47,30}},
    {{0,22},{12,29},{22,28}},
    {{0,32},{13,55},{28,42}},
    {{0,35},{46,41}},
    {{0,14},{1,26},{8,25},{15,24},{22,27}},
    {{0,45},{12,48},{45,51}},
    {{0,36}},
    {{0,33}},
};

// _GetRouteMapPage@8 ROUTE byte table at 0x1000C350. Values index the 12
// authenticated switch arms below.
constexpr uint8_t kPageCaseByRoute[kRouteCount] = {
    0,0,0,0,1,0,2,2,2,2,3,3,3,4,2,5,6,6,3,3,3,7,0,2,3,4,2,0,
    3,8,2,2,0,4,2,4,4,4,0,0,9,0,0,3,3,4,0,2,2,10,3,11,4,3,3,
};

void ClearError(Kotonoha_KtrfError* error) {
    if (error == nullptr) return;
    error->code = KOTONOHA_KTRF_OK;
    error->message[0] = '\0';
}

bool Fail(Kotonoha_KtrfError* error, Kotonoha_KtrfErrorCode code,
          const char* message) {
    if (error != nullptr) {
        error->code = code;
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

bool NativeStoryFor(const KtrfStateStore& state, int64_t route,
                    int* story, Kotonoha_KtrfError* error) {
    if (story == nullptr) return false;
    if (route == 0) *story = 1;
    else if (route >= 1 && route <= 3) *story = 2;
    else if (route >= 4 && route <= 5) *story = 3;
    else if (route >= 6 && route <= 18) *story = 4;
    else if (route >= 19 && route <= 31) *story = 5;
    else if (route >= 32 && route <= 54) *story = 6;
    else {
        int64_t endClear = 0;
        if (!state.ReadGlobalInt("EndClear", &endClear, error)) return false;
        *story = endClear != 0 ? 6 : 0;
    }
    return true;
}

bool SpFallback(const KtrfStateStore& state, uint8_t groupIndex,
                int* result, Kotonoha_KtrfError* error) {
    if (result == nullptr || groupIndex >= 6) return false;
    const SpGroup group = kSpGroups[groupIndex];
    for (int i = group.count - 1; i >= 0; --i) {
        char key[8]{};
        std::snprintf(key, sizeof(key), "SP%03d", group.first + i);
        int64_t value = 0;
        if (!state.ReadSessionInt(key, &value, error)) return false;
        if (value != 0) {
            *result = 11 + i;
            return true;
        }
    }
    *result = 0;
    return true;
}

bool CheckScriptImpl(const KtrfStateStore& state, int major, int* result,
                     Kotonoha_KtrfError* error) {
    if (result == nullptr) return false;
    *result = 0;

    int64_t route = 0;
    if (!state.ReadSessionInt("ROUTE", &route, error)) return false;

    // Native _CheckScript@8 calls _GetStory@4 before its ROUTE bounds check.
    int story = 0;
    if (!NativeStoryFor(state, route, &story, error)) return false;
    if (major != story - 1) return true;
    if (route < 0 || route >= kRouteCount) return true;

    int64_t scene = 0;
    if (!state.ReadSessionInt("SCENE", &scene, error)) return false;
    const auto routeIndex = static_cast<int>(route);
    const RouteRule rule = kRouteRules[routeIndex];
    for (uint8_t i = 0; i < rule.overrideCount; ++i) {
        const SceneOverride entry = kSceneOverrides[routeIndex][i];
        if (scene == entry.scene) {
            *result = entry.value;
            return true;
        }
    }
    return SpFallback(state, rule.spGroup, result, error);
}

bool ReadCheckForPage(const KtrfStateStore& state, int major, int* value,
                      Kotonoha_KtrfError* error) {
    return CheckScriptImpl(state, major, value, error);
}

} // namespace

int SchoolDaysRouteMapOracle::CheckScript(const KtrfStateStore& state,
                                         int major,
                                         Kotonoha_KtrfError* error) {
    ClearError(error);
    int result = 0;
    return CheckScriptImpl(state, major, &result, error) ? result : 0;
}

int SchoolDaysRouteMapOracle::GetRouteMapPage(const KtrfStateStore& state,
                                             int major,
                                             Kotonoha_KtrfError* error) {
    ClearError(error);
    int64_t route = 0;
    if (!state.ReadSessionInt("ROUTE", &route, error)) return 0;
    if (route < 0 || route >= kRouteCount) return 0;

    int check = 0;
    switch (kPageCaseByRoute[static_cast<int>(route)]) {
    case 0:
        return 0;
    case 1:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return check == 12 ? 0 : 1;
    case 2:
        return 1;
    case 3:
        return 2;
    case 4:
        return 3;
    case 5:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return (check == 28 || check == 39 || check == 43) ? 2 : 3;
    case 6:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return check == 19 ? 1 : 0;
    case 7:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return check == 41 ? 2 : 3;
    case 8:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return (check == 43 || check == 57) ? 2 : 3;
    case 9:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        if (check == 12 || check == 20) return 0;
        if (check == 23) return 1;
        if (check == 49) return 3;
        return 2;
    case 10:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return check == 55 ? 3 : 2;
    case 11:
        if (!ReadCheckForPage(state, major, &check, error)) return 0;
        return check == 14 ? 0 : 1;
    default:
        return 0;
    }
}

bool SchoolDaysRouteMapOracle::IsCompletionSceneKey(const char* sceneKey,
                                                    size_t length) {
    static constexpr char kOp1[] = "-OP1";
    if (sceneKey == nullptr || length == 0u) return false;
    return length < sizeof(kOp1) - 1u ||
           std::memcmp(sceneKey + length - (sizeof(kOp1) - 1u), kOp1,
                       sizeof(kOp1) - 1u) != 0;
}

bool SchoolDaysRouteMapOracle::ComputeCompletion(
    const KtrfStateStore& state, const Kotonoha_KtrfDocument* document,
    Completion* completion, Kotonoha_KtrfError* error) {
    ClearError(error);
    if (document == nullptr || completion == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                    "School Days RouteMap completion argument is null");
    }

    const Kotonoha_KtrfSection* nodes = Kotonoha_KtrfFindSection(document, "NODE");
    if (nodes == nullptr) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days RouteMap completion corpus has no NODE section");
    }

    Completion next;
    next.scriptCount = 0;
    for (uint32_t nodeIndex = 0u; nodeIndex < nodes->item_count; ++nodeIndex) {
        Kotonoha_KtrfNode node{};
        if (!Kotonoha_KtrfGetNode(document, nodeIndex, &node, error)) return false;
        const char* nodeId = nullptr;
        size_t nodeIdLength = 0u;
        if (!Kotonoha_KtrfGetString(document, node.id_str, &nodeId,
                                    &nodeIdLength, error)) return false;
        static constexpr char kNodePrefix[] = "sdhq:node:";
        constexpr size_t kNodePrefixLength = sizeof(kNodePrefix) - 1u;
        if (nodeIdLength <= kNodePrefixLength ||
            std::memcmp(nodeId, kNodePrefix, kNodePrefixLength) != 0) {
            return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                        "School Days RouteMap NODE lacks authenticated SceneKey id");
        }
        const char* sceneKey = nodeId + kNodePrefixLength;
        const size_t sceneKeyLength = nodeIdLength - kNodePrefixLength;
        if (!IsCompletionSceneKey(sceneKey, sceneKeyLength)) continue;

        ++next.scriptCount;
        int64_t value = 0;
        if (!state.ReadGlobalInt(std::string(sceneKey, sceneKeyLength), &value,
                                 error)) {
            return false;
        }
        if (value != 0) ++next.readCount;
    }

    if (next.scriptCount != kCompletionScriptCount) {
        return Fail(error, KOTONOHA_KTRF_ERROR_FORMAT,
                    "School Days RouteMap completion corpus is not 1825 scripts");
    }
    next.percent = CompletionPercentForReadCount(next.readCount);
    *completion = next;
    return true;
}

} // namespace Kotonoha
