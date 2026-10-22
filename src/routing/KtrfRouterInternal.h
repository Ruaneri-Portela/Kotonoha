#pragma once

#include <Kotonoha/routing/KtrfRouter.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal profile seam.  Public KTRF routing remains unaware of persistence;
 * specialized adapters can observe each successfully applied transition effect
 * without intercepting direct variable writes used by load/reset/debug paths. */
typedef int (*Kotonoha_KtrfEffectObserverFn)(
    uint32_t effect_index, void* userdata, Kotonoha_KtrfError* error);

int Kotonoha_KtrfRouterTriggerObserved(
    Kotonoha_KtrfRouter* router, const char* trigger,
    Kotonoha_KtrfRouteResult* out, Kotonoha_KtrfEffectObserverFn observer,
    void* observer_userdata, Kotonoha_KtrfError* error);

int Kotonoha_KtrfRouterCommitChoiceValueObserved(
    Kotonoha_KtrfRouter* router, uint32_t choice_index,
    const Kotonoha_KtrfRuntimeValue* value, int* accepted,
    Kotonoha_KtrfEffectObserverFn observer, void* observer_userdata,
    Kotonoha_KtrfError* error);

#ifdef __cplusplus
}
#endif
