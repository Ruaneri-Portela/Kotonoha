#pragma once

#include <Kotonoha/routing/KtrfStateStore.hpp>

namespace Kotonoha {

// Read-only v1.02 RouteProcSDHQ.dll RouteMap oracles.
//
// These functions intentionally derive their answers from the live semantic
// state only. They do not cache or persist a shadow ROUTE/SCENE/SP projection.
// The caller remains responsible for native initialization order
// (GetStory()-1 -> GetRouteMapPage(rawMajor) -> UI clamping).
class SchoolDaysRouteMapOracle final {
public:
    // RouteProcSDHQ.dll:_CheckScript@8 (0x100084D0).
    // Returns the native RouteMap control/current-marker value, or zero when
    // the requested major is not the current story, ROUTE is out of range, or
    // no authenticated SCENE/SP witness is active.
    static int CheckScript(const KtrfStateStore& state, int major,
                           Kotonoha_KtrfError* error);

    // RouteProcSDHQ.dll:_GetRouteMapPage@8 (0x1000C120).
    // The major argument is deliberately not clamped here: SysMenu passes the
    // raw GetStory()-1 value before normalizing -1 to zero.
    static int GetRouteMapPage(const KtrfStateStore& state, int major,
                               Kotonoha_KtrfError* error);
};

} // namespace Kotonoha
